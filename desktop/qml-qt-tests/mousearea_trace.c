/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>

#include "eqml_input.h"

extern const struct eqml_scene eqml_mousearea;

struct controls {
	const struct eqml_text *info, *button, *position;
	const struct eqml_item *red, *blue;
};

/* The original QML declares its three status texts after the two squares. */
static struct controls
controls(struct eqml_ui *ui)
{
	struct controls c = {0};
	const struct eqml_scene *f = eqml_ui_frame(ui);
	const struct eqml_text *status[3];
	size_t i, count = 0;
	if (f == NULL)
		exit(1);
	for (i = 0; i < f->item_count; i++) {
		const struct eqml_item *item = &f->items[i];
		if (item->kind == EQML_RECT) {
			if (item->data.rect.color == 0xf800)
				c.red = item;
			if (item->data.rect.color == 0x001f)
				c.blue = item;
		} else if (item->kind == EQML_TEXT && item->data.text.y >= 300 && count < 3) {
			status[count++] = &item->data.text;
		}
	}
	if (count != 3 || !c.blue || !c.red)
		exit(1);
	c.info = status[0];
	c.button = status[1];
	c.position = status[2];
	return c;
}

static void
send(struct eqml_ui *ui, enum eqml_phase action, int x, int y, uint64_t ms)
{
	if (eqml_ui_pointer(ui, action, x, y, EQML_BUTTON_LEFT, 0, ms) == -1) {
		perror("pointer event");
		exit(1);
	}
}

int
main(void)
{
	struct eqml_ui *ui = eqml_ui_create(&eqml_mousearea, NULL, NULL);
	struct controls c;
	if (ui == NULL) {
		perror("eqml_ui_create");
		return 1;
	}
	send(ui, EQML_DOWN, 30, 40, 0);
	c = controls(ui);
	printf("press: %s | %s | opacity %.1f\n", c.info->prefix, c.position->prefix,
	    1. - (double)c.red->transparency / 255.);
	send(ui, EQML_UP, 30, 40, 10);
	printf("tap: %s\n", controls(ui).button->prefix);
	send(ui, EQML_DOWN, 30, 40, 1000);
	if (eqml_ui_tick(ui, 1801) != 1)
		return 1;
	printf("hold: %s\n", controls(ui).button->prefix);
	send(ui, EQML_UP, 30, 40, 1810);
	printf("lift: %s\n", controls(ui).button->prefix);
	send(ui, EQML_DOWN, 30, 40, 2200);
	send(ui, EQML_UP, 30, 40, 2210);
	send(ui, EQML_DOWN, 30, 40, 2310);
	printf("double: %s\n", controls(ui).button->prefix);
	send(ui, EQML_UP, 30, 40, 2320);
	printf("double-lift: %s\n", controls(ui).button->prefix);
	if (eqml_ui_pointer(ui, EQML_DOWN, 30, 40, EQML_BUTTON_RIGHT, EQML_MODIFIER_SHIFT, 2800) != 1)
		return 1;
	printf("right: %s\n", controls(ui).info->prefix);
	send(ui, EQML_MOVE, 140, 150, 2810);
	c = controls(ui);
	printf("outside: %s | opacity %.1f\n", c.info->prefix,
	    1. - (double)c.red->transparency / 255.);
	send(ui, EQML_MOVE, 30, 40, 2820);
	if (eqml_ui_pointer(ui, EQML_UP, 30, 40, EQML_BUTTON_RIGHT, EQML_MODIFIER_SHIFT, 2830) != 1)
		return 1;
	send(ui, EQML_DOWN, 240, 50, 3000);
	send(ui, EQML_MOVE, 224, 75, 3010);
	send(ui, EQML_MOVE, 160, 150, 3020);
	send(ui, EQML_MOVE, 6, 460, 3030);
	send(ui, EQML_UP, 6, 460, 3040);
	c = controls(ui);
	printf("drag: %d,%d\n", (int)c.blue->data.rect.x, (int)c.blue->data.rect.y);
	eqml_ui_destroy(ui);
	return 0;
}
