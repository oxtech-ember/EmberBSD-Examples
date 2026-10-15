/* SPDX-License-Identifier: MIT */
#ifndef EMBER_PNG_READER_H
#define EMBER_PNG_READER_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* RGB bytes or native-endian uint16 depth, always 640 x 480. Caller frees data. */
int read_tum_png(const char *, int, void **, char *, size_t);
#ifdef __cplusplus
}
#endif
#endif
