/* SPDX-License-Identifier: BSD-2-Clause */
#include "native_adc.h"
#include "native_usb.h"
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Lock order: command[p] -> adc -> admission[p] -> state.
 * State protects only short ownership/admission updates. No blocking device
 * operation holds it. EP0 closes kernel gates before waiting for admission
 * or command locks, and never holds admission while waiting for a command.
 */
struct server {
	struct rx_adc adc;
	struct rx_service service;
	int usb, quitting;
	unsigned mask;
	uint64_t generation, epoch[2], command_epoch[2], command_generation[2];
	pthread_mutex_t adc_lock, command_lock[2], admission[2], state_lock;
	pthread_cond_t changed;
};
#define SERVER_INITIALIZER { .usb = -1, .adc.fd = -1, \
	.adc_lock = PTHREAD_MUTEX_INITIALIZER, .state_lock = PTHREAD_MUTEX_INITIALIZER, \
	.command_lock = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER }, \
	.admission = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER }, \
	.changed = PTHREAD_COND_INITIALIZER }
struct worker { struct server *server; unsigned pipe; };
static _Thread_local struct worker *current_worker;
static volatile sig_atomic_t interrupted;
static void interrupt(int signo) { (void)signo; interrupted = 1; }
static void state_lock(void *v) { pthread_mutex_lock(&((struct server *)v)->state_lock); }
static void state_unlock(void *v) { pthread_mutex_unlock(&((struct server *)v)->state_lock); }

static int
valid(struct server *s, unsigned p, uint64_t generation, uint64_t epoch)
{
	int result;
	pthread_mutex_lock(&s->state_lock);
	result = !s->quitting && (s->mask & (1U << p)) &&
					s->generation == generation && s->epoch[p] == epoch;
	pthread_mutex_unlock(&s->state_lock);
	return result;
}

static int
transfer(struct server *s, unsigned p, unsigned ep, const void *b, size_t n,
				uint64_t generation, uint64_t epoch)
{
	struct usbperiph_xfer x = { .buffer = (uint64_t)(uintptr_t)b,
					.generation = generation, .endpoint = ep, .length = (uint32_t)n, .timeout_ms = 1000 };
	int result;
	pthread_mutex_lock(&s->admission[p]);
	if (!valid(s, p, generation, epoch)) result = -ECANCELED;
	else if (ioctl(s->usb, USBPERIPH_IOC_XFER, &x) < 0) result = -errno;
	else if (x.actual > n || x.generation != generation) result = -ESTALE;
	else result = (int)x.actual;
	pthread_mutex_unlock(&s->admission[p]);
	return result;
}

static int
send_bytes(void *v, unsigned p, const void *data, size_t n)
{
	struct server *s = v;
	const unsigned char *b = data;
	while (n) {
		size_t want = n > USBPERIPH_MAX_XFER ? USBPERIPH_MAX_XFER : n;
		int got = transfer(s, p, 0x81 + p, b, want,
						s->command_generation[p], s->command_epoch[p]);
		if (got <= 0) return got < 0 ? got : -EPIPE;
		b += got; n -= (size_t)got;
	}
	return 0;
}

/* A control command waiting behind RX must also be cancellable by its EP0
 * close, without stopping the other pipe's receiver transaction. */
static int
adc_enter(struct server *s)
{
	for (;;) {
		if (current_worker && !valid(s, current_worker->pipe,
						s->command_generation[current_worker->pipe], s->command_epoch[current_worker->pipe]))
			return -ECANCELED;
		int e = pthread_mutex_trylock(&s->adc_lock);
		if (!e) return 0;
		if (e != EBUSY) return -e;
		struct timespec pause = { .tv_nsec = 1000000 };
		nanosleep(&pause, NULL);
	}
}

static int
attribute(void *v, const char *d, const char *c, const char *a, char *b, size_t n, int w)
{
	struct server *s = v; int e = adc_enter(s);
	if (e) return e;
	e = rx_adc_attr(&s->adc, d, c, a, b, n, w);
	pthread_mutex_unlock(&s->adc_lock); return e;
}
static int
start(void *v, unsigned n)
{
	struct server *s = v; int e = adc_enter(s);
	if (e) return e;
	e = rx_adc_start(&s->adc, n); pthread_mutex_unlock(&s->adc_lock); return e;
}
static int
stop(void *v)
{
	struct server *s = v;
	pthread_mutex_lock(&s->adc_lock);
	int e = rx_adc_stop(&s->adc);
	pthread_mutex_unlock(&s->adc_lock); return e;
}
static ssize_t input(void *v, void *b, size_t n) { return rx_adc_read(&((struct server *)v)->adc, b, n); }
static int
settings(void *v, unsigned timeout, unsigned blocks)
{
	struct server *s = v; int e = adc_enter(s);
	if (e) return e;
	e = rx_adc_settings(&s->adc, timeout, blocks); pthread_mutex_unlock(&s->adc_lock); return e;
}
static int begin_read(void *v) { return adc_enter(v); }
static void end_read(void *v) { pthread_mutex_unlock(&((struct server *)v)->adc_lock); }

