/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef SPARK_WORKSHOP_CAMERA_BMP_H
#define SPARK_WORKSHOP_CAMERA_BMP_H

#include <stddef.h>
#include <stdint.h>

#define WORKSHOP_CAMERA_WIDTH 640
#define WORKSHOP_CAMERA_HEIGHT 480
#define WORKSHOP_CAMERA_STRIDE (WORKSHOP_CAMERA_WIDTH * 2U)
#define WORKSHOP_CAMERA_BYTES (WORKSHOP_CAMERA_STRIDE * WORKSHOP_CAMERA_HEIGHT)

int workshop_camera_write_bmp(int, const uint8_t *, size_t);

#endif
