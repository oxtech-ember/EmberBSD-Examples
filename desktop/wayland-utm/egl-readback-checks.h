/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD contributors (AI-assisted). */
#ifndef EGL_READBACK_CHECKS_H
#define EGL_READBACK_CHECKS_H
#include <ctype.h>
#include <stddef.h>
#include <string.h>

#define SIDE 64
static const unsigned char readback_colors[4][4] = {
    {255, 0, 0, 255}, {0, 255, 0, 255},
    {0, 0, 255, 255}, {255, 255, 255, 255}
};

static int
readback_contains(const char *text, const char *word)
{
	size_t i;

	for (; *text != '\0'; text++) {
		for (i = 0; word[i] != '\0' && text[i] != '\0'; i++) {
			if (tolower((unsigned char)text[i]) !=
			    tolower((unsigned char)word[i]))
				break;
		}
		if (word[i] == '\0')
			return 1;
	}
	return 0;
}

static int
readback_zink_renderer(const char *renderer, const char *device)
{
	static const char *const software[] = {
		"llvmpipe", "softpipe", "lavapipe", "swiftshader", "software"
	};
	size_t i;

	if (renderer == NULL || device == NULL || *device == '\0' ||
	    strncmp(renderer, "zink ", 5) != 0 || strstr(renderer, device) == NULL)
		return 0;
	for (i = 0; i < sizeof(software) / sizeof(software[0]); i++) {
		if (readback_contains(renderer, software[i]))
			return 0;
	}
	return 1;
}

static int
readback_pixels(const unsigned char *pixels, unsigned frame, size_t *bad)
{
	size_t i;
	unsigned x, y, quadrant, expected, actual;

	for (i = 0; i < SIDE * SIDE * 4; i++) {
		x = (unsigned)(i / 4) % SIDE;
		y = (unsigned)(i / 4) / SIDE;
		quadrant = (y / (SIDE / 2)) * 2 + x / (SIDE / 2);
		expected = readback_colors[(quadrant + frame) % 4][i % 4];
		actual = pixels[i];
		if (actual + 1 < expected || expected + 1 < actual) {
			*bad = i;
			return 0;
		}
	}
	return 1;
}
#endif
