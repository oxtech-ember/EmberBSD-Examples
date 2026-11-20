#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Turn the Qt-rendered PPM into runs of native RGB565 pixels per scanline."""
from argparse import ArgumentParser
from itertools import groupby
from pathlib import Path


def encode(ppm):
    # bmptopnm writes an unadorned binary PPM with maxval 255.
    magic, dimensions, maxval, pixels = ppm.split(b'\n', 3)
    if (magic, dimensions, maxval) != (b'P6', b'480 480', b'255'):
        raise ValueError('expected a 480x480 8-bit RGB PPM from bmptopnm')
    if len(pixels) != 480 * 480 * 3:
        raise ValueError('incomplete RGB frame')
    for y in range(480):
        start = y * 480 * 3
        row = iter(pixels[start:start + 480 * 3])
        colors = ((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)
                  for r, g, b in zip(row, row, row))
        for color, group in groupby(colors):
            yield sum(1 for _ in group), color


if __name__ == '__main__':
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('ppm', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_text(''.join(f'\t{{ {length}, 0x{color:04x} }},\n'
                                   for length, color in encode(args.ppm.read_bytes())))
