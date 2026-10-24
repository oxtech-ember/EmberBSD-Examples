/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the real worker with a USB syscall fixture, without a device. */
#include <sys/ioctl.h>
#include <stdarg.h>
#include <assert.h>
static int fixture_ioctl(int, unsigned long, ...);
#define ioctl fixture_ioctl
#define main daemon_main
#include "../src/pluto_iiod.c"
#undef main
#undef ioctl

static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready = PTHREAD_COND_INITIALIZER;
static int attempts, release_packet;

static int
fixture_ioctl(int fd, unsigned long op, ...)
{
	va_list args;
	(void)fd;
	if (op != USBPERIPH_IOC_XFER) { errno = EOPNOTSUPP; return -1; }
	va_start(args, op);
	struct usbperiph_xfer *x = va_arg(args, struct usbperiph_xfer *);
	va_end(args);
	if (x->endpoint & 0x80) { x->actual = x->length; return 0; }
	assert(x->length == 512);
	pthread_mutex_lock(&gate);
	attempts++;
	pthread_cond_broadcast(&ready);
	while (attempts == 1 && !release_packet) pthread_cond_wait(&ready, &gate);
	int first = attempts == 1;
	pthread_mutex_unlock(&gate);
	if (!first) { errno = ETIMEDOUT; return -1; }
	const char command[] = "TIMEOUT 4321\n";
	memcpy((void *)(uintptr_t)x->buffer, command, sizeof(command) - 1);
	x->actual = sizeof(command) - 1;
	return 0;
}

int
main(void)
{
	struct server s = { .usb = -1, .mask = 1, .generation = 7,
	    .lock = PTHREAD_MUTEX_INITIALIZER, .state_lock = PTHREAD_MUTEX_INITIALIZER,
	    .changed = PTHREAD_COND_INITIALIZER };
	struct rx_ops ops = { attribute, start, input, stop, send_bytes, settings };
	struct worker w = { &s, 0 };
	pthread_t thread;
	rx_adc_init(&s.adc, -1); rx_init(&s.service, &ops, &s);
	assert(pthread_create(&thread, NULL, work, &w) == 0);
	pthread_mutex_lock(&gate);
	while (attempts == 0) pthread_cond_wait(&ready, &gate);
	/* Close and reopen a pipe without a USB bus generation change. */
	pthread_mutex_lock(&s.state_lock); s.epoch[0]++; pthread_mutex_unlock(&s.state_lock);
	release_packet = 1; pthread_cond_broadcast(&ready);
	while (attempts < 2) pthread_cond_wait(&ready, &gate);
	pthread_mutex_unlock(&gate);
	pthread_mutex_lock(&s.state_lock); s.quitting = 1; pthread_mutex_unlock(&s.state_lock);
	assert(pthread_join(thread, NULL) == 0);
	assert(s.adc.timeout_ms == 2000);
	pthread_cond_destroy(&s.changed); pthread_mutex_destroy(&s.state_lock); pthread_mutex_destroy(&s.lock);
	puts("worker: stale packet after pipe reopen rejected");
	return 0;
}
