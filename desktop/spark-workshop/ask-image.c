/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <spng.h>

#include "ask-image.h"
#include "camera-bmp.h"

int
workshop_photo_png(const char *path, int dest)
{
	struct spng_ihdr header = { .width = WORKSHOP_CAMERA_WIDTH,
	    .height = WORKSHOP_CAMERA_HEIGHT, .bit_depth = 8,
	    .color_type = SPNG_COLOR_TYPE_TRUECOLOR };
	struct stat st;
	uint8_t row[WORKSHOP_CAMERA_WIDTH * 3];
	spng_ctx *ctx = NULL;
	FILE *stream = NULL;
	int in, out, error = 0;

	if (fstat(dest, &st) == -1)
		return -1;
	if (!S_ISREG(st.st_mode) || st.st_size != 0 ||
	    lseek(dest, 0, SEEK_CUR) != 0) {
		errno = EINVAL;
		return -1;
	}
	in = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (in == -1)
		return -1;
	if (workshop_bmp_validate(in) == -1 || (out = dup(dest)) == -1) {
		error = errno;
		goto done;
	}
	stream = fdopen(out, "wb");
	if (stream == NULL) {
		error = errno;
		(void)close(out);
		goto done;
	}
	ctx = spng_ctx_new(SPNG_CTX_ENCODER);
	if (ctx == NULL) {
		error = ENOMEM;
		goto done;
	}
	if (spng_set_png_file(ctx, stream) || spng_set_ihdr(ctx, &header) ||
	    spng_set_option(ctx, SPNG_IMG_COMPRESSION_LEVEL, 1) ||
	    spng_encode_image(ctx, NULL, 0, SPNG_FMT_PNG,
	    SPNG_ENCODE_PROGRESSIVE | SPNG_ENCODE_FINALIZE)) {
		error = EIO;
		goto done;
	}
	for (unsigned y = 0; y < WORKSHOP_CAMERA_HEIGHT; y++) {
		if (workshop_bmp_read_row(in, y, row) == -1) {
			error = errno;
			goto done;
		}
		for (unsigned x = 0; x < sizeof(row); x += 3) {
			uint8_t blue = row[x];
			row[x] = row[x + 2];
			row[x + 2] = blue;
		}
		int ret = spng_encode_row(ctx, row, sizeof(row));
		if (ret != (y + 1 == WORKSHOP_CAMERA_HEIGHT ? SPNG_EOI : SPNG_OK)) {
			error = EIO;
			goto done;
		}
	}
done:
	spng_ctx_free(ctx);
	if (stream != NULL && fclose(stream) == EOF && error == 0)
		error = errno;
	if (close(in) == -1 && error == 0)
		error = errno;
	if (error != 0) {
		errno = error;
		return -1;
	}
	return 0;
}
