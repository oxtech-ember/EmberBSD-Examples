/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD contributors (AI-assisted). */
#include <stdio.h>
#include <string.h>
#include "../egl-readback-checks.h"
#define CHECK(expression) do { if (!(expression)) { \
	fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #expression); \
	return 1; } } while (0)

int
main(void)
{
	unsigned char pixels[SIDE * SIDE * 4];
	const char *device = "PowerVR B-Series BXM-4-64 MC1";
	size_t x, y, bad = 0;

	CHECK(readback_zink_renderer("zink Vulkan 1.2(PowerVR B-Series BXM-4-64 MC1)", device));
	CHECK(!readback_zink_renderer("PowerVR B-Series BXM-4-64 MC1", device));
	CHECK(!readback_zink_renderer("zink (another GPU)", device));
	CHECK(!readback_zink_renderer("zink (PowerVR B-Series BXM-4-64 MC1 llvmpipe)", device));
	CHECK(!readback_zink_renderer("zink (PowerVR B-Series BXM-4-64 MC1 SOFTPIPE)", device));
	CHECK(!readback_zink_renderer("zink (PowerVR B-Series BXM-4-64 MC1 lavapipe)", device));
	CHECK(!readback_zink_renderer("zink (PowerVR B-Series BXM-4-64 MC1 SwiftShader)", device));
	CHECK(!readback_zink_renderer(NULL, device));
	CHECK(!readback_zink_renderer("zink (PowerVR)", ""));
	/* Independent frame-zero image: red/green below blue/white. */
	for (y = 0; y < SIDE; y++) {
		for (x = 0; x < SIDE; x++) {
			size_t i = (y * SIDE + x) * 4;
			pixels[i] = (y < SIDE / 2 ? x < SIDE / 2 : x >= SIDE / 2) ? 255 : 0;
			pixels[i + 1] = x >= SIDE / 2 ? 255 : 0;
			pixels[i + 2] = y >= SIDE / 2 ? 255 : 0;
			pixels[i + 3] = 255;
		}
	}
	CHECK(readback_pixels(pixels, 0, &bad));
	CHECK(!readback_pixels(pixels, 1, &bad));
	/* Detect a stale frame, a missing transfer, and one corrupted channel. */
	pixels[4 * (SIDE * SIDE - 1) + 3] = 0;
	CHECK(!readback_pixels(pixels, 0, &bad));
	CHECK(bad == 4 * (SIDE * SIDE - 1) + 3);
	memset(pixels, 0x5a, sizeof(pixels));
	CHECK(!readback_pixels(pixels, 0, &bad));
	puts("PASS: renderer and independent pixel rejection contracts");
	return 0;
}
