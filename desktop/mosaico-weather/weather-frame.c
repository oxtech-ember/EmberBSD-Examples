/* SPDX-License-Identifier: BSD-2-Clause */
/* Present a Qt Quick frame rendered on the build host. */
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>

#include "weather-display.h"

static const struct {
	uint16_t length;
	uint16_t color;
} runs[] = {
#include "weather-frame.inc"
};

static int
draw_frame(const struct weather_display *display)
{
	uint16_t row[480];
	unsigned int x = 0, y = 0;
	size_t i;

	if (display->width != 480 || display->height != 480) {
		errno = ENOTSUP;
		return -1;
	}
	for (i = 0; i < sizeof(runs) / sizeof(runs[0]); i++) {
		if (runs[i].length == 0 || runs[i].length > 480 - x)
			break;
		for (unsigned int n = 0; n < runs[i].length; n++)
			row[x++] = runs[i].color;
		if (x == 480) {
			if (weather_display_blit_rgb565(display, 0, y, 480, 1,
			    row, sizeof(row)) == -1)
				return -1;
			x = 0;
			y++;
		}
	}
	if (i == sizeof(runs) / sizeof(runs[0]) && x == 0 && y == 480)
		return 0;
	errno = EINVAL;
	return -1;
}

int
main(int argc, char **argv)
{
	struct weather_display display;
	sigset_t signals;
	int error, signo, status = 0;

	if (argc != 2) {
		fprintf(stderr, "usage: %s wsdisplay\n", argv[0]);
		return 2;
	}
	sigemptyset(&signals);
	sigaddset(&signals, SIGINT);
	sigaddset(&signals, SIGTERM);
	sigaddset(&signals, SIGHUP);
	if (sigprocmask(SIG_BLOCK, &signals, NULL) == -1 ||
	    weather_display_open(&display, argv[1]) == -1) {
		perror("weather-frame");
		return 1;
	}
	if (draw_frame(&display) == -1) {
		perror("draw frame");
		status = 1;
	} else if ((error = sigwait(&signals, &signo)) != 0) {
		errno = error;
		perror("sigwait");
		status = 1;
	}
	if (weather_display_close(&display) == -1) {
		perror("restore display");
		status = 1;
	}
	return status;
}
