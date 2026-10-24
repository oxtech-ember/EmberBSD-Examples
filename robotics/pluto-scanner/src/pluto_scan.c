/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD contributors, AI-assisted by OpenAI Codex (GPT-6). */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <sys/ioctl.h>
#include <dev/fdt/adi_axi_adc.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "scan.h"

struct receiver {
	int fd;
	const struct scan_plan *plan;
	struct adi_axi_adc_status status;
};
static volatile sig_atomic_t interrupted;

static void
stop_scan(int signo)
{
	(void)signo;
	interrupted = 1;
}

static int
capture(void *cookie, uint64_t center, int16_t *iq, size_t samples)
{
	struct receiver *rx = cookie;
	struct adi_axi_adc_config config = {
		.abi_version = ADI_AXI_ADC_ABI_VERSION,
		.struct_size = sizeof(config),
		.frequency_hz = center,
		.sample_rate_hz = rx->plan->rate_hz,
		.bandwidth_hz = rx->plan->bandwidth_hz,
		.rx_mask = ADI_AXI_ADC_RX1_MASK,
		.channel_mask = ADI_AXI_ADC_RX1_IQ_CHANNEL_MASK,
		.gain_mode = { ADI_AXI_ADC_GAIN_SLOW, ADI_AXI_ADC_GAIN_SLOW },
		.timeout_ms = 1000
	};
	struct adi_axi_adc_capture request = {
		.abi_version = ADI_AXI_ADC_ABI_VERSION,
		.struct_size = sizeof(request),
		.sample_count = (uint32_t)samples,
		.timeout_ms = 1000
	};
	memset(&rx->status, 0, sizeof(rx->status));
	rx->status.abi_version = ADI_AXI_ADC_ABI_VERSION;
	rx->status.struct_size = sizeof(rx->status);
	if (interrupted)
		return EINTR;
	if (ioctl(rx->fd, ADI_AXI_ADCIOC_CONFIG, &config) == -1 ||
	    ioctl(rx->fd, ADI_AXI_ADCIOC_CAPTURE, &request) == -1)
		return errno;
	size_t bytes = samples * ADI_AXI_ADC_IQ_SAMPLE_BYTES, offset = 0;
	while (offset < bytes) {
		ssize_t got = read(rx->fd, (unsigned char *)iq + offset, bytes - offset);
		if (got < 0) {
			if (errno == EINTR && !interrupted)
				continue;
			return errno;
		}
		if (got == 0)
			return EIO;
		offset += (size_t)got;
	}
	/* CAPTURE queues the request. The first read performs the actual DMA. */
	if (ioctl(rx->fd, ADI_AXI_ADCIOC_STATUS, &rx->status) == -1)
		return errno;
	if (rx->status.abi_version != ADI_AXI_ADC_ABI_VERSION ||
	    rx->status.struct_size != sizeof(rx->status) ||
	    rx->status.state != ADI_AXI_ADC_STATE_CAPTURED ||
	    rx->status.captured_samples != samples ||
	    rx->status.last_capture_error != 0)
		return EIO;
	return 0;
}

static int
frequency(const char *text, uint64_t *value)
{
	uint64_t n = 0;
	if (*text == '\0')
		return EINVAL;
	for (; *text; text++) {
		if (*text < '0' || *text > '9')
			return EINVAL;
		unsigned digit = (unsigned)(*text - '0');
		if (n > (SCAN_MAX_HZ - digit) / 10)
			return EINVAL;
		n = n * 10 + digit;
	}
	if (n < SCAN_MIN_HZ || n > SCAN_MAX_HZ)
		return EINVAL;
	*value = n;
	return 0;
}

static void
usage(FILE *out)
{
	fputs("usage: pluto-scan [device [first_hz last_hz]]\n"
	    "Defaults: /dev/adiadc0, 325000000..3800000000 Hz, one RX-only sweep.\n"
	    "Explicit endpoints: 70000000..6000000000 Hz if the driver permits them.\n"
	    "CSV levels are relative dBFS per FFT bin, not calibrated dBm.\n", out);
}

int
main(int argc, char **argv)
{
	struct scan_plan plan = {
		SCAN_DEFAULT_MIN_HZ, SCAN_DEFAULT_MAX_HZ,
		3000000, 10000000, 8000000, 4096
	};
	struct receiver rx = { .fd = -1, .plan = &plan };
	struct sigaction action;
	const char *device = argc > 1 ? argv[1] : "/dev/adiadc0";
	int error = 0;
	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		usage(stdout);
		return 0;
	}
	if ((argc != 1 && argc != 2 && argc != 4) ||
	    (argc == 4 && (frequency(argv[2], &plan.first_hz) != 0 ||
	    frequency(argv[3], &plan.last_hz) != 0)) || scan_plan_check(&plan) != 0) {
		usage(stderr);
		return 2;
	}
	memset(&action, 0, sizeof(action));
	action.sa_handler = stop_scan;
	if (sigemptyset(&action.sa_mask) == -1 ||
	    sigaction(SIGINT, &action, NULL) == -1 || sigaction(SIGTERM, &action, NULL) == -1) {
		perror("signal setup");
		return 1;
	}
	rx.fd = open(device, O_RDONLY | O_CLOEXEC);
	if (rx.fd == -1) {
		perror(device);
		return 1;
	}
	puts("center_hz,noise_dbfs,peak_hz,peak_dbfs,rssi1_mdB,gain1_mdB");
	for (uint64_t center = plan.first_hz; !interrupted;) {
		struct scan_result result;
		error = scan_acquire(&plan, center, capture, &rx, &result);
		if (error != 0) {
			fprintf(stderr, "capture at %" PRIu64 " Hz: %s\n", center, strerror(error));
			break;
		}
		for (size_t i = 0; i < result.peak_count || i == 0; i++) {
			printf("%" PRIu64 ",%.2f,", center, result.noise_dbfs);
			if (result.peak_count > 0)
				printf("%.0f,%.2f,", result.peaks[i].frequency_hz, result.peaks[i].dbfs);
			else
				fputs(",,", stdout);
			if (rx.status.valid_mask & ADI_AXI_ADC_RX1_MASK)
				printf("%" PRId32 ",%" PRId32, rx.status.rssi_mdB[0], rx.status.gain_mdB[0]);
			else
				putchar(',');
			putchar('\n');
		}
		if (fflush(stdout) == EOF) { error = EIO; break; }
		uint64_t next;
		int advance = scan_plan_next(&plan, center, &next);
		if (advance == ENOENT)
			break;
		if (advance != 0) { error = advance; break; }
		center = next;
	}
	if (close(rx.fd) == -1 && error == 0)
		error = errno;
	return interrupted ? 130 : (error != 0 ? 1 : 0);
}
