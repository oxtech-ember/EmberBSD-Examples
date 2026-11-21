#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compare the complete scanlines for initial, outside-tap, hidden and restored states."""
import sys
from pathlib import Path


def runs(path):
    for line in Path(path).read_text().splitlines():
        state, y, left, right, color = line.split()
        yield (int(state), int(y), int(left), int(right)), int(color, 16)


def channels(color):
    return color >> 11, (color >> 5) & 63, color & 31


pixels = 0
captures = [[], [], [], []]
for (region, actual), (reference, expected) in zip(runs(sys.argv[1]), runs(sys.argv[2]), strict=True):
    if region != reference:
        raise SystemExit(f"Different run bounds: Runtime {region}, Qt {reference}")
    state, y, left, right = region
    blended = state != 2 and 370 <= y < 470 and 120 <= left < right <= 220
    # Qt blends QColor before conversion. Runtime blends packed RGB565 inputs.
    tolerance = int(blended)
    if any(abs(a - b) > tolerance for a, b in zip(channels(actual), channels(expected))):
        raise SystemExit(f"Different color at {region}: Runtime {actual:04x}, Qt {expected:04x}")
    captures[state].append((y, left, right, actual))
    pixels += right - left
if pixels != 4 * 320 * 480:
    raise SystemExit(f"Incomplete captures: {pixels} pixels")
shown, outside, hidden, restored = captures
if not shown == outside == restored or shown == hidden:
    raise SystemExit("The outside tap, hide or restore produced an unexpected frame")
print("PASS: four 320x480 Qt/Runtime frames, identical color boundaries, outside tap, hide and restore")
