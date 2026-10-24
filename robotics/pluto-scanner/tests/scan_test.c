/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD contributors, AI-assisted by OpenAI Codex (GPT-6). */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "scan.h"

static const struct scan_plan valid = {
	325000000, 3800000000ULL, 3000000, 10000000, 8000000, 4096
};
static int16_t iq[2 * 4096];

static void
tone(int bin)
{
	const double pi = acos(-1.0);
	for (size_t i = 0; i < 4096; i++) {
		double phase = 2.0 * pi * bin * (double)i / 4096.0;
		/* Preserve raw low-12-bit encoding, including negative samples. */
		iq[2 * i] = (int16_t)((uint16_t)(int16_t)lrint(1024 * cos(phase)) & 4095);
		iq[2 * i + 1] = (int16_t)((uint16_t)(int16_t)lrint(1024 * sin(phase)) & 4095);
	}
}

static int
capture_failed(void *cookie, uint64_t center, int16_t *data, size_t count)
{
	(void)cookie; (void)center; (void)data; (void)count;
	return ETIMEDOUT;
}

static int
capture_tone(void *cookie, uint64_t center, int16_t *data, size_t count)
{
	(void)cookie; (void)center;
	assert(count == 4096);
	memcpy(data, iq, sizeof(iq));
	return 0;
}

static void
coverage(void)
{
	struct scan_plan p = valid;
	p.last_hz = p.first_hz + 13000000;
	/* An ideal RF filter removes tones outside the capture passband. */
	for (uint64_t carrier = p.first_hz; carrier <= p.last_hz;) {
		int detected = 0;
		for (uint64_t center = p.first_hz;;) {
			double offset = (double)carrier - (double)center;
			for (size_t i = 0; i < p.samples; i++) {
				double a = fabs(offset) < p.bandwidth_hz / 2.0 ? 1024 : 0;
				double phase = 2 * acos(-1.0) * offset * (double)i / p.rate_hz;
				iq[2*i] = (int16_t)lrint(a * cos(phase));
				iq[2*i+1] = (int16_t)lrint(a * sin(phase));
			}
			struct scan_result result;
			assert(scan_spectrum(&p, center, iq, p.samples, &result) == 0);
			for (size_t j = 0; j < result.peak_count; j++)
				if (fabs(result.peaks[j].frequency_hz - (double)carrier) <
				    (double)p.rate_hz / (double)p.samples)
					detected = 1;
			uint64_t next;
			if (scan_plan_next(&p, center, &next) == ENOENT) break;
			center = next;
		}
		assert(detected); /* Include both band edges and every tune center. */
		uint64_t next;
		if (scan_plan_next(&p, carrier, &next) == ENOENT) break;
		carrier = next;
	}
}

int
main(void)
{
	struct scan_plan p = valid;
	struct scan_result r;
	uint64_t center = p.first_hz, next;
	unsigned centers = 1;
	assert(scan_plan_check(&p) == 0);
	while (scan_plan_next(&p, center, &next) == 0) {
		assert(next > center && next - center <= p.step_hz);
		center = next;
		centers++;
	}
	assert(center == p.last_hz && centers == 1160);
	p.first_hz = 70000000;
	p.last_hz = 6000000000ULL;
	assert(scan_plan_check(&p) == 0);
	assert(scan_plan_next(&p, 5999000000ULL, &next) == 0);
	assert(next == 6000000000ULL);
	p.first_hz = 69999999;
	assert(scan_plan_check(&p) == EINVAL);
	p = valid;
	p.step_hz = 9000000; assert(scan_plan_check(&p) == EINVAL);
	p = valid; p.samples = 4095; assert(scan_plan_check(&p) == EINVAL);
	p = valid; p.samples = SCAN_MAX_SAMPLES + 1; assert(scan_plan_check(&p) == EINVAL);
	p = valid; p.first_hz = 69999999; assert(scan_plan_check(&p) == EINVAL);
	p = valid; p.rate_hz = 0; assert(scan_plan_check(&p) == EINVAL);
	p = valid; p.first_hz = p.last_hz;
	assert(scan_plan_next(&p, p.first_hz, &next) == EINVAL);
	p = valid;
	memset(&r, 0x55, sizeof(r));
	assert(scan_spectrum(&p, 1000000000, iq, 0, &r) == EINVAL);
	assert(r.peak_count == 0);
	assert(scan_spectrum(&p, 1000000000, iq, 4096, &r) == 0);
	assert(r.peak_count == 0 && isfinite(r.noise_dbfs));
	for (int bin = -400; bin <= 400; bin += 800) {
		tone(bin);
		assert(scan_spectrum(&p, 1000000000, iq, 4096, &r) == 0);
		assert(r.peak_count > 0 && r.peak_count <= SCAN_MAX_PEAKS);
		assert(fabs(r.peaks[0].frequency_hz - (1e9 + bin * 1e7 / 4096)) < 0.01);
		assert(fabs(r.peaks[0].dbfs + 6.020599913) < 0.02);
	}
	assert(scan_acquire(&p, 1000000000, capture_tone, NULL, &r) == 0);
	assert(r.peak_count > 0);
	assert(scan_acquire(&p, 1000000000, capture_failed, NULL, &r) == ETIMEDOUT);
	assert(r.peak_count == 0); /* A failed capture must not publish old IQ. */
	tone(-400);
	assert(scan_spectrum(&p, p.first_hz, iq, 4096, &r) == 0);
	for (size_t i = 0; i < r.peak_count; i++)
		assert(r.peaks[i].frequency_hz >= (double)p.first_hz);
	assert(scan_spectrum(&p, p.last_hz + 1, iq, 4096, &r) == EINVAL);
	assert(r.peak_count == 0);
	coverage();
	puts("PASS sweep bounds, overlap, signed 12-bit IQ, Hann FFT, levels, and stale capture refusal");
	return 0;
}
