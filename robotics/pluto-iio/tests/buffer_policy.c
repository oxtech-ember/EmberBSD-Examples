/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise real START and lifecycle cleanup; only device calls are fixtures. */
#include <sys/ioctl.h>
#include <stdarg.h>
static int fixture_ioctl(int, unsigned long, ...);
#define ioctl fixture_ioctl
#define main daemon_main
#include "../src/pluto_iiod.c"
#undef main
#undef ioctl

#define CHECK(e) do { \
	if (!(e)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); \
		exit(1); \
	} \
} while (0)

static struct adi_axi_adc_stream_config requested;
static unsigned starts;
static char response[64];
static size_t response_used;

static int
fixture_ioctl(int fd, unsigned long op, ...)
{
	(void)fd;
	if (op == USBPERIPH_IOC_ENDPOINT_GATE ||
	    op == USBPERIPH_IOC_REPLY_SETUP || op == ADI_AXI_ADCIOC_STREAM_STOP)
		return 0;
	errno = EOPNOTSUPP;
	return -1;
}

static int
control(int fd, unsigned long op, void *arg)
{
	(void)fd;
	if (op == ADI_AXI_ADCIOC_STREAM_START) {
		requested = *(struct adi_axi_adc_stream_config *)arg;
		starts++;
		return 0;
	}
	if (op == ADI_AXI_ADCIOC_STREAM_STOP) return 0;
	errno = EOPNOTSUPP;
	return -1;
}

static int
output(void *arg, unsigned pipe, const void *data, size_t length)
{
	(void)arg;
	CHECK(pipe == 0);
	CHECK(response_used + length < sizeof(response));
	memcpy(response + response_used, data, length);
	response_used += length;
	response[response_used] = '\0';
	return 0;
}

static void
command(struct server *s, const char *line, const char *expected)
{
	response_used = 0;
	CHECK(rx_feed(&s->service, 0, line, strlen(line)) == 0);
	CHECK(strcmp(response, expected) == 0);
}

static void
open_check(struct server *s, unsigned samples, unsigned blocks, unsigned bytes)
{
	char line[80];
	unsigned before = starts;
	snprintf(line, sizeof(line), "OPEN iio:device1 %u 00000003\n", samples);
	command(s, line, "0\n");
	CHECK(starts == before + 1);
	CHECK(requested.abi_version == ADI_AXI_ADC_STREAM_ABI_VERSION);
	CHECK(requested.struct_size == sizeof(requested));
	CHECK(requested.block_count == blocks);
	CHECK(requested.block_bytes == bytes);
}

static void
reopen_control(struct server *s)
{
	struct usbperiph_event event = { .generation = 8, .sequence = 9,
	    .setup = { 0x41, 1, 0, 0, 0, 0, 0, 0 } };
	CHECK(handle_setup(s, &event) == 0);
	CHECK(s->mask == 1);
}

int
main(void)
{
	struct server s = SERVER_INITIALIZER;
	struct rx_ops ops = { attribute, start, input, stop, output, settings,
	    state_lock, state_unlock, begin_read, end_read };
	CHECK(rx_adc_init(&s.adc, -1) == 0);
	s.adc.control = control;
	rx_init(&s.service, &ops, &s);
	s.mask = 1;
	s.generation = 7;

	/* Missing larger default or a wrong START geometry must fail here. */
	open_check(&s, 32768, 16, 131072);
	CHECK(rx_adc_settings(&s.adc, 0, 4) == -EBUSY);
	command(&s, "SET iio:device1 BUFFERS_COUNT 4\n", "-16\n");
	command(&s, "CLOSE iio:device1\n", "0\n");
	open_check(&s, 65536, 16, 262144);
	/* A real global lifecycle reset preserves the selected queue policy. */
	CHECK(close_pipes(&s, 3, 8, 1) == 0);
	reopen_control(&s);
	open_check(&s, 262144, 16, 262144);
	command(&s, "CLOSE iio:device1\n", "0\n");

	command(&s, "SET iio:device1 BUFFERS_COUNT 4\n", "0\n");
	command(&s, "TIMEOUT 1234\n", "0\n");
	open_check(&s, 32768, 4, 131072);
	CHECK(close_pipes(&s, 3, 8, 1) == 0);
	reopen_control(&s);
	open_check(&s, 32768, 4, 131072);
	command(&s, "CLOSE iio:device1\n", "0\n");
	/* Vendor RESET/new control pipe also leaves explicit SET4 intact. */
	struct usbperiph_event reset = { .generation = 8, .sequence = 10,
	    .setup = { 0x41, 0, 0, 0, 0, 0, 0, 0 } };
	CHECK(handle_setup(&s, &reset) == 0);
	reopen_control(&s);
	open_check(&s, 32768, 4, 131072);
	command(&s, "CLOSE iio:device1\n", "0\n");

	command(&s, "SET iio:device1 BUFFERS_COUNT 3\n", "0\n");
	command(&s, "SET iio:device1 BUFFERS_COUNT 2\n", "-22\n");
	CHECK(rx_adc_settings(&s.adc, 0, 2) == -EINVAL);
	open_check(&s, 32768, 3, 131072);
	command(&s, "CLOSE iio:device1\n", "0\n");
	command(&s, "SET iio:device1 BUFFERS_COUNT 16\n", "0\n");
	command(&s, "SET iio:device1 BUFFERS_COUNT 17\n", "-22\n");
	CHECK(rx_adc_settings(&s.adc, 0, 17) == -EINVAL);
	open_check(&s, 32768, 16, 131072);
	command(&s, "CLOSE iio:device1\n", "0\n");
	CHECK(s.adc.timeout_ms == 1234);

	pthread_cond_destroy(&s.changed);
	pthread_mutex_destroy(&s.state_lock);
	pthread_mutex_destroy(&s.adc_lock);
	for (unsigned p = 0; p < 2; p++) {
		pthread_mutex_destroy(&s.command_lock[p]);
		pthread_mutex_destroy(&s.admission[p]);
	}
	puts("buffer policy: default START geometry, explicit depth, and RESET persistence PASS");
	return 0;
}
