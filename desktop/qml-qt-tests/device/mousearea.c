/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/types.h>

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "eqml_input.h"
#include "eqml_wsdisplay.h"
#include "eqml_wsmouse.h"

extern const struct eqml_scene eqml_mousearea;

#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 480
static volatile sig_atomic_t stop;

static void
on_signal(int signo)
{
	(void)signo;
	stop = 1;
}

static int
monotonic_ms(uint64_t *result)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
		return -1;
	if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L) {
		errno = EIO;
		return -1;
	}
	*result = (uint64_t)ts.tv_sec * 1000U +
	    (uint64_t)ts.tv_nsec / 1000000U;
	return 0;
}

static void
qml_log(void *cookie, const char *message)
{
	(void)cookie;
	(void)fprintf(stdout, "qml: %s\n", message);
	(void)fflush(stdout);
}

static int
present(struct eqml_ui *ui, struct eqml_wsdisplay *display,
	uint16_t x, uint16_t y, uint16_t line[DISPLAY_WIDTH])
{
	const struct eqml_scene *frame = eqml_ui_frame(ui);

	if (frame == NULL)
		return -1;
	if (frame->width != 320 || frame->height != DISPLAY_HEIGHT ||
	    x > display->width || frame->width > display->width - x ||
	    y > display->height || frame->height > display->height - y) {
		errno = ENOTSUP;
		return -1;
	}
	return eqml_wsdisplay_present_at(display, frame, NULL, x, y, line,
	    DISPLAY_WIDTH);
}

static int
clear_display(struct eqml_wsdisplay *display, uint16_t color,
	uint16_t line[DISPLAY_WIDTH])
{
	struct eqml_scene blank = {
		.width = DISPLAY_WIDTH,
		.height = DISPLAY_HEIGHT,
		.background = color
	};

	return eqml_wsdisplay_present(display, &blank, NULL, line,
	    DISPLAY_WIDTH);
}

static void
log_blue_position(struct eqml_ui *ui)
{
	const struct eqml_scene *frame = eqml_ui_frame(ui);
	size_t i;

	if (frame == NULL)
		return;
	for (i = 0; i < frame->item_count; i++) {
		if (frame->items[i].kind == EQML_RECT &&
		    frame->items[i].data.rect.color == 0x001f) {
			(void)fprintf(stdout, "blue position x=%ld y=%ld\n",
			    (long)frame->items[i].data.rect.x,
			    (long)frame->items[i].data.rect.y);
			(void)fflush(stdout);
			return;
		}
	}
}

static const char *
phase_name(enum eqml_phase phase)
{
	switch (phase) {
	case EQML_DOWN: return "down";
	case EQML_MOVE: return "move";
	case EQML_UP: return "up";
	case EQML_CANCEL: return "cancel";
	}
	return "invalid";
}

