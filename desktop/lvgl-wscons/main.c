/* SPDX-License-Identifier: BSD-2-Clause */
/* A small LVGL application using only wsdisplay and wsmouse. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <dev/wscons/wsconsio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <lvgl/lvgl.h>

#define DRAW_LINES 16
struct app {
	int mouse_fd;
	uint8_t *fb;
	size_t stride;
	unsigned int width, height;
	int x, y, pressed, input_error;
	lv_obj_t *status;
};
static struct app app;
static volatile sig_atomic_t stopping;

static void
signal_handler(int signo)
{
	(void)signo;
	stopping = 1;
}

static void
flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
	int y;
	size_t bytes = (size_t)(area->x2 - area->x1 + 1) * 2;

	for (y = area->y1; y <= area->y2; y++) {
		memcpy(app.fb + (size_t)y * app.stride + (size_t)area->x1 * 2,
		       data, bytes);
		data += bytes;
	}
	lv_display_flush_ready(display);
}

static int
clamp(int value, unsigned int limit)
{
	if (value < 0)
		return 0;
	if ((unsigned int)value >= limit)
		return (int)limit - 1;
	return value;
}

static bool
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

static void
read_mouse(lv_indev_t *indev, lv_indev_data_t *data)
{
	struct wscons_event event;
	ssize_t n;
	bool update = false;

	(void)indev;
	n = read(app.mouse_fd, &event, sizeof(event));
	if (n == (ssize_t)sizeof(event)) {
		switch (event.type) {
		case WSCONS_EVENT_MOUSE_ABSOLUTE_X:
			app.x = clamp(event.value, app.width);
			update = app.pressed;
			break;
		case WSCONS_EVENT_MOUSE_ABSOLUTE_Y:
			app.y = clamp(event.value, app.height);
			update = app.pressed;
			break;
		case WSCONS_EVENT_MOUSE_DOWN:
			if (event.value == 0) {
				app.pressed = 1;
				update = true;
			}
			break;
		case WSCONS_EVENT_MOUSE_UP:
			if (event.value == 0)
				app.pressed = 0;
			break;
		default:
			break;
		}
		if (update)
			lv_label_set_text_fmt(app.status, "Touch: %d, %d",
			    app.x, app.y);
		data->continue_reading = true;
	} else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR) ||
		   n > 0) {
		app.input_error = 1;
	}
	data->point.x = app.x;
	data->point.y = app.y;
	data->state =
	    app.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static uint64_t
milliseconds(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
		return 0;
	return (uint64_t)ts.tv_sec * 1000 + (unsigned int)ts.tv_nsec / 1000000;
}

int
main(int argc, char **argv)
{
	struct wsdisplay_fbinfo info;
	struct wsdisplayio_fbinfo fbinfo = {0};
	struct sigaction sa;
	struct pollfd pfd;
	lv_display_t *display;
	lv_indev_t *input;
	uint16_t *drawbuf = NULL;
	size_t size, bufsize;
	u_int linebytes, old_mode, mode;
	uint64_t last, now;
	int fbfd = -1, version, ret = 1;

	if (argc != 3) {
		fprintf(stderr, "usage: %s wsdisplay wsmouse\n", argv[0]);
		return 2;
	}
	memset(&app, 0, sizeof(app));
	app.mouse_fd = -1;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGINT, &sa, NULL) == -1 ||
	    sigaction(SIGTERM, &sa, NULL) == -1) {
		perror("sigaction");
		goto out;
	}
	fbfd = open(argv[1], O_RDWR);
	if (fbfd == -1 || ioctl(fbfd, WSDISPLAYIO_GINFO, &info) == -1 ||
	    ioctl(fbfd, WSDISPLAYIO_GET_FBINFO, &fbinfo) == -1 ||
	    ioctl(fbfd, WSDISPLAYIO_GMODE, &old_mode) == -1 ||
	    ioctl(fbfd, WSDISPLAYIO_LINEBYTES, &linebytes) == -1) {
		perror("wsdisplay");
		goto out;
	}
	app.stride = linebytes;
	if (!valid_framebuffer(&info, &fbinfo, linebytes)) {
		fprintf(stderr, "unsupported framebuffer geometry or format\n");
		goto out;
	}
	app.width = info.width;
	app.height = info.height;
	size = app.stride * app.height;
	mode = WSDISPLAYIO_MODE_DUMBFB;
	if (ioctl(fbfd, WSDISPLAYIO_SMODE, &mode) == -1) {
		perror("wsdisplay graphics mode");
		goto out;
	}
	app.fb = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
	if (app.fb == MAP_FAILED) {
		app.fb = NULL;
		perror("wsdisplay mmap");
		goto restore;
	}
	app.mouse_fd = open(argv[2], O_RDONLY | O_NONBLOCK);
	version = WSMOUSE_EVENT_VERSION;
	if (app.mouse_fd == -1 ||
	    ioctl(app.mouse_fd, WSMOUSEIO_SETVERSION, &version) == -1) {
		perror("wsmouse");
		goto restore;
	}
	bufsize = (size_t)app.width * DRAW_LINES * sizeof(*drawbuf);
	drawbuf = malloc(bufsize);
	if (drawbuf == NULL) {
		perror("draw buffer");
		goto restore;
	}
	lv_init();
	display = lv_display_create(app.width, app.height);
	if (display == NULL) {
		fprintf(stderr, "cannot create display\n");
		goto deinit;
	}
	lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
	lv_display_set_buffers(display, drawbuf, NULL, bufsize,
			       LV_DISPLAY_RENDER_MODE_PARTIAL);
	lv_display_set_flush_cb(display, flush);
	lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x102030),
				  0);
	app.status = lv_label_create(lv_screen_active());
	if (app.status == NULL) {
		fprintf(stderr, "cannot create status label\n");
		goto deinit;
	}
	lv_label_set_text(app.status, "EmberBSD ready - touch the screen");
	lv_obj_set_style_text_color(app.status, lv_color_hex(0xffffff), 0);
	lv_obj_center(app.status);
	input = lv_indev_create();
	if (input == NULL) {
		fprintf(stderr, "cannot create input\n");
		goto deinit;
	}
	lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
	lv_indev_set_read_cb(input, read_mouse);
	pfd.fd = app.mouse_fd;
	pfd.events = POLLIN;
	last = milliseconds();
	while (!stopping && !app.input_error) {
		now = milliseconds();
		if (now >= last)
			lv_tick_inc((uint32_t)(now - last));
		last = now;
		(void)lv_timer_handler();
		if (poll(&pfd, 1, 10) == -1 && errno != EINTR) {
			perror("poll");
			goto deinit;
		}
	}
	if (!app.input_error)
		ret = 0;
	else
		fprintf(stderr, "wsmouse input failed\n");
deinit:
	lv_deinit();
restore:
	if (app.mouse_fd != -1)
		close(app.mouse_fd);
	if (app.fb != NULL)
		munmap(app.fb, size);
	if (ioctl(fbfd, WSDISPLAYIO_SMODE, &old_mode) == -1) {
		perror("restore wsdisplay mode");
		ret = 1;
	}
out:
	free(drawbuf);
	if (fbfd != -1)
		close(fbfd);
	return ret;
}
