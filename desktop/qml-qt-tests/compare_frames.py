#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
import hashlib
from pathlib import Path
import struct
import sys

if len(sys.argv) != 6:
    raise SystemExit("usage: compare_frames.py scene runtime.rgb565 qt.rgb565 width height")
scene, runtime_path, qt_path = sys.argv[1:4]
width, height = map(int, sys.argv[4:6])
runtime = Path(runtime_path).read_bytes()
qt = Path(qt_path).read_bytes()
expected_size = width * height * 2
if len(runtime) != expected_size or len(qt) != expected_size:
    raise SystemExit(f"FAIL: expected {expected_size} bytes per frame, got runtime={len(runtime)} qt={len(qt)}")
if runtime != qt:
    offset = next(i for i, (a, b) in enumerate(zip(runtime, qt)) if a != b)
    x, y = (offset // 2) % width, (offset // 2) // width
    got = struct.unpack_from("<H", runtime, offset & ~1)[0]
    expected = struct.unpack_from("<H", qt, offset & ~1)[0]
    raise SystemExit(f"FAIL: {scene} differs from Qt at ({x},{y}): runtime=0x{got:04x}, Qt=0x{expected:04x}")

def pixel(data, x, y):
    return struct.unpack_from("<H", data, (y * width + x) * 2)[0]

def rgb565(value):
    red, green, blue = (int(value[index:index + 2], 16) for index in (1, 3, 5))
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)

background = rgb565("#272822")
checks = {"visualparent": {
    "background": (300, 50, background), "green": (20, 100, rgb565("#008000")),
    "blue on top": (100, 200, rgb565("#0000ff")),
}, "visualparent2": {
    "background": (300, 50, background), "green": (20, 100, rgb565("#008000")),
    "unclipped nested red": (300, 150, rgb565("#ff0000")),
    "blue over nested red": (220, 220, rgb565("#0000ff")),
}, "grid-spacing": {
    "background gap": (56, 56, rgb565("#303030")),
    "red tile": (28, 28, rgb565("#aa6666")),
    "yellow tile": (84, 28, rgb565("#aaaa66")),
    "gray tile": (28, 84, rgb565("#9999aa")),
    "blue tile": (84, 84, rgb565("#6666aa")),
}}
for label, (x, y, value) in checks[scene].items():
    actual = pixel(runtime, x, y)
    if actual != value:
        raise SystemExit(f"FAIL: {scene} {label} at ({x},{y}): RGB565 0x{actual:04x}, expected 0x{value:04x}")
digest = hashlib.sha256(runtime).hexdigest()
print(f"PASS: {scene}: every RGB565 pixel matches Qt; probes passed; size={width}x{height}; bytes={len(runtime)} sha256={digest}")
