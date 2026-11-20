/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef WEATHER_DISPLAY_H
#define WEATHER_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

struct weather_display {
	int fd;
	uint8_t *fb;
	size_t map_size;
	size_t stride;
	unsigned int width;
	unsigned int height;
	unsigned int old_mode;
	int mode_changed;
};

int weather_display_open(struct weather_display *, const char *);
int weather_display_blit_rgb565(const struct weather_display *, unsigned int,
	unsigned int, unsigned int, unsigned int, const uint16_t *, size_t);
int weather_display_close(struct weather_display *);

#endif /* WEATHER_DISPLAY_H */
