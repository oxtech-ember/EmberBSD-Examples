/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <spng.h>

#include "ask-image.h"
#include "camera-bmp.h"

int
main(void)
{
	char name[] = "/tmp/workshop-test-bmp.XXXXXX";
	int in = mkstemp(name);
	FILE *png = tmpfile();
	uint8_t *uyvy = malloc(WORKSHOP_CAMERA_BYTES);
	uint8_t bgr[WORKSHOP_CAMERA_WIDTH * 3], rgb[sizeof(bgr)];
	struct spng_ihdr header;
	spng_ctx *decoder = spng_ctx_new(0);

	assert(in != -1 && png && uyvy && decoder);
	assert(workshop_photo_png(name, fileno(png)) == -1);
	for (size_t i = 0; i < WORKSHOP_CAMERA_BYTES; i++)
		uyvy[i] = (uint8_t)(i * 17 + i / WORKSHOP_CAMERA_STRIDE);
	assert(workshop_camera_write_bmp(in, uyvy, WORKSHOP_CAMERA_BYTES) == 0);
	free(uyvy);
	assert(workshop_photo_png(name, fileno(png)) == 0);
	assert(workshop_photo_png(name, fileno(png)) == -1);
	rewind(png);
	assert(spng_set_png_file(decoder, png) == 0);
	assert(spng_get_ihdr(decoder, &header) == 0);
	assert(header.width == WORKSHOP_CAMERA_WIDTH && header.height == WORKSHOP_CAMERA_HEIGHT);
	assert(spng_decode_image(decoder, NULL, 0, SPNG_FMT_RGB8, SPNG_DECODE_PROGRESSIVE) == 0);
	for (unsigned y = 0; y < header.height; y++) {
		int result = spng_decode_row(decoder, rgb, sizeof(rgb));
		assert(result == (y + 1 == header.height ? SPNG_EOI : SPNG_OK));
		assert(workshop_bmp_read_row(in, y, bgr) == 0);
		for (unsigned x = 0; x < sizeof(bgr); x += 3) {
			assert(rgb[x] == bgr[x + 2]);
			assert(rgb[x + 1] == bgr[x + 1]);
			assert(rgb[x + 2] == bgr[x]);
		}
	}
	spng_ctx_free(decoder);
	int close_png = fclose(png);
	int close_bmp = close(in);
	int unlink_bmp = unlink(name);
	assert(close_png == 0 && close_bmp == 0 && unlink_bmp == 0);
	puts("BMP-to-PNG encoder: PASS");
}
