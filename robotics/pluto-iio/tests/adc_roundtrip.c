/* SPDX-License-Identifier: BSD-2-Clause */
/* Execute the real adapter with a quantized-clock ioctl model. No devices. */
#include "native_adc.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
		exit(1); \
	} \
} while (0)

static struct {
	unsigned configs, starts, stops, statuses;
	uint32_t actual, flags;
	uint64_t discontinuities, dropped;
	int last_error, status_error, bad_abi, bad_size, reserved;
} hw;

static int
control(int fd, unsigned long command, void *data)
{
	CHECK(fd == 123);
	if (command == ADI_AXI_ADCIOC_CONFIG) {
		const struct adi_axi_adc_config *config = data;
		hw.configs++;
		/* The observed pairs include 2400000->2399999 and
		 * 2399999->2399998. Expected test values are literal below.
		 */
		hw.actual = config->sample_rate_hz - 1;
		return 0;
	}
	if (command == ADI_AXI_ADCIOC_STREAM_STATUS) {
		struct adi_axi_adc_stream_status *status = data;
		hw.statuses++;
		CHECK(status->abi_version == ADI_AXI_ADC_STREAM_ABI_VERSION);
		CHECK(status->struct_size == sizeof(*status));
		if (hw.status_error) {
			memset(status, 0xff, sizeof(*status));
			errno = hw.status_error;
			return -1;
		}
		status->sample_rate_hz = hw.actual;
		status->flags = hw.flags;
		status->last_error = hw.last_error;
		status->discontinuities = hw.discontinuities;
		status->dropped_blocks = hw.dropped;
		status->reserved[0] = hw.reserved;
		if (hw.bad_abi) status->abi_version++;
		if (hw.bad_size) status->struct_size--;
		return 0;
	}
	if (command == ADI_AXI_ADCIOC_STREAM_START) {
		hw.starts++;
		hw.flags = ADI_AXI_ADC_STREAM_RUNNING;
		hw.last_error = 0;
		hw.discontinuities = hw.dropped = 0;
		return 0;
	}
	if (command == ADI_AXI_ADCIOC_STREAM_STOP) {
		hw.stops++;
		hw.flags &= ~ADI_AXI_ADC_STREAM_RUNNING;
		return 0;
	}
	CHECK(0);
	return -1;
}

static int
write_on(struct rx_adc *adc, const char *device, const char *channel,
    const char *name, const char *value)
{
	char buffer[64];
	CHECK(strlen(value) < sizeof(buffer));
	strcpy(buffer, value);
	return rx_adc_attr(adc, device, channel, name, buffer,
	    sizeof(buffer), 1);
}

static int
write_attr(struct rx_adc *adc, const char *channel, const char *name,
    const char *value)
{
	return write_on(adc, "iio:device0", channel, name, value);
}

static int
write_rate(struct rx_adc *adc, const char *value)
{
	return write_attr(adc, "INPUT:voltage0", "sampling_frequency", value);
}

static void
setup(struct rx_adc *adc, const char *request, int running)
{
	memset(&hw, 0, sizeof(hw));
	CHECK(rx_adc_init(adc, 123) == 0);
	adc->control = control;
	CHECK(write_rate(adc, request) == 0);
	if (running) CHECK(rx_adc_start(adc, 4096) == 0);
}

