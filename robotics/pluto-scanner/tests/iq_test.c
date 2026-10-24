/* SPDX-License-Identifier: BSD-2-Clause */
/* Test the real CLI adapter; only the device system calls are substituted. */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#define _NETBSD_SOURCE
#include <stdint.h>
#include <sys/ioctl.h>
#include <dev/fdt/adi_axi_adc.h>
#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
		abort(); \
	} \
} while (0)
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int device_open(const char *, int, ...);
static int device_close(int);
static int device_ioctl(int, unsigned long, ...);
static ssize_t device_read(int, void *, size_t);
#define open device_open
#define close device_close
#define ioctl device_ioctl
#define read device_read
#define main iq_cli_main
#include "../src/pluto_iq.c"
#undef main
#undef read
#undef ioctl
#undef close
#undef open

/* Fail after the complete payload, with no buffered bytes left to flush. */
struct footer_sink {
	size_t limit;
	size_t accepted;
	unsigned refused;
};

static ssize_t
sink_write(void *cookie, const char *data, size_t length)
{
	struct footer_sink *sink = cookie;

	(void)data;
	if (length > sink->limit - sink->accepted) {
		sink->refused++;
		errno = ENOSPC;
		return -1;
	}
	sink->accepted += length;
	return (ssize_t)length;
}

#ifndef __linux__
static int
bsd_sink_write(void *cookie, const char *data, int length)
{
	return (int)sink_write(cookie, data, (size_t)length);
}
#endif

static FILE *
footer_stream(struct footer_sink *sink)
{
#ifdef __linux__
	cookie_io_functions_t ops = { .write = sink_write };
	FILE *out = fopencookie(sink, "w", ops);
#else
	FILE *out = funopen(sink, NULL, bsd_sink_write, NULL, NULL);
#endif

	CHECK(out != NULL);
	CHECK(setvbuf(out, NULL, _IONBF, 0) == 0);
	return out;
}

static unsigned phase, reads, opens, closes;
static uint32_t samples;
static uint64_t expected_frequency = 1090000000;
static uint32_t expected_rate = 2400000, expected_bandwidth = 2000000;
static int mode;
enum { NORMAL, SHORT, INTERRUPT, READ_ERROR, CAPTURE_ERROR, STALE,
    BAD_ABI, BAD_SIZE, BAD_COUNT, BAD_ERROR, STATUS_ERROR, CLOSE_ERROR,
    CONFIG_ERROR, OPEN_ERROR, EOF_READ };

