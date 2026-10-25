/* SPDX-License-Identifier: BSD-2-Clause */
#include "native_adc.h"
#include <sys/ioctl.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int
control(int fd, unsigned long op, void *p)
{
	return ioctl(fd, op, p);
}

static ssize_t
input(int fd, void *b, size_t n, unsigned timeout)
{
	struct pollfd p = { .fd = fd, .events = POLLIN };
	int ready = poll(&p, 1, (int)timeout);
	if (ready < 0) return -1;
	if (!ready) { errno = ETIMEDOUT; return -1; }
	if (p.revents & (POLLNVAL | POLLHUP)) { errno = ENODEV; return -1; }
	return read(fd, b, n);
}

int
rx_adc_settings(void *v, unsigned timeout, unsigned blocks)
{
	struct rx_adc *a = v;
	if (timeout > 60000 || (blocks && (blocks < 3 || blocks > 16))) return -EINVAL;
	if (blocks && a->running) return -EBUSY;
	if (timeout) a->timeout_ms = timeout;
	if (blocks) a->blocks = blocks;
	return 0;
}

int
rx_adc_init(struct rx_adc *a, int fd)
{
	memset(a, 0, sizeof(*a));
	a->fd = fd;
	a->control = control;
	a->input = input;
	a->timeout_ms = 2000;
	a->blocks = 16;
	a->config = (struct adi_axi_adc_config) {
		.abi_version = ADI_AXI_ADC_ABI_VERSION,
		.struct_size = sizeof(a->config),
		.frequency_hz = 1090000000, .sample_rate_hz = 2400000,
		.bandwidth_hz = 3000000, .rx_mask = ADI_AXI_ADC_RX1_MASK,
		.channel_mask = ADI_AXI_ADC_RX1_IQ_CHANNEL_MASK,
		.gain_mode = { ADI_AXI_ADC_GAIN_SLOW, ADI_AXI_ADC_GAIN_SLOW },
		.timeout_ms = 2000
	};
	return 0;
}

static int
status(struct rx_adc *a)
{
	memset(&a->status, 0, sizeof(a->status));
	a->status.abi_version = ADI_AXI_ADC_STREAM_ABI_VERSION;
	a->status.struct_size = sizeof(a->status);
	if (a->control(a->fd, ADI_AXI_ADCIOC_STREAM_STATUS, &a->status) < 0)
		return -errno;
	if (a->status.abi_version != ADI_AXI_ADC_STREAM_ABI_VERSION ||
	    a->status.struct_size != sizeof(a->status)) return -EPROTO;
	return 0;
}

int
rx_adc_stop(void *v)
{
	struct rx_adc *a = v;
	if (a->control(a->fd, ADI_AXI_ADCIOC_STREAM_STOP, NULL) < 0)
		return -errno;
	a->running = 0;
	return 0;
}

int
rx_adc_start(void *v, unsigned samples)
{
	struct rx_adc *a = v;
	if (samples < 2 || samples > 1048576 || (samples & 1)) return -EINVAL;
	if (a->running) return -EBUSY;
	a->stream = (struct adi_axi_adc_stream_config) {
		.abi_version = ADI_AXI_ADC_STREAM_ABI_VERSION,
		.struct_size = sizeof(a->stream), .block_count = a->blocks,
		.block_bytes = samples > 65536 ? 262144 : samples * 4
	};
	if (a->control(a->fd, ADI_AXI_ADCIOC_STREAM_START, &a->stream) < 0)
		return -errno;
	a->running = 1;
	return 0;
}

/* Query on the existing ADC descriptor only. A failed ioctl has no outputs. */
static ssize_t
read_error(struct rx_adc *a, int error)
{
	int saved = errno;
	if (a->error_diagnostics < 8) {
		a->error_diagnostics++;
		struct adi_axi_adc_stream_status d = {
			.abi_version = ADI_AXI_ADC_STREAM_ABI_VERSION, .struct_size = sizeof(d)
		};
		int e = a->control(a->fd, ADI_AXI_ADCIOC_STREAM_STATUS, &d) < 0 ? errno : 0;
		if (!e && (d.abi_version != ADI_AXI_ADC_STREAM_ABI_VERSION || d.struct_size != sizeof(d))) e = EPROTO;
		fprintf(stderr, "pluto-iiod: adc-read error=%d", error);
		if (e) fprintf(stderr, " status_error=%d\n", e);
		else fprintf(stderr, " last_error=%d flags=%#x generation=%" PRIu64 " sequence=%" PRIu64
		    " completed_blocks=%" PRIu64 " completed_bytes=%" PRIu64
		    " discontinuities=%" PRIu64 " dropped_blocks=%" PRIu64
		    " block_bytes=%u block_count=%u block_offset=%u ready_blocks=%u sample_rate_hz=%u\n",
		    d.last_error, d.flags, d.generation, d.sequence, d.completed_blocks, d.completed_bytes,
		    d.discontinuities, d.dropped_blocks, d.block_bytes, d.block_count, d.block_offset,
		    d.ready_blocks, d.sample_rate_hz);
		fflush(stderr);
	}
	errno = saved;
	return -error;
}

