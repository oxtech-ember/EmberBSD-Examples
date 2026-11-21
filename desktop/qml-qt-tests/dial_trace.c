/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdio.h>

#include "eqml_input.h"

extern const struct eqml_scene eqml_dial;

static int
write_row(void *cookie, uint16_t y, const uint16_t *pixels, uint16_t width)
{
	FILE *out = cookie;
	(void)y;
	for (unsigned x = 0; x < width; ++x) {
		unsigned p = pixels[x];
		unsigned char rgb[] = {
			((p >> 11) * 255 + 15) / 31,
			(((p >> 5) & 63) * 255 + 31) / 63,
			((p & 31) * 255 + 15) / 31
		};
		if (fwrite(rgb, 1, sizeof(rgb), out) != sizeof(rgb))
			return -1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	const int picks[] = { 0, 100, 25, 75, 50, 0 };
	struct eqml_ui *ui;
	uint16_t line[352];
	int status = 1;

	if (argc != 2 || (ui = eqml_ui_create(&eqml_dial, NULL, NULL)) == NULL)
		return 1;
	for (unsigned i = 0; i < sizeof(picks) / sizeof(picks[0]); ++i) {
		const struct eqml_scene *frame;
		char path[1024];
		int n, rendered, closed;
		FILE *out;

		if (i != 0) {
			int x = 52 + picks[i] / 25 * 62;
			uint64_t now = i * 1000;
			if (eqml_ui_pointer(ui, EQML_DOWN, x, 332,
			    EQML_BUTTON_LEFT, 0, now) < 0 ||
			    eqml_ui_pointer(ui, EQML_UP, x, 332,
			    EQML_BUTTON_LEFT, 0, now + 20) < 0)
				goto done;
		}
		frame = eqml_ui_frame(ui);
		n = snprintf(path, sizeof(path), "%s/dial%u.ppm", argv[1], i);
		if (n < 0 || (size_t)n >= sizeof(path) || frame == NULL ||
		    frame->width != 352 || frame->height != 376 ||
		    (out = fopen(path, "wb")) == NULL)
			goto done;
		rendered = fprintf(out, "P6\n352 376\n255\n") > 0 &&
		    eqml_render(frame, NULL, line, 352, write_row, out) == 0;
		closed = fclose(out);
		if (!rendered || closed != 0)
			goto done;
	}
	status = 0;
done:
	eqml_ui_destroy(ui);
	return status;
}