static int
endpoint_gate(const struct server *s, unsigned mask, int enable, uint64_t generation)
{
	for (unsigned p = 0; p < 2; p++) if (mask & (1U << p)) {
		for (unsigned in = 0; in < 2; in++) {
			struct usbperiph_endpoint_gate g = { .generation = generation,
							.endpoint = p + 1 + (in ? 0x80 : 0), .enable = (uint32_t)enable };
			if (ioctl(s->usb, USBPERIPH_IOC_ENDPOINT_GATE, &g) < 0) return -errno;
		}
	}
	return 0;
}

static void
cancel_endpoints(const struct server *s, unsigned mask)
{
	for (uint32_t ep = 1; ep <= 2; ep++) if (mask & (1U << (ep - 1))) {
		uint32_t in = ep | 0x80;
		(void)ioctl(s->usb, USBPERIPH_IOC_CANCEL, &ep);
		(void)ioctl(s->usb, USBPERIPH_IOC_CANCEL, &in);
	}
}

static int
close_pipes(struct server *s, unsigned mask, uint64_t generation, int use_gate)
{
	int owner_closed, error = 0;
	pthread_mutex_lock(&s->state_lock);
	owner_closed = s->service.active && (mask & (1U << s->service.owner));
	s->mask &= ~mask; s->generation = generation; s->service.closing |= mask;
	for (unsigned p = 0; p < 2; p++) if (mask & (1U << p)) s->epoch[p]++;
	pthread_mutex_unlock(&s->state_lock);
	if (use_gate) {
		error = endpoint_gate(s, mask, 0, generation);
		if (error) cancel_endpoints(s, mask); /* Clean up, but never ACK this failure. */
	} else cancel_endpoints(s, mask); /* Bus reset already invalidates generation. */
	if (owner_closed && ioctl(s->adc.fd, ADI_AXI_ADCIOC_STREAM_STOP, (void *)NULL) < 0)
		error = -errno;
	for (unsigned p = 0; p < 2; p++) if (mask & (1U << p)) {
		pthread_mutex_lock(&s->admission[p]); pthread_mutex_unlock(&s->admission[p]);
		pthread_mutex_lock(&s->command_lock[p]);
		int e = rx_close_pipe(&s->service, p);
		if (!error) error = e;
		pthread_mutex_unlock(&s->command_lock[p]);
	}
	/* An already admitted START can finish after the first raw STOP. New OPEN
		* reservations remain disabled until this final native stop completes. */
	if (owner_closed) { int e = stop(s); if (!error) error = e; }
	pthread_mutex_lock(&s->state_lock); s->service.closing &= ~mask; pthread_mutex_unlock(&s->state_lock);
	return error;
}

static void *
work(void *v)
{
	struct worker *w = v; struct server *s = w->server;
	unsigned char data[512] = {0};
	current_worker = w;
	for (;;) {
		uint64_t generation, epoch;
		pthread_mutex_lock(&s->state_lock);
		while (!(s->mask & (1U << w->pipe)) && !s->quitting)
			pthread_cond_wait(&s->changed, &s->state_lock);
		if (s->quitting) { pthread_mutex_unlock(&s->state_lock); current_worker = NULL; return NULL; }
		generation = s->generation; epoch = s->epoch[w->pipe];
		pthread_mutex_unlock(&s->state_lock);
		int n = transfer(s, w->pipe, w->pipe + 1, data, sizeof(data), generation, epoch);
		pthread_mutex_lock(&s->command_lock[w->pipe]);
		if (valid(s, w->pipe, generation, epoch)) {
			s->command_generation[w->pipe] = generation; s->command_epoch[w->pipe] = epoch;
			int e = n > 0 ? rx_feed(&s->service, w->pipe, data, (size_t)n) : 0;
			if (e || (n < 0 && n != -ETIMEDOUT && n != -EINTR && n != -ECANCELED && n != -ESTALE)) {
				pthread_mutex_lock(&s->state_lock); s->mask &= ~(1U << w->pipe); pthread_mutex_unlock(&s->state_lock);
				(void)rx_close_pipe(&s->service, w->pipe);
			}
		}
		pthread_mutex_unlock(&s->command_lock[w->pipe]);
	}
}

static int
handle_setup(struct server *s, const struct usbperiph_event *event)
{
	struct usbperiph_reply reply = { .generation = event->generation, .sequence = event->sequence };
	unsigned mask, reset;
	pthread_mutex_lock(&s->state_lock); mask = s->mask; pthread_mutex_unlock(&s->state_lock);
	reply.stall = rx_usb_setup(event->setup, &mask, &reset) != 0;
	if (!reply.stall && reset) {
		int e = close_pipes(s, reset, event->generation, 1);
		if (e) return e;
	} else if (!reply.stall && event->setup[1] == 1) {
		int e = endpoint_gate(s, 1U << event->setup[2], 1, event->generation);
		if (e) return e;
	}
	if (ioctl(s->usb, USBPERIPH_IOC_REPLY_SETUP, &reply) < 0) return -errno;
	if (!reply.stall) {
		pthread_mutex_lock(&s->state_lock);
		s->mask = mask; s->generation = event->generation; pthread_cond_broadcast(&s->changed);
		pthread_mutex_unlock(&s->state_lock);
	}
	return 0;
}

