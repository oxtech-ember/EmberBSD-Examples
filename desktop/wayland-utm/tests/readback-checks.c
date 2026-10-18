/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD contributors (AI-assisted). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../egl-readback-checks.h"

int
main(void)
{
	unsigned char pixels[SIDE * SIDE * 4];
	const char *device = "PowerVR B-Series BXM-4-64 MC1";
	size_t x, y, i, bad = 0;

	assert(readback_zink_renderer("zink Vulkan 1.2(PowerVR B-Series BXM-4-64 MC1)", device));
	assert(!readback_zink_renderer("PowerVR B-Series BXM-4-64 MC1", device));
	assert(!readback_zink_renderer("zink (another GPU)", device));
	assert(!readback_zink_renderer("zink (PowerVR B-Series BXM-4-64 MC1 llvmpipe)", device));
	assert(!readback_zink_renderer("zink (PowerVR B-Series BXM-4-64 MC1 SOFTPIPE)", device));
	assert(!readback_zink_renderer("zink (PowerVR B-Series BXM-4-64 MC1 lavapipe)", device));
	assert(!readback_zink_renderer("zink (PowerVR B-Series BXM-4-64 MC1 SwiftShader)", device));
	assert(!readback_zink_renderer(NULL, device));
	assert(!readback_zink_renderer("zink (PowerVR)", ""));
	/* Independent frame-zero image: red/green below blue/white. */
	for (y = 0; y < SIDE; y++) {
		for (x = 0; x < SIDE; x++) {
			i = (y * SIDE + x) * 4;
			pixels[i] = (y < SIDE / 2 ? x < SIDE / 2 : x >= SIDE / 2) ? 255 : 0;
			pixels[i + 1] = x >= SIDE / 2 ? 255 : 0;
			pixels[i + 2] = y >= SIDE / 2 ? 255 : 0;
			pixels[i + 3] = 255;
		}
	}
	assert(readback_pixels(pixels, 0, &bad));
	assert(!readback_pixels(pixels, 1, &bad));
	/* Detect a stale frame, a missing transfer, and one corrupted channel. */
	pixels[4 * (SIDE * SIDE - 1) + 3] = 0;
	assert(!readback_pixels(pixels, 0, &bad));
	assert(bad == 4 * (SIDE * SIDE - 1) + 3);
	memset(pixels, 0x5a, sizeof(pixels));
	assert(!readback_pixels(pixels, 0, &bad));
	puts("PASS: renderer and independent pixel rejection contracts");
	return 0;
}
