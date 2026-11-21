/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "eqml.h"

extern const struct eqml_scene eqml_horizontalspacing;

#define VIEW_WIDTH 320
#define VIEW_HEIGHT 240

struct crc_context {
	uint32_t value;
};

static void
crc_byte(struct crc_context *context, uint8_t byte)
{
	unsigned int bit;

	context->value ^= byte;
	for (bit = 0; bit < 8; bit++)
		context->value = (context->value >> 1) ^
		    (0xedb88320U & (0U - (context->value & 1U)));
}

static int
consume_row(void *cookie, uint16_t y, const uint16_t *pixels, uint16_t width)
{
	struct crc_context *context = cookie;
	size_t x;
	(void)y;

	for (x = 0; x < width; x++) {
		crc_byte(context, (uint8_t)pixels[x]);
		crc_byte(context, (uint8_t)(pixels[x] >> 8));
	}
	return 0;
}

int
main(void)
{
	struct eqml_scene view;
	struct crc_context crc = { .value = 0xffffffffU };
	uint16_t line[VIEW_WIDTH];

	if (eqml_horizontalspacing.width != 640 ||
	    eqml_horizontalspacing.height != 480 ||
	    eqml_horizontalspacing.property_count != 0 ||
	    eqml_horizontalspacing.properties != NULL) {
		errno = EINVAL;
		perror("unexpected generated Row scene");
		return 1;
	}
	view = eqml_horizontalspacing;
	view.width = VIEW_WIDTH;
	view.height = VIEW_HEIGHT;
	if (eqml_render(&view, NULL, line, VIEW_WIDTH, consume_row, &crc) == -1) {
		perror("eqml_render");
		return 1;
	}
	crc.value ^= 0xffffffffU;
	if (printf("horizontal-spacing-ltr 320x240 crc32=%08lx\n",
	    (unsigned long)crc.value) < 0)
		return 1;
	return 0;
}
