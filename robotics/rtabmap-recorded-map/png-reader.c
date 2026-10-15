/* SPDX-License-Identifier: MIT */
#include "png-reader.h"
#include <png.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct reader {
    FILE *file;
    png_structp png;
    png_infop info;
    unsigned char *data;
    png_bytep rows[480];
    char *error;
    size_t error_size;
    int ok;
};

static void
png_failure(png_structp png, png_const_charp text)
{
    struct reader *r = png_get_error_ptr(png);
    snprintf(r->error, r->error_size, "%s", text);
    png_longjmp(png, 1);
}

static void
reader_warning(png_structp png, png_const_charp text)
{
    (void)png;
    (void)text;
}

int
read_tum_png(const char *path, int depth, void **out, char *error, size_t error_size)
{
    struct reader *r;
    unsigned int y;
    size_t stride;
    const uint16_t native = 1;

    *out = NULL;
    r = calloc(1, sizeof(*r));
    if (r == NULL) {
        snprintf(error, error_size, "out of memory");
        return 0;
    }
    r->error = error;
    r->error_size = error_size;
    r->file = fopen(path, "rb");
    if (r->file == NULL) {
        snprintf(error, error_size, "cannot open PNG");
        goto done;
    }
    r->png = png_create_read_struct(PNG_LIBPNG_VER_STRING, r, png_failure, reader_warning);
    if (r->png == NULL) {
        snprintf(error, error_size, "cannot allocate PNG reader");
        goto done;
    }
    r->info = png_create_info_struct(r->png);
    if (r->info == NULL) {
        snprintf(error, error_size, "cannot allocate PNG info");
        goto done;
    }
    if (setjmp(png_jmpbuf(r->png)))
        goto done;
    png_init_io(r->png, r->file);
    png_set_user_limits(r->png, 640, 480);
    png_set_crc_action(r->png, PNG_CRC_ERROR_QUIT, PNG_CRC_ERROR_QUIT);
    png_read_info(r->png, r->info);
    if (png_get_image_width(r->png, r->info) != 640 ||
        png_get_image_height(r->png, r->info) != 480 ||
        png_get_bit_depth(r->png, r->info) != (depth ? 16 : 8) ||
        png_get_color_type(r->png, r->info) != (depth ? PNG_COLOR_TYPE_GRAY : PNG_COLOR_TYPE_RGB))
        png_error(r->png, "expected TUM 640x480 RGB8 or gray16 depth PNG");
    if (depth && *(const unsigned char *)&native == 1)
        png_set_swap(r->png);
    /* No gamma, scale, channel-order or depth-value conversion is performed. */
    png_set_interlace_handling(r->png);
    png_read_update_info(r->png, r->info);
    stride = (size_t)640 * (depth ? 2 : 3);
    if (png_get_rowbytes(r->png, r->info) != stride)
        png_error(r->png, "unexpected PNG row size");
    r->data = malloc(stride * 480);
    if (r->data == NULL)
        png_error(r->png, "out of memory");
    for (y = 0; y < 480; ++y)
        r->rows[y] = r->data + y * stride;
    png_read_image(r->png, r->rows);
    png_read_end(r->png, r->info);
    *out = r->data;
    r->data = NULL;
    r->ok = 1;
done:
    if (r->png != NULL)
        png_destroy_read_struct(&r->png, &r->info, NULL);
    if (r->file != NULL)
        fclose(r->file);
    const int ok = r->ok;
    free(r->data);
    free(r);
    return ok;
}
