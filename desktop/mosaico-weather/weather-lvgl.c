/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/types.h>

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <lvgl/lvgl.h>

#include "weather-display.h"

#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 480
#define DRAW_LINES 16

static struct weather_display display;
static int flush_error;
static unsigned int flush_count;

static void
flush(lv_display_t *lv_display, const lv_area_t *area, uint8_t *pixels)
{
	unsigned int width, height;

	width = (unsigned int)(area->x2 - area->x1 + 1);
	height = (unsigned int)(area->y2 - area->y1 + 1);
	if (weather_display_blit_rgb565(&display, (unsigned int)area->x1,
	    (unsigned int)area->y1, width, height, (const uint16_t *)pixels,
	    (size_t)width * sizeof(uint16_t)) == -1) {
		if (flush_error == 0)
			flush_error = errno == 0 ? EIO : errno;
	} else {
		flush_count++;
	}
	lv_display_flush_ready(lv_display);
}

int
main(int argc, char **argv)
{
	sigset_t wait_signals, old_signals;
	lv_display_t *lv_display;
	lv_obj_t *title, *temperature, *condition;
	uint16_t *draw_buffer;
	int signo, error;
	int result = 1;

	if (argc != 2) {
		fprintf(stderr, "usage: %s wsdisplay\n", argv[0]);
		return 2;
	}
	sigemptyset(&wait_signals);
	if (sigaddset(&wait_signals, SIGINT) == -1 ||
	    sigaddset(&wait_signals, SIGTERM) == -1 ||
	    sigaddset(&wait_signals, SIGHUP) == -1 ||
	    sigprocmask(SIG_BLOCK, &wait_signals, &old_signals) == -1) {
		perror("block termination signals");
		return 1;
	}
	if (weather_display_open(&display, argv[1]) == -1) {
		perror("wsdisplay");
		return 1;
	}
	if (display.width != SCREEN_WIDTH || display.height != SCREEN_HEIGHT) {
		fprintf(stderr, "weather screen requires 480x480 RGB565\n");
		goto out;
	}
	draw_buffer = malloc((size_t)SCREEN_WIDTH * DRAW_LINES *
	    sizeof(*draw_buffer));
	if (draw_buffer == NULL) {
		perror("draw buffer");
		goto out;
	}
	lv_init();
	lv_display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
	if (lv_display == NULL) {
		fprintf(stderr, "cannot create LVGL display\n");
		goto out_lvgl;
	}
	lv_display_set_color_format(lv_display, LV_COLOR_FORMAT_RGB565);
	lv_display_set_buffers(lv_display, draw_buffer, NULL,
	    (size_t)SCREEN_WIDTH * DRAW_LINES * sizeof(*draw_buffer),
	    LV_DISPLAY_RENDER_MODE_PARTIAL);
	lv_display_set_flush_cb(lv_display, flush);

	lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x122230), 0);
	title = lv_label_create(lv_screen_active());
	temperature = lv_label_create(lv_screen_active());
	condition = lv_label_create(lv_screen_active());
	if (title == NULL || temperature == NULL || condition == NULL) {
		fprintf(stderr, "cannot create weather labels\n");
		goto out_lvgl;
	}
	lv_label_set_text(title, "MOSAICO");
	lv_obj_set_style_text_color(title, lv_color_hex(0xd6e3ea), 0);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 88);
	lv_label_set_text(temperature, "21 C");
	lv_obj_set_style_text_color(temperature, lv_color_hex(0xffffff), 0);
	lv_obj_center(temperature);
	lv_label_set_text(condition, "SUNNY");
	lv_obj_set_style_text_color(condition, lv_color_hex(0xffcb69), 0);
	lv_obj_align(condition, LV_ALIGN_TOP_MID, 0, 334);

	/* Render synchronously once; the fixed scene has no periodic work. */
	lv_refr_now(lv_display);
	if (flush_error != 0) {
		errno = flush_error;
		perror("weather framebuffer flush");
		goto out_lvgl;
	}
	if (flush_count == 0) {
		fprintf(stderr, "LVGL produced no framebuffer flush\n");
		goto out_lvgl;
	}
	error = sigwait(&wait_signals, &signo);
	if (error != 0) {
		errno = error;
		perror("wait for termination signal");
		goto out_lvgl;
	}
	result = 0;
out_lvgl:
	lv_deinit();
	free(draw_buffer);
out:
	if (weather_display_close(&display) == -1) {
		perror("restore wsdisplay");
		result = 1;
	}
	if (sigprocmask(SIG_SETMASK, &old_signals, NULL) == -1) {
		perror("restore signal mask");
		result = 1;
	}
	return result;
}
