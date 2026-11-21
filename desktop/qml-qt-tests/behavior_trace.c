/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>

#include "eqml_input.h"

extern const struct eqml_scene eqml_behavior;

static int
red_x(struct eqml_ui *ui)
{
	const struct eqml_scene *frame = eqml_ui_frame(ui);

	if (frame != NULL) {
		for (size_t i = 0; i < frame->item_count; i++) {
			const struct eqml_item *item = &frame->items[i];
			if (item->kind == EQML_RECT && item->data.rect.color == 0xf800)
				return item->data.rect.x;
		}
	}
	return -1;
}

int
main(void)
{
	struct eqml_ui *ui = eqml_ui_create(&eqml_behavior, NULL, NULL);
	int status = 1;

	if (ui == NULL)
		return 1;
	for (int step = 0; step < 2; step++) {
		int x = step * 50;
		uint64_t t = (uint64_t)step * 2000;

		if (red_x(ui) != x ||
		    eqml_ui_pointer(ui, EQML_DOWN, x + 90, 150, EQML_BUTTON_LEFT, 0, t) < 0 ||
		    eqml_ui_pointer(ui, EQML_UP, x + 90, 150, EQML_BUTTON_LEFT, 0, t + 10) < 0 ||
		    eqml_ui_tick(ui, t + 510) < 0 || red_x(ui) != x + 25 ||
		    eqml_ui_tick(ui, t + 1010) < 0 || red_x(ui) != x + 50 ||
		    eqml_ui_timeout(ui, t + 1010) != -1)
			goto out;
		printf("x %d -> intermediate -> %d\n", x, x + 50);
	}
	status = 0;
out:
	eqml_ui_destroy(ui);
	return status;
}
