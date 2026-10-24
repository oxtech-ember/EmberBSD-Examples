/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD contributors, AI-assisted by OpenAI Codex (GPT-6). */
#ifndef PLUTO_SCAN_H
#define PLUTO_SCAN_H
#include <stddef.h>
#include <stdint.h>

#define SCAN_MIN_HZ UINT64_C(325000000)
#define SCAN_MAX_HZ UINT64_C(3800000000)
#define SCAN_MAX_PEAKS 5
#define SCAN_MAX_SAMPLES 65536

struct scan_plan {
	uint64_t first_hz, last_hz, step_hz;
	uint32_t rate_hz, bandwidth_hz;
	size_t samples;
};
struct scan_peak {
	double frequency_hz, dbfs;
};
struct scan_result {
	double noise_dbfs;
	size_t peak_count;
	struct scan_peak peaks[SCAN_MAX_PEAKS];
};
typedef int (*scan_capture_fn)(void *, uint64_t, int16_t *, size_t);
int scan_plan_check(const struct scan_plan *);
int scan_plan_next(const struct scan_plan *, uint64_t, uint64_t *);
int scan_spectrum(const struct scan_plan *, uint64_t, const int16_t *,
    size_t, struct scan_result *);
int scan_acquire(const struct scan_plan *, uint64_t, scan_capture_fn, void *,
    struct scan_result *);
#endif
