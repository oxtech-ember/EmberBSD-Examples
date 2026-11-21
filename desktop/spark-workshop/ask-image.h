/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef SPARK_WORKSHOP_ASK_IMAGE_H
#define SPARK_WORKSHOP_ASK_IMAGE_H
/* Convert one accepted camera BMP into a full-resolution PNG, starting at
 * offset zero of an empty regular file owned by the caller. */
int workshop_photo_png(const char *, int);
#endif
