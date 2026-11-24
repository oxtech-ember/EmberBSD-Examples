#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Compute CPU contour frames and score visible T-LESS boundaries."""

import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess
import sys


PANEL_SIZE = (720, 540)
PANEL_ROI = (190, 95, 540, 445)
WORK_RASTER = (96, 96)
WORK_MASK_FILTER = "format=gray,crop=350:350:190:95,scale=96:96:flags=area"


def panel_regions(gray):
    width, _ = PANEL_SIZE
    left, top, right, bottom = PANEL_ROI
    return [gray[y * width + left:y * width + right] for y in range(top, bottom)]


def panel_threshold(gray):
    histogram = Counter(value for row in panel_regions(gray) for value in row)
    count = sum(histogram.values())
    total = sum(value * population for value, population in histogram.items())
    low_count = low_sum = 0
    best, threshold = -1.0, None
    for value in range(255):
        low_count += histogram[value]
        low_sum += value * histogram[value]
        high_count = count - low_count
        if low_count and high_count:
            score = (low_sum * count - total * low_count) ** 2 / (low_count * high_count)
            if score > best:
                best, threshold = score, value
    if threshold is None:
        raise ValueError("Panel calibration needs two intensity classes")
    return threshold


def prepare_panel(gray, threshold):
    width, _ = PANEL_SIZE
    left, top, right, _ = PANEL_ROI
    lookup = bytes(255 if value > threshold else 0 for value in range(256))
    prepared = bytearray(len(gray))
    for y, row in enumerate(panel_regions(gray), top):
        prepared[y * width + left:y * width + right] = row.translate(lookup)
    return bytes(prepared)


def contour(gray, width, height):
    lookup = bytes((value * 10 + 127) // 255 for value in range(256))
    q = gray.translate(lookup)
    result = bytearray(len(q))
    for y in range(height):
        row = y * width
        below = min(y + 1, height - 1) * width
        for x in range(width):
            p = row + x
            horizontal = q[row + min(x + 1, width - 1)] - q[p]
            vertical = q[below + x] - q[p]
            if abs(horizontal) + abs(vertical) >= 3:
                result[p] = 255
    return result


def read_masks(ffmpeg, scene, frame_id, instances, frame_bytes, work96=False):
    combined = bytearray(frame_bytes)
    for instance in range(instances):
        path = scene / "mask_visib" / f"{frame_id:06d}_{instance:06d}.png"
        command = [ffmpeg, "-nostdin", "-v", "error", "-i", str(path)]
        if work96:
            command.extend(("-vf", WORK_MASK_FILTER))
        command.extend(("-pix_fmt", "gray", "-f", "rawvideo", "-"))
        mask = subprocess.check_output(command)
        if len(mask) != frame_bytes:
            raise ValueError(f"Mask size does not match source frame: {path}")
        cutoff = 127 if work96 else 0
        for pos, value in enumerate(mask):
            if value > cutoff:
                combined[pos] = 1
    return combined


def boundary(mask, width, height):
    return {
        p for p, value in enumerate(mask)
        if (p % width + 1 < width and value != mask[p + 1])
        or (p + width < width * height and value != mask[p + width])
    }


def close_matches(source, target, width, height, radius):
    offsets = [(dx, dy) for dy in range(-radius, radius + 1)
               for dx in range(-radius, radius + 1)
               if dx * dx + dy * dy <= radius * radius]
    matched = 0
    for p in source:
        y, x = divmod(p, width)
        if any(0 <= x + dx < width and 0 <= y + dy < height
               and (y + dy) * width + x + dx in target
               for dx, dy in offsets):
            matched += 1
    return matched


def scores(raw, target, width, height, radius):
    prediction = {p for p, value in enumerate(raw) if value != 0}
    intersection = len(prediction & target)
    union = len(prediction | target)
    precision = (close_matches(prediction, target, width, height, radius)
                 / len(prediction)) if prediction else 0.0
    recall = (close_matches(target, prediction, width, height, radius)
              / len(target)) if target else 0.0
    f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    return {"boundary_iou": intersection / union if union else 1.0,
            "precision": precision, "recall": recall, "f1": f1}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("width", type=int)
    parser.add_argument("height", type=int)
    parser.add_argument("gray", type=Path, help="Headerless 8-bit gray frames")
    parser.add_argument("output", type=Path, help="CPU contour output")
    parser.add_argument("--candidate", type=Path, help="Compare hardware output")
    parser.add_argument("--tless-panel", action="store_true",
                        help="Calibrate against the first frame of T-LESS scene 000001")
    parser.add_argument("--scene", type=Path, help="Optional BOP scene directory")
    parser.add_argument("--tless-work96", action="store_true",
                        help="Score prepared 96x96 panel frames against matching BOP mask crops")
    parser.add_argument("--first-frame-id", type=int, default=0,
                        help="BOP ID of the first frame in a consecutive sequence")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--radius", type=int, default=2)
    args = parser.parse_args()
    width, height = args.width, args.height
    if not (1 <= width <= 4096 and 1 <= height <= 4096 and 0 <= args.radius <= 8):
        parser.error("Use dimensions in 1..4096 and radius in 0..8")
    if args.first_frame_id < 0:
        parser.error("--first-frame-id must be nonnegative")
    if args.tless_panel and (width, height) != PANEL_SIZE:
        parser.error("--tless-panel requires 720x540 grayscale frames")
    if args.tless_work96 and ((width, height) != WORK_RASTER or not args.scene):
        parser.error("--tless-work96 requires 96x96 prepared frames and --scene")
    inputs = {args.gray.resolve()}
    if args.candidate:
        inputs.add(args.candidate.resolve())
    if args.output.resolve() in inputs:
        parser.error("Output must differ from input paths")

    gt = None
    if args.scene:
        gt = json.loads((args.scene / "scene_gt.json").read_text())
    size = width * height
    frames = 0
    threshold = None
    with args.gray.open("rb") as source, args.output.open("xb") as dst, (
            args.candidate.open("rb") if args.candidate else open("/dev/null", "rb")
    ) as candidate:
        while gray := source.read(size):
            if len(gray) != size:
                raise ValueError(f"Truncated input frame {frames}")
            if args.tless_panel:
                if threshold is None:
                    threshold = panel_threshold(gray)
                    print(f"panel_threshold={threshold} roi=190,95,350,350", file=sys.stderr)
                gray = prepare_panel(gray, threshold)
            expected = contour(gray, width, height)
            dst.write(expected)
            if args.candidate:
                raw = candidate.read(size)
                if raw != expected:
                    raise ValueError(f"Hardware contour differs at frame {frames}")
            if gt is not None:
                frame_id = args.first_frame_id + frames
                annotations = gt.get(str(frame_id))
                if annotations is None:
                    raise ValueError(f"Missing BOP frame ID {frame_id} for clip frame {frames}")
                mask = read_masks(args.ffmpeg, args.scene, frame_id,
                                  len(annotations), size, args.tless_work96)
                target = boundary(mask, width, height)
                print(json.dumps({
                    "bop_frame_id": frame_id,
                    "clip_frame_index": frames,
                    "panel_calibration": (args.tless_panel and frames == 0)
                    or (args.tless_work96 and frame_id == 0),
                    **scores(expected, target, width, height, args.radius),
                }))
            frames += 1
        if args.candidate and candidate.read(1):
            raise ValueError("Hardware output has extra frames")
    if not frames:
        raise ValueError("No complete input frames")
    print(f"Reference checked {frames} frames", file=sys.stderr)


if __name__ == "__main__":
    main()
