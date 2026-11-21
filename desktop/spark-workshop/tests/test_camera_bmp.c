/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../camera.h"
#include "../camera-bmp.h"

int
main(void)
{
	static uint8_t uyvy[WORKSHOP_CAMERA_BYTES];
	static uint16_t thumb[WORKSHOP_THUMB_WIDTH * WORKSHOP_THUMB_HEIGHT];
	/* Black, white, red, green and blue in BT.601 studio range. */
	static const uint8_t uyvy_pairs[][4] = {
		{128,16,128,235}, {128,235,128,16}, {90,81,240,81},
		{54,145,34,145}, {240,41,110,41}
	};
	static const uint16_t colors[] = {0, 0xffff, 0xf800, 0x07e0, 0x001f};
	char path[] = "/tmp/workshop-photo.XXXXXX";
	const size_t count = sizeof(thumb) / sizeof(*thumb);
	uint8_t pair[6];
	struct stat st;
	int fd = mkstemp(path);
	assert(fd >= 0);
	assert(unlink(path) == 0);
	for (unsigned y = 0; y < WORKSHOP_CAMERA_HEIGHT; y++) {
		for (unsigned x = 0; x < WORKSHOP_CAMERA_WIDTH; x += 2) {
			unsigned color = (y / 4 + x / 4) % 5;
			memcpy(uyvy + y * WORKSHOP_CAMERA_STRIDE + 2 * x, uyvy_pairs[color], 4);
		}
	}
	assert(workshop_camera_write_bmp(fd, uyvy, sizeof(uyvy) - 1) == -1 && errno == EINVAL);
	assert(fstat(fd, &st) == 0 && st.st_size == 0);
	assert(workshop_camera_write_bmp(fd, uyvy, sizeof(uyvy)) == 0);
	assert(fstat(fd, &st) == 0 && st.st_size == 921654);
	assert(pread(fd, pair, sizeof(pair), 54) == 6);
	assert(memcmp(pair, "\0\0\0\xff\xff\xff", 6) == 0);
	assert(workshop_camera_write_bmp(fd, uyvy, sizeof(uyvy)) == -1 && errno == EINVAL);
	assert(lseek(fd, 27, SEEK_SET) == 27);
	assert(workshop_camera_thumbnail(fd, thumb, count - 1) == -1 && errno == EINVAL);
	assert(workshop_camera_thumbnail(fd, thumb, count) == 0);
	assert(lseek(fd, 0, SEEK_CUR) == 27);
	for (unsigned y = 0; y < WORKSHOP_THUMB_HEIGHT; y++) {
		for (unsigned x = 0; x < WORKSHOP_THUMB_WIDTH; x++)
			assert(thumb[y * WORKSHOP_THUMB_WIDTH + x] == colors[(x + y) % 5]);
	}
	assert(pwrite(fd, "X", 1, 0) == 1);
	assert(workshop_camera_thumbnail(fd, thumb, count) == -1 && errno == EINVAL);
	assert(ftruncate(fd, 54) == 0);
	assert(workshop_camera_thumbnail(fd, thumb, count) == -1 && errno == EINVAL);
	assert(close(fd) == 0);
	return 0;
}
