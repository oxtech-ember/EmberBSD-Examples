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
#include <unistd.h>

struct server {
	struct rx_adc adc;
	struct rx_service service;
	int usb, quitting;
	unsigned mask;
	uint64_t generation;
	uint64_t epoch[2];
	pthread_mutex_t lock;
	pthread_mutex_t state_lock;
	pthread_cond_t changed;
};
struct worker { struct server *server; unsigned pipe; };
static volatile sig_atomic_t interrupted;

static void
interrupt(int signo)
{
	(void)signo;
	interrupted = 1;
}

static int
transfer(const struct server *s, unsigned ep, const void *b, size_t n, uint64_t generation)
{
	struct usbperiph_xfer x = {
		.buffer = (uint64_t)(uintptr_t)b, .generation = generation,
		.endpoint = ep, .length = (uint32_t)n, .timeout_ms = 1000
	};
	if (ioctl(s->usb, USBPERIPH_IOC_XFER, &x) < 0) return -errno;
	if (x.actual > n || x.generation != generation) return -ESTALE;
	return (int)x.actual;
}

static int
send_bytes(void *v, unsigned pipe, const void *data, size_t n)
{
	struct server *s = v;
	const unsigned char *b = data;
	uint64_t generation;
	pthread_mutex_lock(&s->state_lock); generation = s->generation; pthread_mutex_unlock(&s->state_lock);
	while (n) {
		size_t want = n > USBPERIPH_MAX_XFER ? USBPERIPH_MAX_XFER : n;
		int got = transfer(s, 0x81 + pipe, (void *)(uintptr_t)b, want, generation);
		if (got <= 0) return got < 0 ? got : -EPIPE;
		b += got; n -= (size_t)got;
	}
	return 0;
}

static int
attribute(void *v, const char *d, const char *c, const char *a, char *b, size_t n, int w)
{
	return rx_adc_attr(&((struct server *)v)->adc, d, c, a, b, n, w);
}
static int start(void *v, unsigned n) { return rx_adc_start(&((struct server *)v)->adc, n); }
static int stop(void *v) { return rx_adc_stop(&((struct server *)v)->adc); }
static ssize_t input(void *v, void *b, size_t n) { return rx_adc_read(&((struct server *)v)->adc, b, n); }
static int settings(void *v, unsigned timeout, unsigned blocks) { return rx_adc_settings(&((struct server *)v)->adc, timeout, blocks); }

static void
cancel(const struct server *s, unsigned mask)
{
	/* This does not hold the protocol mutex. It can wake a blocked worker. */
	for (uint32_t ep = 1; ep <= 2; ep++) if (mask & (1U << (ep - 1))) {
		uint32_t in = ep | 0x80;
		(void)ioctl(s->usb, USBPERIPH_IOC_CANCEL, &ep);
		(void)ioctl(s->usb, USBPERIPH_IOC_CANCEL, &in);
	}
	(void)ioctl(s->adc.fd, ADI_AXI_ADCIOC_STREAM_STOP, (void *)NULL);
}

static void *
work(void *v)
{
	struct worker *w = v;
	struct server *s = w->server;
	/* One high-speed packet also completes exact-512-byte host writes. */
	unsigned char data[512] = {0};
	for (;;) {
		uint64_t generation, epoch;
		pthread_mutex_lock(&s->state_lock);
		while (!(s->mask & (1U << w->pipe)) && !s->quitting)
			pthread_cond_wait(&s->changed, &s->state_lock);
		if (s->quitting) { pthread_mutex_unlock(&s->state_lock); return NULL; }
		generation = s->generation;
		epoch = s->epoch[w->pipe];
		pthread_mutex_unlock(&s->state_lock);
		int n = transfer(s, w->pipe + 1, data, sizeof(data), generation);
		pthread_mutex_lock(&s->lock);
		pthread_mutex_lock(&s->state_lock);
		int valid = s->generation == generation && s->epoch[w->pipe] == epoch &&
		    (s->mask & (1U << w->pipe));
		pthread_mutex_unlock(&s->state_lock);
		if (valid) {
			if (n > 0) {
				int e = rx_feed(&s->service, w->pipe, data, (size_t)n);
				if (e) {
					fprintf(stderr, "pipe %u: protocol/transport error %d\n", w->pipe, -e);
					pthread_mutex_lock(&s->state_lock); s->mask &= ~(1U << w->pipe); pthread_mutex_unlock(&s->state_lock);
					(void)rx_disconnect(&s->service);
				}
			} else if (n < 0 && n != -ETIMEDOUT && n != -EINTR && n != -ECANCELED && n != -ESTALE) {
				pthread_mutex_lock(&s->state_lock); s->mask &= ~(1U << w->pipe); pthread_mutex_unlock(&s->state_lock);
				(void)rx_disconnect(&s->service);
			}
		}
		pthread_mutex_unlock(&s->lock);
	}
}

