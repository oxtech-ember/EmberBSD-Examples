/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "eqml_input.h"
#include "eqml_wsdisplay.h"
#include "eqml_wsmouse.h"
#include "networklab.h"
#include "sample.h"

enum probe { IDLE, TIME_QUERY, DNS_QUERY };
enum { TIME_PERIOD = 300000, DNS_PERIOD = 30000, DNS_COOLDOWN = 2000,
    QUERY_TIMEOUT = 8000, SCALE_WIDTH = 295 };

struct lab {
	struct eqml_ui *ui;
	struct eqml_wsdisplay display;
	struct eqml_wsmouse mouse;
	uint16_t *line;
	const char *lookup_host, *time_host, *service;
	struct net_sample clock;
	int64_t displayed_second;
	uint64_t dns_due, clock_due, next_manual, deadline;
	int reply_fd;
	pid_t child;
	enum probe running;
	int refresh;
};

static volatile sig_atomic_t interrupted;

static void
on_signal(int sig)
{
	(void)sig;
	interrupted = 1;
}

static uint64_t
monotonic_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
		perror("CLOCK_MONOTONIC");
		exit(1);
	}
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void
qml_log(void *context, const char *message)
{
	(void)context;
	printf("qml %s\n", message);
}

static void
qml_signal(void *context, const char *name)
{
	struct lab *lab = context;
	if (strcmp(name, "refreshRequested") == 0) {
		lab->refresh = 1;
		puts("DNS refresh requested");
	}
}

static int
present(void *context, const struct eqml_scene *scene,
    const struct eqml_region *region)
{
	struct lab *lab = context;
	return eqml_wsdisplay_present_region_at(&lab->display, scene, NULL, 0, 0,
	    region, lab->line, scene->width);
}

/* Keep blocking name service outside the display loop. Only one child is
 * active. Its single result fits in an atomic pipe write. */
static int
start_query(struct lab *lab, enum probe kind, uint64_t now)
{
	int fd[2];
	pid_t pid;

	if (pipe(fd) == -1)
		return -1;
	pid = fork();
	if (pid == -1) {
		int saved = errno;
		close(fd[0]);
		close(fd[1]);
		errno = saved;
		return -1;
	}
	if (pid == 0) {
		struct net_sample result;
		ssize_t written;
		close(fd[0]);
		close(lab->display.fd);
		close(lab->mouse.fd);
		if (kind == DNS_QUERY)
			sample_dns(lab->lookup_host, &result);
		else
			sample_clock(lab->time_host, lab->service, &result);
		do {
			written = write(fd[1], &result, sizeof(result));
		} while (written == -1 && errno == EINTR);
		_exit(written == (ssize_t)sizeof(result) ? 0 : 1);
	}
	lab->running = kind;
	lab->child = pid;
	lab->reply_fd = fd[0];
	close(fd[1]);
	lab->deadline = now + QUERY_TIMEOUT;
	if (kind == DNS_QUERY) {
		lab->dns_due = now + DNS_PERIOD;
		lab->next_manual = now + DNS_COOLDOWN;
		return eqml_ui_set(lab->ui, eqml_networklab_text_dnsStatus, "RESOLVING");
	}
	lab->clock_due = now + TIME_PERIOD;
	return eqml_ui_set(lab->ui, eqml_networklab_text_clockStatus, "CONTACTING SERVER");
}

static void
finish_child(struct lab *lab, int cancel)
{
	if (lab->running == IDLE)
		return;
	if (cancel)
		(void)kill(lab->child, SIGKILL);
	while (waitpid(lab->child, NULL, 0) == -1 && errno == EINTR)
		continue;
	close(lab->reply_fd);
	lab->reply_fd = -1;
	lab->running = IDLE;
}

