/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <dev/wscons/wsconsio.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "weather-display.h"

static int
valid_framebuffer(const struct wsdisplay_fbinfo *info,
    const struct wsdisplayio_fbinfo *fbinfo, u_int stride)
{
	const struct _fbi_rgbmasks *rgb = &fbinfo->fbi_subtype.fbi_rgbmasks;

	return info->depth == 16 && info->width > 0 && info->height > 0 &&
	    info->width <= 4096 && info->height <= 4096 &&
	    stride >= (size_t)info->width * 2 &&
	    stride <= SIZE_MAX / info->height &&
	    fbinfo->fbi_width == info->width &&
	    fbinfo->fbi_height == info->height &&
	    fbinfo->fbi_stride == stride && fbinfo->fbi_bitsperpixel == 16 &&
	    fbinfo->fbi_pixeltype == WSFB_RGB && fbinfo->fbi_fboffset == 0 &&
	    fbinfo->fbi_fbsize >= (uint64_t)stride * info->height &&
	    rgb->red_offset == 11 && rgb->red_size == 5 &&
	    rgb->green_offset == 5 && rgb->green_size == 6 &&
	    rgb->blue_offset == 0 && rgb->blue_size == 5 &&
	    rgb->alpha_size == 0;
}

int
weather_display_open(struct weather_display *display, const char *device)
{
	struct wsdisplay_fbinfo info;
	struct wsdisplayio_fbinfo fbinfo = {0};
	u_int stride, mode;
	int error;

	memset(display, 0, sizeof(*display));
	display->fd = -1;
	display->fd = open(device, O_RDWR);
	if (display->fd == -1)
		return -1;
	if (ioctl(display->fd, WSDISPLAYIO_GINFO, &info) == -1 ||
	    ioctl(display->fd, WSDISPLAYIO_GET_FBINFO, &fbinfo) == -1 ||
	    ioctl(display->fd, WSDISPLAYIO_GMODE, &display->old_mode) == -1 ||
	    ioctl(display->fd, WSDISPLAYIO_LINEBYTES, &stride) == -1)
		goto fail;
	if (!valid_framebuffer(&info, &fbinfo, stride)) {
		errno = ENOTSUP;
		goto fail;
	}
	display->width = info.width;
	display->height = info.height;
	display->stride = stride;
	display->map_size = display->stride * display->height;
	mode = WSDISPLAYIO_MODE_DUMBFB;
	if (ioctl(display->fd, WSDISPLAYIO_SMODE, &mode) == -1)
		goto fail;
	display->mode_changed = 1;
	display->fb = mmap(NULL, display->map_size, PROT_READ | PROT_WRITE,
	    MAP_SHARED, display->fd, 0);
	if (display->fb == MAP_FAILED) {
		display->fb = NULL;
		goto fail;
	}
	return 0;

fail:
	error = errno;
	(void)weather_display_close(display);
	errno = error;
	return -1;
}

int
weather_display_blit_rgb565(const struct weather_display *display, unsigned int x,
    unsigned int y, unsigned int width, unsigned int height,
    const uint16_t *pixels, size_t source_stride)
{
	unsigned int row;
	size_t row_bytes;

	if (display->fb == NULL || pixels == NULL || width == 0 || height == 0 ||
	    x > display->width || y > display->height ||
	    width > display->width - x || height > display->height - y ||
	    (size_t)width > SIZE_MAX / sizeof(*pixels)) {
		errno = EINVAL;
		return -1;
	}
	row_bytes = (size_t)width * sizeof(*pixels);
	if (source_stride < row_bytes) {
		errno = EINVAL;
		return -1;
	}
	if (height > 1 && source_stride > SIZE_MAX / (height - 1)) {
		errno = EOVERFLOW;
		return -1;
	}
	for (row = 0; row < height; row++)
		memcpy(display->fb + (size_t)(y + row) * display->stride +
		    (size_t)x * sizeof(*pixels),
		    (const uint8_t *)pixels + (size_t)row * source_stride,
		    row_bytes);
	return 0;
}

int
weather_display_close(struct weather_display *display)
{
	int error = 0;

	if (display->fb != NULL && munmap(display->fb, display->map_size) == -1)
		error = errno;
	if (display->mode_changed && display->fd != -1 &&
	    ioctl(display->fd, WSDISPLAYIO_SMODE, &display->old_mode) == -1 &&
	    error == 0)
		error = errno;
	if (display->fd != -1 && close(display->fd) == -1 && error == 0)
		error = errno;
	display->fb = NULL;
	display->fd = -1;
	display->mode_changed = 0;
	if (error != 0) {
		errno = error;
		return -1;
	}
	return 0;
}
