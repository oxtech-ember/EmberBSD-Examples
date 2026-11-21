#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compare the sampled pointer direction and the printed face with the live Qt reference."""
from pathlib import Path
import sys


def ppm(path, width, height):
    # Both writers produce uncompressed binary PPM with no comments.
    magic, dimensions, maximum, data = path.read_bytes().split(b"\n", 3)
    if (magic, dimensions, maximum) != (b"P6", f"{width} {height}".encode(), b"255"):
        raise ValueError(f"{path}: unexpected PPM header")
    if len(data) != width * height * 3:
        raise ValueError(f"{path}: truncated raster")
    return data


def rgb(data, width, x, y):
    start = (width * y + x) * 3
    return data[start:start + 3]


def red(color):
    r, g, b = color
    return r > g + 45 and r > b + 45


def main(directory):
    first = None
    labels = {}
    for i, level in enumerate((0, 100, 25, 75, 50, 0)):
        actual = ppm(directory / f"dial{i}.ppm", 352, 376)
        original = ppm(directory / f"original{level}.ppm", 210, 210)
        if i == 0:
            first = actual
        if i == 5 and actual != first:
            raise ValueError("return to zero did not restore the initial frame")
        error = samples = both_red = any_red = 0
        for y in range(210):
            for x in range(210):
                radius2 = (x - 103)**2 + (y - 98)**2
                if radius2 > 83**2:
                    continue  # skip translucent outer rim over different panel backgrounds
                a, b = rgb(actual, 352, x + 71, y + 57), rgb(original, 210, x, y)
                error += sum(abs(u - v) for u, v in zip(a, b))
                samples += 3
                if radius2 < 58**2:
                    both_red += red(a) and red(b)
                    any_red += red(a) or red(b)
        if error / samples > 8 or both_red < 45 or both_red / any_red < .70:
            raise ValueError(f"{level}%: face error {error/samples:.2f}; needle overlap {both_red}/{any_red}")
        for index in range(5):
            marker = rgb(actual, 352, 52 + index * 62, 357)
            if (marker[0] > 235 and marker[1] > 130 and marker[2] < 100) != (index == level // 25):
                raise ValueError(f"{level}%: incorrect highlighted tap target {index}")
        labels[level] = b"".join(rgb(actual, 352, x, y) for y in range(17, 36) for x in range(255, 348))
        print(f"{level:3d}%: mean face channel error {error/samples:.2f}/255; needle mask {both_red}/{any_red}")
    if len(set(labels.values())) != 5:
        raise ValueError("five settings must produce five distinct readouts")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
