/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD contributors, AI-assisted by OpenAI Codex (GPT-6). */
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "scan.h"

int
scan_plan_check(const struct scan_plan *p)
{
	if (p == NULL || p->first_hz < SCAN_MIN_HZ ||
	    p->last_hz > SCAN_MAX_HZ || p->first_hz > p->last_hz ||
	    p->rate_hz == 0 || p->bandwidth_hz == 0 ||
	    p->bandwidth_hz > p->rate_hz || p->step_hz == 0 ||
	    p->step_hz > p->bandwidth_hz || p->samples < 16 ||
	    p->samples > SCAN_MAX_SAMPLES ||
	    (p->samples & (p->samples - 1)) != 0)
		return EINVAL;
	/* Adjacent passbands must cover each other's rejected DC bins. */
	if (2 * p->step_hz + 4 * (uint64_t)p->rate_hz / p->samples >= p->bandwidth_hz ||
	    (p->last_hz - p->first_hz) * p->samples <= 2 * (uint64_t)p->rate_hz)
		return EINVAL;
	return 0;
}

int
scan_plan_next(const struct scan_plan *p, uint64_t current, uint64_t *next)
{
	if (next == NULL || scan_plan_check(p) != 0 ||
	    current < p->first_hz || current > p->last_hz)
		return EINVAL;
	if (current == p->last_hz)
		return ENOENT;
	*next = p->last_hz - current <= p->step_hz ? p->last_hz :
	    current + p->step_hz;
	return 0;
}

static int
signed12(int16_t sample)
{
	unsigned value = (uint16_t)sample & 4095U;
	return value >= 2048 ? (int)value - 4096 : (int)value;
}

static void
fft(double *re, double *im, size_t n)
{
	for (size_t i = 1, j = 0; i < n; i++) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j) {
			double t = re[i]; re[i] = re[j]; re[j] = t;
			t = im[i]; im[i] = im[j]; im[j] = t;
		}
	}
	for (size_t length = 2; length <= n; length <<= 1) {
		double angle = -2 * acos(-1.0) / (double)length;
		double step_re = cos(angle), step_im = sin(angle);
		double wr = 1, wi = 0;
		for (size_t j = 0; j < length / 2; j++) {
			for (size_t i = j; i < n; i += length) {
				size_t k = i + length / 2;
				double tr = wr * re[k] - wi * im[k];
				double ti = wr * im[k] + wi * re[k];
				re[k] = re[i] - tr; im[k] = im[i] - ti;
				re[i] += tr; im[i] += ti;
			}
			double next_wr = wr * step_re - wi * step_im;
			wi = wr * step_im + wi * step_re;
			wr = next_wr;
		}
	}
}

static int
compare_power(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

static double
dbfs(double power)
{
	return 10 * log10(fmax(power, 1e-18));
}

static double
bin_offset(size_t bin, size_t n, uint32_t rate)
{
	double signed_bin = bin < n / 2 ? (double)bin :
	    (double)bin - (double)n;
	return signed_bin * rate / (double)n;
}

static int
valid_bin(const struct scan_plan *p, uint64_t center, size_t bin)
{
	double offset = bin_offset(bin, p->samples, p->rate_hz);
	double frequency = (double)center + offset;
	double half_bin = (double)p->rate_hz / (2.0 * (double)p->samples);
	/* Reject the DC bin and its immediate Hann neighbors. */
	return fabs(offset) > (double)p->rate_hz / (double)p->samples &&
	    fabs(offset) < p->bandwidth_hz / 2.0 &&
	    frequency >= (double)p->first_hz - half_bin &&
	    frequency <= (double)p->last_hz + half_bin;
}

int
scan_spectrum(const struct scan_plan *p, uint64_t center, const int16_t *iq,
    size_t count, struct scan_result *result)
{
	double *storage, *re, *im, *power, *noise, sum = 0;
	size_t n, used = 0, selected[SCAN_MAX_PEAKS];
	if (result == NULL)
		return EINVAL;
	memset(result, 0, sizeof(*result));
	if (scan_plan_check(p) != 0 || iq == NULL || count != p->samples ||
	    center < p->first_hz || center > p->last_hz)
		return EINVAL;
	n = p->samples;
	storage = calloc(4 * n, sizeof(double));
	if (storage == NULL)
		return ENOMEM;
	re = storage; im = re + n; power = im + n; noise = power + n;
	for (size_t i = 0; i < n; i++) {
		double w = 0.5 - 0.5 * cos(2 * acos(-1.0) * (double)i / (double)n);
		re[i] = signed12(iq[2 * i]) * w;
		im[i] = signed12(iq[2 * i + 1]) * w;
		sum += w;
	}
	fft(re, im, n);
	for (size_t i = 0; i < n; i++) {
		power[i] = (re[i] * re[i] + im[i] * im[i]) /
		    (sum * sum * 2048.0 * 2048.0);
		if (valid_bin(p, center, i))
			noise[used++] = power[i];
	}
	qsort(noise, used, sizeof(double), compare_power);
	double floor = used == 0 ? 0 : noise[used / 2];
	result->noise_dbfs = dbfs(floor);
	for (size_t j = 0; j < SCAN_MAX_PEAKS; j++) {
		size_t best = n;
		for (size_t i = 0; i < n; i++) {
			if (!valid_bin(p, center, i) ||
			    power[i] <= fmax(floor * 10, 1e-18) ||
			    power[i] <= power[(i + n - 1) % n] ||
			    power[i] < power[(i + 1) % n])
				continue;
			int separated = 1;
			for (size_t k = 0; k < j; k++) {
				size_t distance = i > selected[k] ? i - selected[k] : selected[k] - i;
				if (distance < 3 || n - distance < 3)
					separated = 0;
			}
			if (separated && (best == n || power[i] > power[best]))
				best = i;
		}
		if (best == n)
			break;
		selected[j] = best;
		result->peaks[j].frequency_hz = fmin((double)p->last_hz,
		    fmax((double)p->first_hz, (double)center + bin_offset(best, n, p->rate_hz)));
		result->peaks[j].dbfs = dbfs(power[best]);
		result->peak_count++;
	}
	free(storage);
	return 0;
}

int
scan_acquire(const struct scan_plan *p, uint64_t center,
    scan_capture_fn capture, void *cookie, struct scan_result *result)
{
	int error;
	int16_t *iq;
	if (result == NULL)
		return EINVAL;
	memset(result, 0, sizeof(*result));
	if (scan_plan_check(p) != 0 || capture == NULL ||
	    center < p->first_hz || center > p->last_hz)
		return EINVAL;
	iq = calloc(2 * p->samples, sizeof(*iq));
	if (iq == NULL)
		return ENOMEM;
	error = capture(cookie, center, iq, p->samples);
	if (error == 0)
		error = scan_spectrum(p, center, iq, p->samples, result);
	free(iq);
	return error;
}
