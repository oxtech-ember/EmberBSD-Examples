/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <signal.h>
#include <stdint.h>
#include <stdio.h>

#include "eqml.h"
#include "eqml_wsdisplay.h"

extern const struct eqml_scene eqml_horizontalspacing;

#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 480
#define VIEW_WIDTH 320
#define VIEW_HEIGHT 240

int
main(int argc, char **argv)
{
	struct eqml_wsdisplay display = { .fd = -1 };
	struct eqml_scene blank = {
		.width = DISPLAY_WIDTH,
		.height = DISPLAY_HEIGHT,
		.background = 0xffff
	};
	struct eqml_scene view;
	uint16_t line[DISPLAY_WIDTH];
	sigset_t signals;
	int signo, status = 1;
	const char *device = argc > 1 ? argv[1] : "/dev/ttyE0";

	if (argc > 2) {
		(void)fprintf(stderr, "usage: %s [wsdisplay-device]\n", argv[0]);
		return 2;
	}
	if (eqml_horizontalspacing.width != 640 ||
	    eqml_horizontalspacing.height != 480 ||
	    eqml_horizontalspacing.property_count != 0 ||
	    eqml_horizontalspacing.properties != NULL) {
		(void)fprintf(stderr, "unexpected generated Row scene\n");
		return 1;
	}
	if (sigemptyset(&signals) == -1 || sigaddset(&signals, SIGINT) == -1 ||
	    sigaddset(&signals, SIGTERM) == -1 || sigaddset(&signals, SIGHUP) == -1 ||
	    sigprocmask(SIG_BLOCK, &signals, NULL) == -1) {
		perror("sigprocmask");
		return 1;
	}
	if (eqml_wsdisplay_open(&display, device, DISPLAY_WIDTH, DISPLAY_HEIGHT) == -1) {
		perror("eqml_wsdisplay_open");
		return 1;
	}
	if (eqml_wsdisplay_present(&display, &blank, NULL, line,
	    DISPLAY_WIDTH) == -1) {
		perror("clear display");
		goto out;
	}
	view = eqml_horizontalspacing;
	view.width = VIEW_WIDTH;
	view.height = VIEW_HEIGHT;
	if (eqml_wsdisplay_present_at(&display, &view, NULL,
	    (DISPLAY_WIDTH - VIEW_WIDTH) / 2,
	    (DISPLAY_HEIGHT - VIEW_HEIGHT) / 2, line, DISPLAY_WIDTH) == -1) {
		perror("present Row scene");
		goto out;
	}
	if (printf("presented horizontal-spacing.qml, LTR, centered 320x240\n") < 0 ||
	    fflush(stdout) == EOF) {
		perror("write presentation status");
		goto out;
	}
	if (sigwait(&signals, &signo) != 0) {
		perror("sigwait");
		goto out;
	}
	status = 0;

out:
	if (eqml_wsdisplay_close(&display) == -1) {
		perror("eqml_wsdisplay_close");
		return 1;
	}
	return status;
}