int
main(void)
{
	struct rx_adc adc;
	struct adi_axi_adc_config saved;
	struct adi_axi_adc_stream_config saved_stream;
	char buffer[64];
	const char *const aliases[][2] = {
		{"iio:device0", "INPUT:voltage0"},
		{"iio:device1", ""},
		{"iio:device1", "INPUT:voltage0"},
		{"iio:device1", "INPUT:voltage1"}
	};

	for (int running = 0; running <= 1; running++) {
		setup(&adc, "2400000", running);
		saved = adc.config;
		saved_stream = adc.stream;
		for (int repeat = 0; repeat < 4; repeat++) {
			CHECK(rx_adc_attr(&adc, "iio:device0", "INPUT:voltage0",
			    "sampling_frequency", buffer, sizeof(buffer), 0) == 7);
			CHECK(!strcmp(buffer, "2399999"));
			CHECK(write_rate(&adc, buffer) == 0);
			CHECK(hw.actual == 2399999 && hw.configs == 1);
			CHECK(hw.starts == (unsigned)running && hw.stops == 0);
			CHECK(adc.running == running && adc.configured);
			CHECK(memcmp(&saved, &adc.config, sizeof(saved)) == 0);
			CHECK(memcmp(&saved_stream, &adc.stream, sizeof(saved_stream)) == 0);
		}
		for (unsigned alias = 0; alias < sizeof(aliases)/sizeof(aliases[0]); alias++) {
			CHECK(write_on(&adc, aliases[alias][0], aliases[alias][1],
			    "sampling_frequency", "2399999") == 0);
			CHECK(hw.configs == 1 && hw.starts == (unsigned)running && hw.stops == 0);
			CHECK(memcmp(&saved, &adc.config, sizeof(saved)) == 0);
			CHECK(memcmp(&saved_stream, &adc.stream, sizeof(saved_stream)) == 0);
		}
	}
	setup(&adc, "2083334", 0);
	CHECK(write_rate(&adc, "2083333") == 0);
	CHECK(adc.config.sample_rate_hz == 2083334 && hw.actual == 2083333);
	CHECK(hw.configs == 1 && hw.starts == 0 && hw.stops == 0);
	CHECK(write_rate(&adc, "2083332") == -ERANGE && hw.configs == 1);

	/* No configured profile or stale equality may authorize a no-op. */
	memset(&hw, 0, sizeof(hw));
	CHECK(rx_adc_init(&adc, 123) == 0);
	adc.control = control;
	adc.status.sample_rate_hz = hw.actual = 2399999;
	CHECK(write_rate(&adc, "2399999") == 0);
	CHECK(hw.configs == 1 && adc.config.sample_rate_hz == 2399999);
	setup(&adc, "2400000", 0);
	hw.actual = 3071999;
	CHECK(write_rate(&adc, "2399999") == 0);
	CHECK(hw.configs == 2 && hw.actual == 2399998);
	setup(&adc, "2400000", 0);
	hw.actual = 0;
	CHECK(write_rate(&adc, "0") == -ERANGE && hw.configs == 1);

	for (int fault = 0; fault < 13; fault++) {
		int running = fault == 8;
		setup(&adc, "2400000", running);
		saved = adc.config;
		int expected = -EPROTO;
		if (fault == 0) { hw.status_error = EIO; expected = -EIO; }
		if (fault == 1) hw.bad_abi = 1;
		if (fault == 2) hw.bad_size = 1;
		if (fault == 3) { hw.last_error = EIO; expected = -EIO; }
		if (fault == 4) { hw.discontinuities = 1; expected = -EOVERFLOW; }
		if (fault == 5) { hw.dropped = 1; expected = -EOVERFLOW; }
		if (fault == 6) {
			hw.flags = ADI_AXI_ADC_STREAM_LOSS_UNKNOWN;
			expected = -EOVERFLOW;
		}
		if (fault == 7) hw.flags = 4;
		if (fault == 8) hw.flags = 0;
		if (fault == 9) hw.flags = ADI_AXI_ADC_STREAM_RUNNING;
		if (fault == 10) hw.reserved = 1;
		if (fault == 11) hw.last_error = -EIO;
		if (fault == 12) hw.last_error = INT32_MIN;
		CHECK(write_rate(&adc, "2399999") == expected);
		CHECK(hw.configs == 1 && hw.starts == (unsigned)running);
		CHECK(hw.stops == 0 && memcmp(&saved, &adc.config, sizeof(saved)) == 0);
	}

	/* A different valid request keeps the existing loss-recovery transaction. */
	setup(&adc, "2400000", 1);
	hw.last_error = EOVERFLOW;
	hw.discontinuities = 1;
	hw.flags |= ADI_AXI_ADC_STREAM_LOSS_UNKNOWN;
	CHECK(write_rate(&adc, "3072000") == 0);
	CHECK(hw.configs == 2 && hw.stops == 1 && hw.starts == 2);
	CHECK(adc.config.sample_rate_hz == 3072000 && hw.actual == 3071999);
	CHECK(adc.running && !hw.last_error && !hw.discontinuities);
	CHECK(write_attr(&adc, "INPUT:voltage0", "rf_bandwidth", "2500000") == 0);
	CHECK(hw.configs == 3 && adc.config.bandwidth_hz == 2500000);
	CHECK(write_attr(&adc, "OUTPUT:altvoltage0", "frequency", "162000000") == 0);
	CHECK(hw.configs == 4 && adc.config.frequency_hz == 162000000);

	setup(&adc, "2400000", 0);
	unsigned before = hw.statuses;
	CHECK(write_rate(&adc, "garbage") == -EINVAL && hw.statuses == before);
	CHECK(write_rate(&adc, "1536000") == -ERANGE && hw.configs == 1);
	before = hw.statuses;
	hw.status_error = EIO;
	CHECK(write_rate(&adc, "1536000") == -EIO && hw.configs == 1);
	CHECK(hw.statuses == before + 1);
	before = hw.statuses;
	CHECK(write_rate(&adc, "garbage") == -EINVAL && hw.statuses == before);
	CHECK(write_rate(&adc, "18446744073709551616") == -ERANGE);
	CHECK(hw.statuses == before);
	CHECK(rx_adc_init(&adc, 123) == 0);
	adc.control = control;
	adc.status.sample_rate_hz = hw.actual = 2083333;
	CHECK(write_rate(&adc, "2083333") == -ERANGE && hw.statuses == before);
	CHECK(write_attr(&adc, "INPUT:voltage0", "filter_fir_en", "1") == -EOPNOTSUPP);
	puts("ADC actual-rate roundtrip PASS: idle/running, fresh status, edge, faults and explicit reconfiguration");
	return 0;
}
