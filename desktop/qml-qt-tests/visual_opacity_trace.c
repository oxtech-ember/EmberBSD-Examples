/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdio.h>

#include "eqml_input.h"

extern const struct eqml_scene eqml_visualopacity;

/* Each state has few colors. Record every horizontal run for comparison with Qt. */
static int
write_row(void *cookie, uint16_t y, const uint16_t *pixels, uint16_t width)
{
	for (unsigned x = 0, end; x < width; x = end) {
		for (end = x + 1; end < width && pixels[end] == pixels[x]; end++)
			continue;
		if (printf("%u %u %u %u %04x\n", *(unsigned *)cookie, y,
		    x, end, (unsigned)pixels[x]) < 0)
			return -1;
	}
	return 0;
}

int
main(void)
{
	struct eqml_ui *ui = eqml_ui_create(&eqml_visualopacity, NULL, NULL);
	const int taps[][2] = { { 10, 10 }, { 125, 375 }, { 70, 320 } };
	uint16_t line[320];
	int status = 1;

	if (ui == NULL)
		return 1;
	for (unsigned state = 0; state <= sizeof(taps) / sizeof(taps[0]); state++) {
		const struct eqml_scene *frame;
		if (state != 0) {
			uint64_t now = (uint64_t)state * 1000;
			int x = taps[state - 1][0], y = taps[state - 1][1];
			if (eqml_ui_pointer(ui, EQML_DOWN, x, y, EQML_BUTTON_LEFT, 0, now) < 0 ||
			    eqml_ui_pointer(ui, EQML_UP, x, y, EQML_BUTTON_LEFT, 0, now + 20) < 0)
				goto out;
		}
		frame = eqml_ui_frame(ui);
		if (frame == NULL || frame->width != 320 || frame->height != 480 ||
		    eqml_render(frame, NULL, line, 320, write_row, &state) != 0)
			goto out;
	}
	status = 0;
out:
	eqml_ui_destroy(ui);
	return status;
}
