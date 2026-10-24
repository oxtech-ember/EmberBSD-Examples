/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the actual adapter against the documented lazy-read ABI. */
#define _POSIX_C_SOURCE 200809L
#include <sys/ioctl.h>
#include <stdint.h>
#include <dev/fdt/adi_axi_adc.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int fixture_ioctl(int, unsigned long, ...);
static ssize_t fixture_read(int, void *, size_t);
#define ioctl fixture_ioctl
#define read fixture_read
#define main scanner_cli_main
#include "../src/pluto_scan.c"
#undef main
#undef read
#undef ioctl

static unsigned phase, reads, status_calls;
static int fail_read;

static int
fixture_ioctl(int fd, unsigned long operation, ...)
{
	va_list ap;
	void *value;
	assert(fd == 7);
	va_start(ap, operation); value = va_arg(ap, void *); va_end(ap);
	if (operation == ADI_AXI_ADCIOC_CONFIG) {
		assert(phase == 0); phase = 1;
	} else if (operation == ADI_AXI_ADCIOC_CAPTURE) {
		assert(phase == 1); phase = 2;
	} else {
		struct adi_axi_adc_status *status = value;
		assert(operation == ADI_AXI_ADCIOC_STATUS);
		status_calls++;
		status->state = phase == 4 ? ADI_AXI_ADC_STATE_CAPTURED :
		    ADI_AXI_ADC_STATE_CONFIGURED;
		status->captured_samples = phase == 4 ? 4096 : 0;
	}
	return 0;
}

static ssize_t
fixture_read(int fd, void *data, size_t length)
{
	assert(fd == 7 && phase >= 2 && phase < 4);
	if (fail_read) { errno = EIO; return -1; }
	assert(status_calls == 0); /* read starts DMA; STATUS does not. */
	reads++; phase++;
	size_t bytes = phase == 3 ? length / 2 : length;
	memset(data, 0, bytes);
	return (ssize_t)bytes;
}

int
main(void)
{
	struct scan_plan plan = { SCAN_MIN_HZ, SCAN_MAX_HZ, 3000000,
	    10000000, 8000000, 4096 };
	struct receiver rx = { .fd = 7, .plan = &plan };
	struct scan_result result;
	assert(scan_acquire(&plan, 1000000000, capture, &rx, &result) == 0);
	assert(phase == 4 && reads == 2 && status_calls == 1);
	phase = reads = status_calls = 0; fail_read = 1;
	result.peak_count = 5;
	assert(scan_acquire(&plan, 1000000000, capture, &rx, &result) == EIO);
	assert(status_calls == 0 && result.peak_count == 0);
	puts("PASS adapter CONFIG/CAPTURE/read/read/STATUS sequence and read-error refusal");
	return 0;
}