static void *
self_worker(void *v)
{
	return v;
}

int
main(int argc, char **argv)
{
	struct server s = { .usb = -1, .adc.fd = -1,
	    .lock = PTHREAD_MUTEX_INITIALIZER, .state_lock = PTHREAD_MUTEX_INITIALIZER,
	    .changed = PTHREAD_COND_INITIALIZER };
	struct rx_ops ops = { attribute, start, input, stop, send_bytes, settings };
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
			struct usbperiph_reply reply = { .generation = event.generation, .sequence = event.sequence };
			unsigned mask, reset;
			pthread_mutex_lock(&s.state_lock); mask = s.mask; pthread_mutex_unlock(&s.state_lock);
			reply.stall = rx_usb_setup(event.setup, &mask, &reset) != 0;
			if (!reply.stall && reset) {
				pthread_mutex_lock(&s.state_lock);
				s.mask &= ~reset;
				for (unsigned p = 0; p < 2; p++) if (reset & (1U << p)) s.epoch[p]++;
				pthread_mutex_unlock(&s.state_lock);
				cancel(&s, reset);
			}
			pthread_mutex_lock(&s.lock);
			if (!reply.stall && reset) (void)rx_disconnect(&s.service);
			if (ioctl(s.usb, USBPERIPH_IOC_REPLY_SETUP, &reply) < 0) { result = 1; pthread_mutex_unlock(&s.lock); break; }
			if (!reply.stall) {
				pthread_mutex_lock(&s.state_lock);
				s.mask = mask; s.generation = event.generation; pthread_cond_broadcast(&s.changed);
				pthread_mutex_unlock(&s.state_lock);
			}
			pthread_mutex_unlock(&s.lock);
		} else if (event.type == USBPERIPH_EVENT_RESET || event.type == USBPERIPH_EVENT_DISCONNECT ||
		    event.type == USBPERIPH_EVENT_SUSPEND || (event.type == USBPERIPH_EVENT_CONFIGURE && !event.configured)) {
			pthread_mutex_lock(&s.state_lock);
			s.mask = 0; s.generation = event.generation; s.epoch[0]++; s.epoch[1]++;
			pthread_mutex_unlock(&s.state_lock);
			cancel(&s, 3);
			pthread_mutex_lock(&s.lock);
			(void)rx_disconnect(&s.service);
			pthread_mutex_unlock(&s.lock);
		}
	}
shutdown:
	pthread_mutex_lock(&s.state_lock); s.quitting = 1; s.mask = 0; pthread_cond_broadcast(&s.changed); pthread_mutex_unlock(&s.state_lock);
	cancel(&s, 3);
	for (unsigned i = 0; i < created; i++) (void)pthread_join(threads[i], NULL);
	(void)rx_disconnect(&s.service);
	(void)ioctl(s.usb, USBPERIPH_IOC_DISCONNECT);
out:
	if (s.usb >= 0) close(s.usb);
	if (s.adc.fd >= 0) close(s.adc.fd);
	pthread_cond_destroy(&s.changed); pthread_mutex_destroy(&s.lock); pthread_mutex_destroy(&s.state_lock);
	return result;
}