static int
receive_query(struct lab *lab, int timed_out)
{
	struct net_sample sample = {0};
	const enum probe kind = lab->running;
	char text[32];
	const char *status;
	ssize_t count = 0;
	int32_t x;

	if (!timed_out) {
		do {
			count = read(lab->reply_fd, &sample, sizeof(sample));
		} while (count == -1 && errno == EINTR);
	}
	finish_child(lab, timed_out);
	if (count != (ssize_t)sizeof(sample))
		memset(&sample, 0, sizeof(sample));
	if (kind == TIME_QUERY) {
		if (sample.clock_ok) {
			lab->clock = sample;
			status = "NETWORK SYNCED";
			printf("SNTP accepted, RTT %" PRIu32 " ms\n", sample.ntp_ms);
		} else {
			status = lab->clock.clock_ok ? "HOLDOVER / RETRY" : "NO TIME REPLY";
			puts("SNTP unavailable");
		}
		return eqml_ui_set(lab->ui, eqml_networklab_text_clockStatus, status);
	}
	if (!sample.dns_ok) {
		puts("DNS lookup failed");
		return eqml_ui_set(lab->ui, eqml_networklab_text_dnsStatus, "NO ANSWER / RETRY");
	}
	snprintf(text, sizeof(text), "%" PRIu32 " ms", sample.dns_ms);
	x = (int32_t)(((sample.dns_ms < 500 ? sample.dns_ms : 500) * SCALE_WIDTH + 250) / 500);
	printf("DNS IPv4 answer, lookup %" PRIu32 " ms\n", sample.dns_ms);
	if (eqml_ui_set(lab->ui, eqml_networklab_text_dnsAddress, sample.address) == -1 ||
	    eqml_ui_set(lab->ui, eqml_networklab_text_dnsLatency, text) == -1 ||
	    eqml_ui_set_coordinate(lab->ui, eqml_networklab_node_pulseMarker, EQML_AXIS_X,
	    x, eqml_networklab_duration_x_pulseMarker) == -1 ||
	    eqml_ui_set_coordinate(lab->ui, eqml_networklab_node_pulseTrail, EQML_AXIS_X,
	    x, eqml_networklab_duration_x_pulseTrail) == -1)
		return -1;
	return eqml_ui_set(lab->ui, eqml_networklab_text_dnsStatus, "ANSWER RECEIVED");
}

static int
update_clock(struct lab *lab, uint64_t now, uint64_t *next)
{
	const struct net_sample *clock = &lab->clock;
	uint64_t elapsed;
	time_t second;
	struct tm tm;
	char text[16];

	if (!clock->clock_ok || !eqml_ui_visible(lab->ui, eqml_networklab_node_clockPage))
		return 0;
	elapsed = (now >= clock->received_ms ? now - clock->received_ms : 0) +
	    clock->fraction_ms;
	second = (time_t)(clock->utc_seconds + (int64_t)(elapsed / 1000));
	*next = now + 1000 - elapsed % 1000;
	if (lab->displayed_second == (int64_t)second)
		return 0;
	lab->displayed_second = second;
	if (gmtime_r(&second, &tm) == NULL || !strftime(text, sizeof(text), "%H:%M:%S", &tm) ||
	    eqml_ui_set(lab->ui, eqml_networklab_text_utcTime, text) == -1 ||
	    !strftime(text, sizeof(text), "%Y-%m-%d", &tm) ||
	    eqml_ui_set(lab->ui, eqml_networklab_text_utcDate, text) == -1)
		return -1;
	return eqml_ui_set_coordinate(lab->ui, eqml_networklab_node_secondMarker,
	    EQML_AXIS_X, tm.tm_sec * 5,
	    tm.tm_sec == 0 ? 0 : eqml_networklab_duration_x_secondMarker);
}

