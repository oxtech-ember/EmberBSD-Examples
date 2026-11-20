/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <sys/types.h>

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "eqml.h"
#include "eqml_wsdisplay.h"

extern const struct eqml_scene eqml_app;
static volatile sig_atomic_t quitting;

static void
stop(int signo)
{
	(void)signo;
	quitting = 1;
}

/* Accept a signed decimal temperature, one line per update. */
static int
update(char *line, int32_t *value)
{
	char *end;
	long long n;

	errno = 0;
	n = strtoll(line, &end, 10);
	if (errno || end == line || (*end != '\0' && strcmp(end, "\r") != 0) ||
	    n < INT32_MIN || n > INT32_MAX) {
		fputs("expected a signed 32-bit temperature\n", stderr);
		return -1;
	}
	*value = (int32_t)n;
	return 0;
}

static int
run(struct eqml_wsdisplay *display, int32_t *values, size_t index,
    uint16_t *row, int hold, const sigset_t *handled)
{
	struct pollfd in = { .fd = STDIN_FILENO, .events = POLLIN };
	char input[64], line[32];
	size_t used = 0;
	int discard = 0;

	if (eqml_wsdisplay_present(display, &eqml_app, values, row,
	    eqml_app.width) == -1)
		return -1;
	puts("presented");
	fflush(stdout);
	while (!quitting) {
		int ready = poll(&in, 1, 1000);
		if (ready == -1 && errno == EINTR)
			continue;
		if (ready == -1)
			return -1;
		if (ready == 0)
			continue;
		ssize_t count = read(in.fd, input, sizeof(input));
		if (count == -1 && errno == EINTR)
			continue;
		if (count == -1)
			return -1;
		if (count == 0) {
			if (!hold)
				break;
			/* Keep the displayed sample after the input stream closes. */
			sigset_t old;
			if (sigprocmask(SIG_BLOCK, handled, &old) == -1)
				return -1;
			while (!quitting)
				sigsuspend(&old);
			return sigprocmask(SIG_SETMASK, &old, NULL);
		}
		for (ssize_t i = 0; i < count; i++) {
			if (input[i] != '\n') {
				if (used < sizeof(line) - 1 && input[i] != '\0')
					line[used++] = input[i];
				else
					discard = 1;
				continue;
			}
			line[used] = '\0';
			if (discard)
				fputs("invalid temperature line\n", stderr);
			else if (update(line, &values[index]) == 0) {
				if (eqml_wsdisplay_present(display, &eqml_app,
				    values, row, eqml_app.width) == -1)
					return -1;
				puts("presented");
				fflush(stdout);
			}
			discard = 0;
			used = 0;
		}
	}
	return 0;
}

int
main(int argc, char **argv)
{
	struct eqml_wsdisplay display;
	struct sigaction sa = { .sa_handler = stop };
	int32_t *values;
	uint16_t *row;
	size_t i, temp = eqml_app.property_count;
	int result = 1;
	int hold = argc == 3 && !strcmp(argv[1], "--hold");

	if (argc != 2 && !hold) {
		fputs("usage: weather-qml [--hold] /dev/ttyE0\n", stderr);
		return 2;
	}
	for (i = 0; i < eqml_app.property_count; i++)
		if (!strcmp(eqml_app.properties[i].name, "temperatureC"))
			temp = i;
	if (temp == eqml_app.property_count) {
		fputs("scene requires the temperatureC integer property\n", stderr);
		return 1;
	}
	values = calloc(eqml_app.property_count, sizeof(*values));
	row = calloc(eqml_app.width, sizeof(*row));
	if (row == NULL || values == NULL) {
		perror("allocate scene buffers");
		goto done;
	}
	for (i = 0; i < eqml_app.property_count; i++)
		values[i] = eqml_app.properties[i].initial;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGINT);
	sigaddset(&sa.sa_mask, SIGTERM);
	sigaddset(&sa.sa_mask, SIGHUP);
	if (sigaction(SIGINT, &sa, NULL) == -1 ||
	    sigaction(SIGTERM, &sa, NULL) == -1 ||
	    sigaction(SIGHUP, &sa, NULL) == -1 ||
	    sigprocmask(SIG_UNBLOCK, &sa.sa_mask, NULL) == -1) {
		perror("sigaction");
		goto done;
	}
	if (eqml_wsdisplay_open(&display, argv[argc - 1], eqml_app.width,
	    eqml_app.height) == -1) {
		perror("open wsdisplay");
		goto done;
	}
	result = run(&display, values, temp, row, hold, &sa.sa_mask);
	if (result == -1)
		perror("render or read");
	if (eqml_wsdisplay_close(&display) == -1) {
		perror("restore wsdisplay");
		result = 1;
	}
done:
	free(row);
	free(values);
	return result == 0 ? 0 : 1;
}