ssize_t
rx_adc_read(void *v, void *b, size_t n)
{
	struct rx_adc *a = v;
	ssize_t got;
	int e;
	if (!a->running) return read_error(a, EBADF);
	got = a->input(a->fd, b, n, a->timeout_ms);
	if (got < 0) return read_error(a, errno);
	/* Discard this owned copy if fresh status identifies a loss. */
	if ((e = status(a)) != 0) return read_error(a, -e);
	if (a->status.last_error) return read_error(a, a->status.last_error);
	if (a->status.discontinuities || (a->status.flags & ADI_AXI_ADC_STREAM_LOSS_UNKNOWN))
		return read_error(a, EOVERFLOW);
	return got;
}

static int
integer(const char *b, uint64_t *out)
{
	char *end;
	if (!*b) return -EINVAL;
	for (const char *q = b; *q; q++) if (*q < '0' || *q > '9') return -EINVAL;
	errno = 0; *out = strtoull(b, &end, 10);
	return errno || *end ? -ERANGE : 0;
}

int
rx_adc_attr(void *v, const char *dev, const char *ch, const char *name,
    char *b, size_t cap, int write)
{
	struct rx_adc *a = v;
	struct adi_axi_adc_config next = a->config;
	uint64_t n = 0;
	int kind = 0, e;
	if (!strcmp(dev, "iio:device0")) {
		if (!strcmp(ch, "INPUT:voltage0")) {
			if (!strcmp(name, "sampling_frequency")) kind = 1;
			else if (!strcmp(name, "rf_bandwidth")) kind = 2;
			else if (!strcmp(name, "rf_port_select")) kind = 3;
			else if (!strcmp(name, "gain_control_mode")) kind = 4;
			else if (!strcmp(name, "hardwaregain")) kind = 5;
			else if (!strcmp(name, "filter_fir_en")) kind = 7;
		} else if (!strcmp(ch, "OUTPUT:altvoltage0") && !strcmp(name, "frequency")) kind = 6;
		else if (!strcmp(ch, "OUTPUT:altvoltage0") && !strcmp(name, "powerdown")) kind = 11;
		else if (!strcmp(ch, "OUTPUT:altvoltage1") && !strcmp(name, "powerdown")) kind = 12;
		else if (!*ch && !strcmp(name, "in_out_voltage_filter_fir_en")) kind = 7;
		else if (!*ch && !strcmp(name, "filter_fir_config")) return -EOPNOTSUPP;
	} else if (!strcmp(dev, "iio:device1")) {
		if ((!*ch || !strcmp(ch, "INPUT:voltage0") || !strcmp(ch, "INPUT:voltage1")) &&
		    !strcmp(name, "sampling_frequency")) kind = 1;
		else if (!*ch && !strcmp(name, "rx_discontinuities")) kind = 8;
		else if (!*ch && !strcmp(name, "rx_dropped_blocks")) kind = 9;
		else if (!*ch && !strcmp(name, "rx_loss_unknown")) kind = 10;
	}
	if (!kind) return -ENOENT;
	if (!write) {
		if (kind == 1 || (kind >= 8 && kind <= 10)) {
			if ((e = status(a)) != 0) return e;
			if (kind == 1 && !a->status.sample_rate_hz) return -ENODATA;
			n = kind == 1 ? a->status.sample_rate_hz : kind == 8 ? a->status.discontinuities :
			    kind == 9 ? a->status.dropped_blocks : !!(a->status.flags & ADI_AXI_ADC_STREAM_LOSS_UNKNOWN);
			return snprintf(b, cap, "%" PRIu64, n);
		}
		if (!a->configured) return -ENODATA;
		switch (kind) {
		case 2: return snprintf(b, cap, "%u", a->config.bandwidth_hz);
		case 3: return snprintf(b, cap, "%s", a->config.rf_port ? "B_BALANCED" : "A_BALANCED");
		case 4: return snprintf(b, cap, "%s", a->config.gain_mode[0] == ADI_AXI_ADC_GAIN_MANUAL ?
		    "manual" : a->config.gain_mode[0] == ADI_AXI_ADC_GAIN_SLOW ? "slow_attack" : "fast_attack");
		case 5: {
			struct adi_axi_adc_status st = { .abi_version = ADI_AXI_ADC_ABI_VERSION, .struct_size = sizeof(st) };
			if (a->control(a->fd, ADI_AXI_ADCIOC_STATUS, &st) < 0) return -errno;
			if (!(st.valid_mask & 1)) return -ENODATA;
			return snprintf(b, cap, "%.3f dB", st.gain_mdB[0] / 1000.0);
		}
		case 6: return snprintf(b, cap, "%" PRIu64, a->config.frequency_hz);
		case 12: return snprintf(b, cap, "1"); /* Enforced RX-only policy, not a hardware measurement. */
		default: return snprintf(b, cap, "0");
		}
	}
	if (kind >= 8 && kind <= 10) return -EACCES;
	if (kind == 11) return !strcmp(b, "0") && a->configured ? 0 : -EACCES;
	if (kind == 12) return !strcmp(b, "1") && a->configured ? 0 : -EACCES;
	if (kind == 7) return !strcmp(b, "0") ? 0 : -EOPNOTSUPP;
	if (kind == 3) {
		if (!strcmp(b, "A_BALANCED")) next.rf_port = 0;
		else if (!strcmp(b, "B_BALANCED")) next.rf_port = 1;
		else return -EINVAL;
	} else if (kind == 4) {
		if (!strcmp(b, "manual")) next.gain_mode[0] = ADI_AXI_ADC_GAIN_MANUAL;
		else if (!strcmp(b, "slow_attack")) next.gain_mode[0] = ADI_AXI_ADC_GAIN_SLOW;
		else if (!strcmp(b, "fast_attack")) next.gain_mode[0] = ADI_AXI_ADC_GAIN_FAST;
		else return -EINVAL;
	} else if (kind == 5) {
		char *end; double gain;
		errno = 0; gain = strtod(b, &end);
		if (!*b || *end || errno || !isfinite(gain) || gain < -1 || gain > 73) return -ERANGE;
		next.gain_mdB[0] = (int32_t)round(gain * 1000);
	} else {
		if ((e = integer(b, &n)) != 0) return e;
		if (kind == 1) {
			if (a->configured) {
				/* Fresh kernel status qualifies an actual-rate roundtrip.
				 * Keep its original request, including quantized edges.
				 */
				if ((e = status(a)) != 0) return e;
				if (n != 0 && n == a->status.sample_rate_hz) {
					const struct adi_axi_adc_stream_status *s = &a->status;
					if (s->last_error < 0) return -EPROTO;
					if (s->last_error) return -s->last_error;
					if (s->discontinuities || s->dropped_blocks ||
					    (s->flags & ADI_AXI_ADC_STREAM_LOSS_UNKNOWN))
						return -EOVERFLOW;
					if ((s->flags & ~ADI_AXI_ADC_STREAM_RUNNING) ||
					    !!(s->flags & ADI_AXI_ADC_STREAM_RUNNING) != !!a->running ||
					    s->reserved[0] || s->reserved[1] || s->reserved[2])
						return -EPROTO;
					return 0;
				}
			}
			if (n < 2083334 || n > 61440000) return -ERANGE;
			next.sample_rate_hz = (uint32_t)n;
		} else if (kind == 2) {
			if (!n || n > 20000000) return -ERANGE;
			next.bandwidth_hz = (uint32_t)n;
		} else {
			if (n < 70000000 || n > 6000000000ULL) return -ERANGE;
			next.frequency_hz = n;
		}
	}
	int running = a->running;
	unsigned samples = a->stream.block_bytes / 4;
	if (running && (e = rx_adc_stop(a)) != 0) return e;
	a->configured = 0;
	if (a->control(a->fd, ADI_AXI_ADCIOC_CONFIG, &next) < 0) return -errno;
	a->config = next;
	if ((e = status(a)) != 0) return e;
	if (!a->status.sample_rate_hz) return -ENODATA;
	a->configured = 1;
	return running ? rx_adc_start(a, samples) : 0;
}