static int
run(struct lab *lab)
{
	struct eqml_wsmouse_event events[16];
	char label[27];

	snprintf(label, sizeof(label), "%.26s", lab->lookup_host);
	if (strlen(lab->lookup_host) >= sizeof(label))
		memcpy(label + sizeof(label) - 4, "...", 4);
	if (eqml_ui_set(lab->ui, eqml_networklab_text_dnsHost, label) == -1 ||
	    eqml_ui_set(lab->ui, eqml_networklab_text_utcTime, "--:--:--") == -1 ||
	    eqml_ui_set(lab->ui, eqml_networklab_text_dnsAddress, "--") == -1 ||
	    eqml_ui_set(lab->ui, eqml_networklab_text_dnsLatency, "-- ms") == -1 ||
	    eqml_ui_set(lab->ui, eqml_networklab_text_dnsStatus, "WAITING") == -1)
		return -1;
	while (!interrupted) {
		uint64_t now = monotonic_ms(), next_second = UINT64_MAX, wake;
		struct pollfd fds[2];
		int timeout, delay, ready, count;
		if (eqml_ui_tick(lab->ui, now) == -1)
			return -1;
		if (lab->refresh) {
			lab->refresh = 0;
			if (lab->running != DNS_QUERY)
				lab->dns_due = now < lab->next_manual ? lab->next_manual : now;
		}
		if (lab->running == IDLE) {
			enum probe kind = IDLE;
			if (now >= lab->clock_due)
				kind = TIME_QUERY;
			else if (now >= lab->dns_due)
				kind = DNS_QUERY;
			if (kind != IDLE && start_query(lab, kind, now) == -1)
				return -1;
		}
		if (update_clock(lab, now, &next_second) == -1 ||
		    eqml_ui_present(lab->ui, present, lab) == -1)
			return -1;
		wake = lab->running == IDLE ?
		    (lab->dns_due < lab->clock_due ? lab->dns_due : lab->clock_due) : lab->deadline;
		if (next_second < wake)
			wake = next_second;
		now = monotonic_ms();
		timeout = wake > now ? (int)(wake - now) : 0;
		delay = eqml_ui_timeout(lab->ui, now);
		if (delay >= 0 && delay < timeout)
			timeout = delay;
		fds[0] = (struct pollfd){ .fd = lab->mouse.fd, .events = POLLIN };
		fds[1] = (struct pollfd){ .fd = lab->reply_fd, .events = POLLIN };
		ready = poll(fds, 2, timeout);
		if (ready == -1) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			errno = EIO;
			return -1;
		}
		if (fds[0].revents & POLLIN) {
			count = eqml_wsmouse_read(&lab->mouse, events, sizeof(events)/sizeof(events[0]));
			if (count == -1)
				return -1;
			for (int i = 0; i < count; i++) {
				if (eqml_ui_pointer(lab->ui, events[i].phase, events[i].x, events[i].y,
				    events[i].button, events[i].modifiers, events[i].now_ms) == -1)
					return -1;
			}
		}
		if (lab->running != IDLE && (fds[1].revents || monotonic_ms() >= lab->deadline)) {
			if (receive_query(lab, fds[1].revents == 0) == -1)
				return -1;
		}
	}
	return 0;
}

int
main(int argc, char **argv)
{
	struct lab lab = { .display.fd = -1, .reply_fd = -1, .displayed_second = -1,
	    .lookup_host = "example.org", .time_host = "time.google.com", .service = "123" };
	struct sigaction sa = { .sa_handler = on_signal };
	int option, result = 1;
	while ((option = getopt(argc, argv, "n:s:p:")) != -1) {
		switch (option) {
		case 'n': lab.lookup_host = optarg; break;
		case 's': lab.time_host = optarg; break;
		case 'p': lab.service = optarg; break;
		default: goto usage;
		}
	}
	if (argc != optind || !*lab.lookup_host || !*lab.time_host || !*lab.service)
		goto usage;
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (sigemptyset(&sa.sa_mask) == -1 || sigaction(SIGTERM, &sa, NULL) == -1 ||
	    sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGHUP, &sa, NULL) == -1) {
		perror("signals");
		return 1;
	}
	eqml_wsmouse_init(&lab.mouse);
	lab.line = calloc(eqml_networklab.width, sizeof(*lab.line));
	lab.ui = eqml_ui_create(&eqml_networklab, qml_log, NULL);
	eqml_ui_set_signal_handler(lab.ui, qml_signal, &lab);
	if (lab.line == NULL || lab.ui == NULL ||
	    eqml_wsdisplay_open(&lab.display, "/dev/ttyE0", eqml_networklab.width,
	    eqml_networklab.height) == -1 || eqml_wsmouse_open(&lab.mouse, "/dev/wsmouse0") == -1 ||
	    run(&lab) == -1)
		perror("network-lab");
	else
		result = 0;
	finish_child(&lab, 1);
	if (eqml_wsmouse_close(&lab.mouse) == -1)
		result = 1;
	if (eqml_wsdisplay_close(&lab.display) == -1)
		result = 1;
	eqml_ui_destroy(lab.ui);
	free(lab.line);
	return result;
usage:
	fprintf(stderr, "usage: network-lab [-n lookup-host] [-s time-server] [-p udp-port]\n");
	return 2;
}
