/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD contributors, AI-assisted by OpenAI Codex (GPT-6). */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <sys/ioctl.h>
#include <dev/fdt/adi_axi_adc.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int
number(const char *text, uint64_t min, uint64_t max, uint64_t *value)
{
	uint64_t n = 0;

	if (*text == '\0')
		return EINVAL;
	for (; *text != '\0'; text++) {
		unsigned digit;

		if (*text < '0' || *text > '9')
			return EINVAL;
		digit = (unsigned)(*text - '0');
		if (n > (max - digit) / 10)
			return EINVAL;
		n = n * 10 + digit;
	}
	if (n < min || n > max)
		return EINVAL;
	*value = n;
	return 0;
}

static void
usage(FILE *out)
{
	fputs("usage: pluto-iq DEVICE FREQUENCY RATE BANDWIDTH SAMPLE_COUNT\n"
	    "Use decimal hertz and an even count from 2 through 262144.\n"
	    "One independent finite RX1 block; requested rate is not measured rate.\n", out);
}

static int
iq_run(int argc, char **argv, FILE *out, FILE *err)
{
	struct adi_axi_adc_config config = {
		.abi_version = ADI_AXI_ADC_ABI_VERSION,
		.struct_size = sizeof(config),
		.rx_mask = ADI_AXI_ADC_RX1_MASK,
		.channel_mask = ADI_AXI_ADC_RX1_IQ_CHANNEL_MASK,
		.gain_mode = { ADI_AXI_ADC_GAIN_SLOW, ADI_AXI_ADC_GAIN_SLOW },
		.timeout_ms = 1000
	};
	struct adi_axi_adc_capture request = {
		.abi_version = ADI_AXI_ADC_ABI_VERSION,
		.struct_size = sizeof(request),
		.timeout_ms = 1000
	};
	struct adi_axi_adc_status status = {
		.abi_version = ADI_AXI_ADC_ABI_VERSION,
		.struct_size = sizeof(status)
	};
	uint64_t frequency, rate, bandwidth, count;
	unsigned char *iq;
	size_t bytes;
	ssize_t got;
	int fd, error = 0;

	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		usage(out);
		return ferror(out) ? 1 : 0;
	}
	if (argc != 6 || argv[1][0] == '\0' ||
	    number(argv[2], 70000000, 6000000000ULL, &frequency) != 0 ||
	    number(argv[3], 1, UINT32_MAX, &rate) != 0 ||
	    number(argv[4], 1, UINT32_MAX, &bandwidth) != 0 ||
	    number(argv[5], 2, ADI_AXI_ADC_CAPTURE_MAX_BYTES /
	    ADI_AXI_ADC_IQ_SAMPLE_BYTES, &count) != 0 ||
	    (count * ADI_AXI_ADC_IQ_SAMPLE_BYTES) %
	    ADI_AXI_ADC_CAPTURE_ALIGNMENT != 0) {
		usage(err);
		return 2;
	}
	bytes = (size_t)count * ADI_AXI_ADC_IQ_SAMPLE_BYTES;
	iq = malloc(bytes);
	if (iq == NULL) {
		fprintf(err, "allocate IQ: %s\n", strerror(errno));
		return 1;
	}
	config.frequency_hz = frequency;
	config.sample_rate_hz = (uint32_t)rate;
	config.bandwidth_hz = (uint32_t)bandwidth;
	request.sample_count = (uint32_t)count;
	fd = open(argv[1], O_RDONLY | O_CLOEXEC);
	if (fd == -1) {
		error = errno;
		goto done;
	}
	if (ioctl(fd, ADI_AXI_ADCIOC_CONFIG, &config) == -1 ||
	    ioctl(fd, ADI_AXI_ADCIOC_CAPTURE, &request) == -1) {
		error = errno;
		goto close_device;
	}
	/* ABI1 starts DMA on the first read. It needs the whole finite buffer. */
	do {
		got = read(fd, iq, bytes);
	} while (got == -1 && errno == EINTR);
	if (got < 0) {
		error = errno;
		goto close_device;
	}
	/* Refuse an incomplete block instead of joining data across boundaries. */
	if ((size_t)got != bytes) {
		error = EIO;
		goto close_device;
	}
	if (ioctl(fd, ADI_AXI_ADCIOC_STATUS, &status) == -1) {
		error = errno;
		goto close_device;
	}
	if (status.abi_version != ADI_AXI_ADC_ABI_VERSION ||
	    status.struct_size != sizeof(status) ||
	    status.state != ADI_AXI_ADC_STATE_CAPTURED ||
	    status.captured_samples != count || status.last_capture_error != 0)
		error = EIO;
close_device:
	if (close(fd) == -1 && error == 0)
		error = errno;
	if (error != 0)
		goto done;

	/* Publish nothing until this capture and its close have succeeded. */
	fprintf(out, "PLUTO-IQ 1\nBEGIN block=0 discontinuity=1\n"
	    "REQUEST frequency_hz=%" PRIu64 " sample_rate_hz=%" PRIu64
	    " bandwidth_hz=%" PRIu64 "\n"
	    "FORMAT s12_le16_iq bytes_per_sample=4\n"
	    "COUNT samples=%" PRIu64 " bytes=%zu\n",
	    frequency, rate, bandwidth, count, bytes);
	for (size_t offset = 0; offset < bytes; offset += 32) {
		size_t length = bytes - offset < 32 ? bytes - offset : 32;

		fprintf(out, "DATA offset=%08zx ", offset);
		for (size_t i = 0; i < length; i++)
			fprintf(out, "%02x", (unsigned)iq[offset + i]);
		fputc('\n', out);
		if (ferror(out)) {
			error = EIO;
			goto done;
		}
	}
	if (fprintf(out, "END block=0 samples=%" PRIu64
	    " bytes=%zu status=ok\n", count, bytes) < 0)
		error = EIO;
	if (fflush(out) == EOF || ferror(out))
		error = EIO;
done:
	free(iq);
	if (error != 0)
		fprintf(err, "finite IQ: %s\n", strerror(error));
	return error != 0 ? 1 : 0;
}

int
main(int argc, char **argv)
{
	return iq_run(argc, argv, stdout, stderr);
}
