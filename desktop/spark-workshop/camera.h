/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef SPARK_WORKSHOP_CAMERA_H
#define SPARK_WORKSHOP_CAMERA_H

#include <stddef.h>
#include <stdint.h>

#define WORKSHOP_THUMB_WIDTH 160
#define WORKSHOP_THUMB_HEIGHT 120

/* Worker operation. Write one 640x480, top-down, RGB24 BMP to an empty
 * regular file. The caller owns both the fd and cleanup of its private file.
 * All functions return 0 on success, -1 with errno on failure. */
int workshop_camera_capture(const char *device, int bmp_fd);

/* Read a completed photo produced above without moving the file offset.
 * Caller provides WORKSHOP_THUMB_WIDTH * WORKSHOP_THUMB_HEIGHT RGB565 pixels. */
int workshop_camera_thumbnail(int bmp_fd, uint16_t *pixels, size_t count);

#endif
