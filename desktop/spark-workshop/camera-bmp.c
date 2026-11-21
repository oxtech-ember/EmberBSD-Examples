/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/stat.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "camera.h"
#include "camera-bmp.h"

enum {
	HEADER = 54,
	ROW = WORKSHOP_CAMERA_WIDTH * 3,
	FILE_BYTES = HEADER + ROW * WORKSHOP_CAMERA_HEIGHT,
};

static void
put32(uint8_t *p, uint32_t n)
{
	p[0] = (uint8_t)n;
	p[1] = (uint8_t)(n >> 8);
	p[2] = (uint8_t)(n >> 16);
	p[3] = (uint8_t)(n >> 24);
}

static void
bmp_header(uint8_t *p)
{
	memset(p, 0, HEADER);
	p[0] = 'B';
	p[1] = 'M';
	put32(p + 2, FILE_BYTES);
	put32(p + 10, HEADER);
	put32(p + 14, 40);	/* BITMAPINFOHEADER */
	put32(p + 18, WORKSHOP_CAMERA_WIDTH);
	put32(p + 22, (uint32_t)-WORKSHOP_CAMERA_HEIGHT);
	p[26] = 1;		/* planes */
	p[28] = 24;		/* BGR, rows already DWORD-aligned */
	put32(p + 34, FILE_BYTES - HEADER);
}

static int
read_at(int fd, uint8_t *p, size_t len, off_t at)
{
	while (len != 0) {
		ssize_t n = pread(fd, p, len, at);
		if (n == -1 && errno == EINTR)
			continue;
		if (n == -1)
			return -1;
		if (n == 0) {
			errno = EIO;
			return -1;
		}
		at += n;
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

static int
write_at(int fd, const uint8_t *p, size_t len, off_t at)
{
	while (len != 0) {
		ssize_t n = pwrite(fd, p, len, at);
		if (n == -1 && errno == EINTR)
			continue;
		if (n == -1)
			return -1;
		if (n == 0) {
			errno = EIO;
			return -1;
		}
		at += n;
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

static uint8_t
channel(int n)
{
	if (n <= 0)
		return 0;
	if (n >= 255 * 256)
		return 255;
	return (uint8_t)((unsigned)n >> 8);
}

int
workshop_camera_write_bmp(int fd, const uint8_t *uyvy, size_t size)
{
	struct stat st;
	uint8_t header[HEADER], row[ROW];

	if (uyvy == NULL || size != WORKSHOP_CAMERA_BYTES) {
		errno = EINVAL;
		return -1;
	}
	if (fstat(fd, &st) == -1)
		return -1;
	if (!S_ISREG(st.st_mode) || st.st_size != 0) {
		errno = EINVAL;
		return -1;
	}
	bmp_header(header);
	if (write_at(fd, header, sizeof(header), 0) == -1)
		return -1;
	for (int y = 0; y < WORKSHOP_CAMERA_HEIGHT; y++) {
		uint8_t *out = row;
		for (unsigned x = 0; x < WORKSHOP_CAMERA_WIDTH; x += 2, uyvy += 4) {
			/* BT.601 studio-range Y'CbCr as used by the SD camera. */
			int u = (int)uyvy[0] - 128, v = (int)uyvy[2] - 128;
			int red = 409 * v, green = -100 * u - 208 * v, blue = 516 * u;
			for (unsigned i = 1; i <= 3; i += 2) {
				int luma = 298 * ((int)uyvy[i] - 16) + 128;
				*out++ = channel(luma + blue);
				*out++ = channel(luma + green);
				*out++ = channel(luma + red);
			}
		}
		if (write_at(fd, row, sizeof(row), HEADER + (off_t)y * ROW) == -1)
			return -1;
	}
	return 0;
}

int
workshop_bmp_validate(int fd)
{
	struct stat st;
	uint8_t header[HEADER], expected[HEADER];

	if (fstat(fd, &st) == -1)
		return -1;
	if (!S_ISREG(st.st_mode) || st.st_size != FILE_BYTES) {
		errno = EINVAL;
		return -1;
	}
	bmp_header(expected);
	if (read_at(fd, header, sizeof(header), 0) == -1)
		return -1;
	if (memcmp(header, expected, HEADER) != 0) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

int
workshop_bmp_read_row(int fd, unsigned y, uint8_t bgr[WORKSHOP_CAMERA_WIDTH * 3])
{
	if (y >= WORKSHOP_CAMERA_HEIGHT) {
		errno = EINVAL;
		return -1;
	}
	return read_at(fd, bgr, ROW, HEADER + (off_t)y * ROW);
}

int
workshop_camera_thumbnail(int fd, uint16_t *pixels, size_t count)
{
	uint8_t row[ROW];

	if (pixels == NULL || count != WORKSHOP_THUMB_WIDTH * WORKSHOP_THUMB_HEIGHT) {
		errno = EINVAL;
		return -1;
	}
	if (workshop_bmp_validate(fd) == -1)
		return -1;
	for (int y = 0; y < WORKSHOP_THUMB_HEIGHT; y++) {
		if (workshop_bmp_read_row(fd, (unsigned)y * 4, row) == -1)
			return -1;
		for (unsigned x = 0; x < WORKSHOP_THUMB_WIDTH; x++) {
			const uint8_t *bgr = row + x * 4 * 3;
			*pixels++ = (uint16_t)(((bgr[2] >> 3) << 11) |
			    ((bgr[1] >> 2) << 5) | (bgr[0] >> 3));
		}
	}
	return 0;
}