int
main(int argc, char **argv)
{
	struct eqml_wsdisplay display = { .fd = -1 };
	struct eqml_wsmouse mouse;
	struct eqml_ui *ui = NULL;
	struct sigaction sa;
	struct eqml_wsmouse_event events[16];
	uint16_t line[DISPLAY_WIDTH];
	struct pollfd pfd;
	uint64_t now;
	int timeout, status = 1, ready, count, changed, result;
	uint16_t viewport_x, viewport_y;
	const char *display_path = argc > 1 ? argv[1] : "/dev/ttyE0";
	const char *mouse_path = argc > 2 ? argv[2] : "/dev/wsmouse0";

	if (argc > 3) {
		(void)fprintf(stderr, "usage: %s [display [mouse]]\n", argv[0]);
		return 2;
	}
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	if (sigemptyset(&sa.sa_mask) == -1 ||
	    sigaction(SIGINT, &sa, NULL) == -1 ||
	    sigaction(SIGTERM, &sa, NULL) == -1 ||
	    sigaction(SIGHUP, &sa, NULL) == -1) {
		perror("signal setup");
		return 1;
	}
	eqml_wsmouse_init(&mouse);
	if (eqml_wsdisplay_open(&display, display_path,
	    DISPLAY_WIDTH, DISPLAY_HEIGHT) == -1) {
		perror("open display");
		goto out;
	}
	if (eqml_wsmouse_open(&mouse, mouse_path) == -1) {
		perror("open wsmouse");
		goto out;
	}
	ui = eqml_ui_create(&eqml_mousearea, qml_log, NULL);
	if (ui == NULL) {
		perror("create UI");
		goto out;
	}
	viewport_x = (uint16_t)((display.width - eqml_mousearea.width) / 2);
	viewport_y = (uint16_t)((display.height - eqml_mousearea.height) / 2);
	if (clear_display(&display, eqml_mousearea.background, line) == -1 ||
	    present(ui, &display, viewport_x, viewport_y, line) == -1) {
		perror("initial render");
		goto out;
	}
	(void)fprintf(stdout, "presented mousearea 320x480 at x=%u y=%u\n",
	    viewport_x, viewport_y);
	(void)fflush(stdout);
	pfd.fd = mouse.fd;
	pfd.events = POLLIN;
	while (!stop) {
		if (monotonic_ms(&now) == -1) {
			perror("clock_gettime");
			goto out;
		}
		timeout = eqml_ui_timeout(ui, now);
		pfd.revents = 0;
		ready = poll(&pfd, 1, timeout);
		if (ready == -1) {
			if (errno == EINTR)
				continue;
			perror("poll wsmouse");
			goto out;
		}
		changed = 0;
		if (ready > 0 && (pfd.revents & POLLIN)) {
			do {
				count = eqml_wsmouse_read(&mouse, events,
				    sizeof(events) / sizeof(events[0]));
				if (count == -1) {
					perror("read wsmouse");
					goto out;
				}
				for (int i = 0; i < count; i++) {
					if (events[i].phase != EQML_MOVE) {
						(void)fprintf(stdout,
						    "pointer %s x=%ld y=%ld button=0x%08lx\n",
						    phase_name(events[i].phase),
						    (long)events[i].x, (long)events[i].y,
						    (unsigned long)events[i].button);
						(void)fflush(stdout);
					}
					{
					int64_t local_x = (int64_t)events[i].x - viewport_x;
					int64_t local_y = (int64_t)events[i].y - viewport_y;
					int32_t x = local_x < INT32_MIN ? INT32_MIN :
					    local_x > INT32_MAX ? INT32_MAX : (int32_t)local_x;
					int32_t y = local_y < INT32_MIN ? INT32_MIN :
					    local_y > INT32_MAX ? INT32_MAX : (int32_t)local_y;
					result = eqml_ui_pointer(ui, events[i].phase,
					    x, y, events[i].button,
					    events[i].modifiers, events[i].now_ms);
					if (result == -1) {
						perror("dispatch pointer");
						goto out;
					}
					changed |= result > 0;
					if (events[i].phase == EQML_UP)
						log_blue_position(ui);
					}
				}
			} while (count == (int)(sizeof(events) / sizeof(events[0])) &&
			    !stop);
		} else if (ready > 0 &&
		    (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
			errno = EIO;
			perror("wsmouse poll status");
			goto out;
		}
		if (monotonic_ms(&now) == -1 ||
		    (result = eqml_ui_tick(ui, now)) == -1) {
			perror("UI timer");
			goto out;
		}
		changed |= result > 0;
		if (changed && present(ui, &display, viewport_x, viewport_y,
		    line) == -1) {
			perror("render");
			goto out;
		}
	}
	status = 0;
out:
	eqml_ui_destroy(ui);
	if (eqml_wsmouse_close(&mouse) == -1) {
		perror("close wsmouse");
		status = 1;
	}
	if (eqml_wsdisplay_close(&display) == -1) {
		perror("restore display");
		status = 1;
	}
	return status;
}