static int
device_open(const char *path, int flags, ...)
{
	CHECK(strcmp(path, "/dev/test") == 0);
	CHECK(flags == (O_RDONLY | O_CLOEXEC));
	opens++;
	if (mode == OPEN_ERROR) { errno = ENOENT; return -1; }
	return 7;
}
static int
device_close(int fd)
{
	CHECK(fd == 7); closes++;
	if (mode == CLOSE_ERROR) { errno = EIO; return -1; }
	return 0;
}
static int
device_ioctl(int fd, unsigned long op, ...)
{
	va_list ap;
	void *p;
	CHECK(fd == 7);
	va_start(ap, op); p = va_arg(ap, void *); va_end(ap);
	if (op == ADI_AXI_ADCIOC_CONFIG) {
		const struct adi_axi_adc_config *c = p;
		CHECK(phase == 0); phase = 1;
		CHECK(c->abi_version == 1 && c->struct_size == sizeof(*c));
		CHECK(c->frequency_hz == expected_frequency && c->sample_rate_hz == expected_rate);
		CHECK(c->bandwidth_hz == expected_bandwidth && c->rf_port == 0);
		CHECK(c->rx_mask == 1 && c->channel_mask == 3);
		CHECK(c->gain_mode[0] == ADI_AXI_ADC_GAIN_SLOW);
		CHECK(c->timeout_ms > 0 && c->timeout_ms <= 5000);
		if (mode == CONFIG_ERROR) { errno = EINVAL; return -1; }
	} else if (op == ADI_AXI_ADCIOC_CAPTURE) {
		const struct adi_axi_adc_capture *c = p;
		CHECK(phase == 1); phase = 2;
		CHECK(c->abi_version == 1 && c->struct_size == sizeof(*c));
		CHECK(c->sample_count == samples);
		if (mode == CAPTURE_ERROR) { errno = EIO; return -1; }
	} else {
		struct adi_axi_adc_status *s = p;
		CHECK(op == ADI_AXI_ADCIOC_STATUS && phase == 3); phase = 4;
		CHECK(s->abi_version == 1 && s->struct_size == sizeof(*s));
		CHECK(s->state == 0 && s->captured_samples == 0);
		if (mode == STATUS_ERROR) { errno = EIO; return -1; }
		s->state = mode == STALE ? ADI_AXI_ADC_STATE_CONFIGURED : ADI_AXI_ADC_STATE_CAPTURED;
		s->captured_samples = mode == BAD_COUNT ? samples - 2 : samples;
		if (mode == BAD_ABI) s->abi_version = 2;
		if (mode == BAD_SIZE) s->struct_size = 0;
		if (mode == BAD_ERROR) s->last_capture_error = EIO;
	}
	return 0;
}
static ssize_t
device_read(int fd, void *data, size_t length)
{
	CHECK(fd == 7 && phase == 2);
	/* A sub-buffer first read violates ABI1 and would fail on hardware. */
	CHECK(length == (size_t)samples * 4);
	reads++;
	if (mode == INTERRUPT && reads == 1) { errno = EINTR; return -1; }
	if (mode == READ_ERROR) { errno = EIO; return -1; }
	phase = 3;
	for (size_t i = 0; i < length; i++) ((unsigned char *)data)[i] = (unsigned char)i;
	if (mode == SHORT) return (ssize_t)length - 4;
	if (mode == EOF_READ) return 0;
	return (ssize_t)length;
}
static void
run(int scenario, const char *count, int good)
{
	char *argv[] = { "pluto-iq", "/dev/test", "1090000000", "2400000", "2000000", (char *)count };
	FILE *out = tmpfile(), *err = tmpfile();
	char text[2048];
	int result;
	CHECK(out && err);
	phase = reads = opens = closes = 0; mode = scenario;
	samples = (uint32_t)strtoul(count, NULL, 10);
	result = iq_run(6, argv, out, err);
	CHECK((result == 0) == good);
	if (good) {
		CHECK(phase == 4 && opens == 1 && closes == 1);
		CHECK(reads == (scenario == INTERRUPT ? 2U : 1U));
		rewind(out);
		size_t n = fread(text, 1, sizeof(text)-1, out); text[n] = '\0';
		CHECK(strstr(text, "PLUTO-IQ 1\nBEGIN block=0 discontinuity=1\n"));
		CHECK(strstr(text, "REQUEST frequency_hz=1090000000 sample_rate_hz=2400000 bandwidth_hz=2000000\n"));
		CHECK(strstr(text, "FORMAT s12_le16_iq bytes_per_sample=4\n"));
		CHECK(strstr(text, "DATA offset=00000000 0001020304050607"));
		/* Independently parse every row: missing, repeated, and corrupt data fail. */
		rewind(out);
		for (unsigned i = 0; i < 5; i++) CHECK(fgets(text, sizeof(text), out));
		for (size_t offset = 0; offset < (size_t)samples * 4; offset += 32) {
			size_t row_offset, length = (size_t)samples * 4 - offset;
			char hex[65], extra;
			if (length > 32) length = 32;
			CHECK(fgets(text, sizeof(text), out));
			CHECK(sscanf(text, "DATA offset=%zx %64s %c", &row_offset, hex, &extra) == 2);
			CHECK(row_offset == offset && strlen(hex) == length * 2);
			for (size_t j = 0; j < length; j++) {
				unsigned value;
				CHECK(sscanf(hex + j * 2, "%2x", &value) == 1);
				CHECK(value == (unsigned char)(offset + j));
			}
		}
		CHECK(fgets(text, sizeof(text), out));
		if (samples == 2)
			CHECK(strcmp(text, "END block=0 samples=2 bytes=8 status=ok\n") == 0);
		else
			CHECK(strcmp(text, "END block=0 samples=262144 bytes=1048576 status=ok\n") == 0);
		CHECK(fgetc(out) == EOF);

	} else {
		CHECK(ftell(out) == 0); /* No stale or partially accepted IQ. */
		CHECK(closes == (opens && scenario != OPEN_ERROR ? 1U : 0U));
	}
	fclose(out); fclose(err);
}
int
main(void)
{
	run(NORMAL, "2", 1);
	run(NORMAL, "262144", 1);
	run(INTERRUPT, "2", 1);
	for (int m = SHORT; m <= EOF_READ; m++) {
		if (m != INTERRUPT) run(m, "2", 0);
	}
	const char *bad[] = { "0", "1", "3", "262145", "262146", "4294967296", "-2", "+2", " 2", "2x", "" };
	for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
		run(NORMAL, bad[i], 0); CHECK(opens == 0);
	}
	char *argv[] = { "pluto-iq", "/dev/test", "1090000000", "2400000", "2000000", "2" };
	const char *badnum[] = { "0", "999999999999", "-1", "+1", "1x", "", " 1" };
	for (int field = 2; field <= 4; field++) {
		char *save = argv[field];
		for (size_t i = 0; i < sizeof(badnum)/sizeof(badnum[0]); i++) {
			FILE *out = tmpfile(), *err = tmpfile(); CHECK(out && err);
			argv[field] = (char *)badnum[i]; opens = 0;
			CHECK(iq_run(6, argv, out, err) == 2 && opens == 0 && ftell(out) == 0);
			fclose(out); fclose(err);
		}
		argv[field] = save;
	}
	FILE *helpout = tmpfile(), *helperr = tmpfile(); CHECK(helpout && helperr);
	char *help[] = { "pluto-iq", "--help" };
	opens = 0;
	CHECK(iq_run(2, help, helpout, helperr) == 0 && opens == 0);
	CHECK(iq_run(1, argv, helpout, helperr) == 2 && opens == 0);
	char *save_device = argv[1]; argv[1] = "";
	CHECK(iq_run(6, argv, helpout, helperr) == 2 && opens == 0);
	argv[1] = save_device;
	fclose(helpout); fclose(helperr);
	/* A second invocation must use its own settings and independent boundary. */
	argv[2] = "162000000"; argv[3] = "3072000"; argv[4] = "600000";
	expected_frequency = 162000000; expected_rate = 3072000; expected_bandwidth = 600000;
	FILE *out = tmpfile(), *err = tmpfile(); CHECK(out && err);
	phase = reads = opens = closes = 0; mode = NORMAL; samples = 2;
	CHECK(iq_run(6, argv, out, err) == 0);
	rewind(out);
	char text[512]; size_t n = fread(text, 1, sizeof(text)-1, out); text[n] = '\0';
	CHECK(strstr(text, "REQUEST frequency_hz=162000000 sample_rate_hz=3072000 bandwidth_hz=600000\n"));
	fclose(out); fclose(err);
	/* A failed END write must fail even when the following fflush succeeds. */
	const char *footer = strstr(text, "END block=0 "); CHECK(footer != NULL);
	struct footer_sink sink = { .limit = (size_t)(footer - text) };
	out = footer_stream(&sink); err = tmpfile(); CHECK(err != NULL);
	phase = reads = opens = closes = 0;
	int footer_result = iq_run(6, argv, out, err);
	CHECK(sink.accepted == sink.limit && sink.refused > 0);
	CHECK(ferror(out) && fflush(out) == 0);
	CHECK(footer_result == 1);
	fclose(out); fclose(err);
	/* Output errors must propagate to the exit status. */
	out = fopen("/dev/null", "r"); err = tmpfile(); CHECK(out && err);
	phase = reads = opens = closes = 0;
	CHECK(iq_run(6, argv, out, err) == 1 && ferror(out));
	fclose(out); fclose(err);
	puts("PASS finite IQ adapter, framing, maximum count, strict CLI, EINTR and failure refusal");
	return 0;
}