static void *
self_worker(void *v)
{
	return v;
}

int
main(int argc, char **argv)
{
	struct server s = SERVER_INITIALIZER;
	struct rx_ops ops = { attribute, start, input, stop, send_bytes, settings, state_lock, state_unlock, begin_read, end_read };
	struct usbperiph_config config;
	pthread_t threads[2];
	struct worker workers[2];
	unsigned created = 0;
	int result = 1;
	if (argc == 2 && !strcmp(argv[1], "--self-test")) {
		pthread_t t; void *out = NULL;
		if (pthread_create(&t, NULL, self_worker, &s) || pthread_join(t, &out) || out != &s) return 1;
		puts("pluto-iiod: thread startup PASS"); return 0;
	}
	if (argc != 5) {
		fprintf(stderr, "usage: pluto-iiod USB_DEVICE ADC_DEVICE VID_HEX PID_HEX\n");
		return 2;
	}
	char *end; unsigned long vid, pid;
	errno = 0; vid = strtoul(argv[3], &end, 16);
	if (errno || !*argv[3] || *end || vid > 65535 || !vid) return 2;
	errno = 0; pid = strtoul(argv[4], &end, 16);
	if (errno || !*argv[4] || *end || pid > 65535 || !pid) return 2;
	(void)rx_adc_init(&s.adc, -1);
	s.adc.fd = open(argv[2], O_RDONLY);
	if (s.adc.fd < 0) { perror("ADC open"); goto out; }
	/* Apply a real initial configuration before exposing any IIO attributes. */
	char rate[] = "2400000";
	if (rx_adc_attr(&s.adc, "iio:device0", "INPUT:voltage0", "sampling_frequency", rate, sizeof(rate), 1)) {
		fprintf(stderr, "ADC initial configuration/readback failed\n"); goto out;
	}
	s.usb = open(argv[1], O_RDWR);
	if (s.usb < 0) { perror("USB open"); goto out; }
	(void)rx_usb_config(&config, (uint16_t)vid, (uint16_t)pid);
	if (ioctl(s.usb, USBPERIPH_IOC_CONFIGURE, &config) < 0) { perror("USB configure"); goto out; }
	if (endpoint_gate(&s, 3, 0, 0)) {
		fprintf(stderr, "USB endpoint admission gates are unavailable\n"); goto out;
	}
	rx_init(&s.service, &ops, &s);
	for (unsigned i = 0; i < 2; i++) {
		workers[i] = (struct worker){ &s, i };
		if (pthread_create(&threads[i], NULL, work, &workers[i])) goto shutdown;
		created++;
	}
	signal(SIGINT, interrupt); signal(SIGTERM, interrupt);
	if (ioctl(s.usb, USBPERIPH_IOC_CONNECT) < 0) { perror("USB connect"); goto shutdown; }
	result = 0;
	while (!interrupted) {
		struct usbperiph_event event;
		if (ioctl(s.usb, USBPERIPH_IOC_GET_EVENT, &event) < 0) {
			if (errno == EINTR || errno == EWOULDBLOCK || errno == ETIMEDOUT) continue;
			perror("USB event"); result = 1; break;
		}
		if (event.type == USBPERIPH_EVENT_SETUP) {
			if (handle_setup(&s, &event)) { result = 1; break; }
		} else if (event.type == USBPERIPH_EVENT_RESET || event.type == USBPERIPH_EVENT_DISCONNECT ||
		    event.type == USBPERIPH_EVENT_SUSPEND || (event.type == USBPERIPH_EVENT_CONFIGURE && !event.configured)) {
			if (close_pipes(&s, 3, event.generation, 1)) { result = 1; break; }
		}
	}
shutdown:
	pthread_mutex_lock(&s.state_lock); s.quitting = 1; s.mask = 0; pthread_cond_broadcast(&s.changed); pthread_mutex_unlock(&s.state_lock);
	(void)close_pipes(&s, 3, s.generation, 1);
	for (unsigned i = 0; i < created; i++) (void)pthread_join(threads[i], NULL);
	(void)rx_disconnect(&s.service);
	(void)ioctl(s.usb, USBPERIPH_IOC_DISCONNECT);
out:
	if (s.usb >= 0) close(s.usb);
	if (s.adc.fd >= 0) close(s.adc.fd);
	pthread_cond_destroy(&s.changed); pthread_mutex_destroy(&s.adc_lock); pthread_mutex_destroy(&s.state_lock);
	for (unsigned p = 0; p < 2; p++) { pthread_mutex_destroy(&s.admission[p]); pthread_mutex_destroy(&s.command_lock[p]); }
	return result;
}
