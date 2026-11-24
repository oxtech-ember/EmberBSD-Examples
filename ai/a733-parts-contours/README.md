# Offline parts contours on the A733

This example measures fixed-network output for recorded industrial parts.
It reads headerless 8-bit grayscale frames of equal size. FFmpeg decodes
the input clip and encodes the contour frames without loss.

## Build

Use an AArch64 SDK, an SDK header that provides `<arm/a733_npu_tile.h>`,
and a matching A733 kernel. The device exposes the privileged
`A733_NPU_TILE_RUN` ioctl on `/dev/vipcore`.
AI assistance was used for this example and its validation.
[Driver procedure](https://github.com/oxtech-ember/EmberBSD/blob/main/sys/arch/arm/sunxi/docs/a733-npu-vip.md).

```sh
"$TARGET_CC" --sysroot="$SYSROOT" -std=c11 -O2 -Wall -Wextra -Werror \
  contours.c -o a733-parts-contours
```

Use the installed FFmpeg 9 executable from the target package. Set
`FFMPEG` to its absolute path.

## Prepare the target device

Boot the matching kernel and a device tree with the A733 NPU enabled.
Use a privileged shell. Query the current boot for the character-device
major. For unit 0, the minor is 0. Confirm both numbers on any existing
node before opening it.

```sh
set -e
npu=hw.sun60inpuid0
major=$(sysctl -n "$npu.vipmajor")
test "$major" -ge 0
if [ ! -e /dev/vipcore ]; then
  mknod -m 600 /dev/vipcore c "$major" 0
fi
test -c /dev/vipcore
test "$(stat -f '%Hr:%Lr' /dev/vipcore)" = "$major:0"
```

For the Zero 4 `graphics` profile with on-demand NPU support, run the
client after checking the device node. It opens `/dev/vipcore` and submits
the tile ioctl directly. The driver prepares the NPU on the first admitted
request and reuses the context on subsequent requests. The same procedure
applies to cold and warm runs.

For the dedicated `npu-observe` profile or an older compatible kernel
and DTB with manual preparation, run these stages once per boot before
the first tile. Leave the FE and diagnostic TASK profiles unselected.

```sh
for stage in 1 2 3 4; do
  if [ "$stage" -eq 3 ]; then
    if sysctl -w "$npu.stage=$stage"; then
      printf 'NPU stage 3 unexpectedly succeeded\n' >&2
      exit 1
    fi
  else
    sysctl -w "$npu.stage=$stage"
  fi
  result=$(sysctl -n "$npu.result")
  case "$stage:$result" in
    1:0|2:0|3:16|4:0) ;;
    *) printf 'NPU stage %s reported %s\n' "$stage" "$result" >&2; exit 1 ;;
  esac
done
```

Stage 3 must fail with `EBUSY` (16) for the cold preflight. The loop checks
the cached result before advancing. Stages 1, 2 and 4 must succeed.

## Run one physical tile

Use the first 24 frames of scene `000001` from
[T-LESS Primesense All test images](https://bop.felk.cvut.cz/datasets/#T-LESS).
Wrap the unmodified RGB PNG sequences in FFV1 at 8 fps. Use frame IDs
`000000` through `000023`. Credit T. Hodaň, P. Haluza, Š. Obdržálek,
J. Matas, M. Lourakis and X. Zabulis. The dataset has a
[CC BY 4.0 license](https://cmp.felk.cvut.cz/t-less/).

Extract the first disk crop from the 720×540 clip. Apply the calibrated
panel threshold below, then reduce the binary crop to 8×8.
Run the fixed task once as a privileged user, then compare all 64 pixels
with the independent host reference.

```sh
"$FFMPEG" -v error -i tless-scene-000001.mkv -frames:v 1 \
  -vf "format=gray,crop=158:158:363:278,lut=y='if(gt(val,124),255,0)',scale=8:8:flags=area,format=gray" \
  -f rawvideo first-disk.gray
./a733-parts-contours 8 8 first-disk.gray > first-disk-npu.gray
python3 reference.py 8 8 first-disk.gray first-disk-cpu.gray \
  --candidate first-disk-npu.gray
```

A complete tile reports `frames_written=1 positions_verified=64 tiles_verified=1`.
The client accepts a grayscale file or standard input. It writes complete
contour frames to standard output and reports verified counts on standard
error. The same ioctl supports further tiles after a successful run.

## Run the full panel clip with warm submission

Use a kernel that implements warm submission through the same ioctl.
A successful first tile allows another request. An uncertain run or a
failed device proof quarantines the retained context until reboot.

Decode the 24-frame clip with FFmpeg 9. Crop the working area at its
source size, apply the calibrated threshold of 124, then area-scale to
96×96. The threshold precedes scaling so the values at mixed boundary
pixels preserve the fraction of foreground.

```sh
set -e
"$FFMPEG" -v error -i tless-scene-000001.mkv -pix_fmt gray -f rawvideo scene.gray
"$FFMPEG" -v error -f rawvideo -pixel_format gray -video_size 720x540 \
  -framerate 8 -i scene.gray \
  -vf "crop=350:350:190:95,lut=y='if(gt(val,124),255,0)',scale=96:96:flags=area" \
  -pix_fmt gray -f rawvideo panel-work96.gray
test "$(wc -c < panel-work96.gray)" -eq 221184
```

Run the client as a privileged user after preparing the device. The input
is already prepared. Use the independent reference to check every output
byte and score the part boundaries.

```sh
set -e
./a733-parts-contours 96 96 panel-work96.gray > npu-work96.gray
python3 reference.py 96 96 panel-work96.gray cpu-work96.gray \
  --candidate npu-work96.gray --tless-work96 --first-frame-id 0 \
  --scene test_primesense/000001 --ffmpeg "$FFMPEG" --radius 1 > work96-metrics.jsonl
"$FFMPEG" -v error -f rawvideo -pixel_format gray -video_size 96x96 \
  -framerate 8 -i npu-work96.gray -c:v ffv1 npu-work96.mkv
```

The expected count is 24 complete frames, 221,184 verified positions and
3,456 tiles. Compare these numbers with the target output. The reference
crops and area-scales each visible part mask to the same raster. It uses
pixels above 127 in the mask union and reports boundary scores at one
pixel of tolerance. The tolerance is in the 96×96 raster. Compare quality
on BOP IDs 1 through 23. Frame 0 supplied the fixed threshold.

## Run one frame at source resolution

Use the `scene.gray` file decoded above. Extract the first 720×540 frame,
then use the client to apply panel preparation at source resolution.
This runs 6,120 tiles and writes one full contour frame.

```sh
set -e
dd if=scene.gray of=frame0.gray bs=388800 count=1
test "$(wc -c < frame0.gray)" -eq 388800
./a733-parts-contours --tless-panel 720 540 frame0.gray > frame0-npu.gray
python3 reference.py 720 540 frame0.gray frame0-cpu.gray \
  --tless-panel --candidate frame0-npu.gray
```

The first source frame is also the calibration frame. Use the same
preparation for its independent CPU comparison.

## Orange Pi Zero 4 graphics acceptance

With [EmberBSD kernel `e120f07b`](https://github.com/oxtech-ember/EmberBSD/commit/e120f07b8276e317e7bcb549de2816d0b49f5d84)
and the matching `graphics` DTB, the first admitted tile ioctl prepared
the NPU from stage 0. Two 24-frame 96×96 clip runs matched the CPU
reference in the same accelerated virtual Plasma session. Each run added
3,456 IRQs. The client times were 41.08 and 35.69 seconds.
Physical HDMI remains unverified.

## Orange Pi Zero 4 NPU observation acceptance

With the dedicated `npu-observe` DTB and [EmberBSD kernel `ff55aa03`](https://github.com/oxtech-ember/EmberBSD/commit/ff55aa034d41208744ee3e21d5620e1df8156813),
the board processed 24 panel frames at 96×96. The 221,184 verified
positions across 3,456 tile calls took 22.46 seconds of elapsed client
time. The output matched the CPU reference byte for byte. Its SHA-256
was `e6e8f6d823cc10b77a6db35de3eb587975dbed440648a536f5819ea790216b59`.
Boundary F1 was 0.9052 across BOP IDs 1 through 23 at one pixel of tolerance
in the 96×96 raster.

The board also processed the first 720×540 frame before and after
the 96×96 clip, on the same boot. Each source-resolution run verified
388,800 positions across 6,120 tile calls. The run from a cold TASK
context took 61.00 seconds. The run from a warm context took 59.71
seconds. The cumulative TASK IRQ count advanced from 0 to 6,120, then
9,576 after the clip, then 15,696. Both 720×540 outputs had SHA-256
`245a45c973ad988cf770225e0002c848353893afc896c8e9ec88860ed1330978`.

## Verify the full clip on the host

Use `--tless-panel` to prepare this scene before computing gradients.
The rectangular work area spans x=190 through 539 and y=95 through 444.
These zero-based bounds come from the panel image and omit the peripheral
calibration markers. The first frame selects one Otsu threshold from the
grayscale histogram inside the rectangle. The measured threshold is 124.
Pixels above it become 255. All other pixels become 0. Apply that same
threshold and rectangle to the remaining 23 frames. The C client supports
the same preparation via `./a733-parts-contours --tless-panel 720 540 scene.gray`.

Supply the extracted BOP scene directory that contains `scene_gt.json` and
`mask_visib`. The host reference writes raw contour frames and reports
one JSON result per frame with its `bop_frame_id` and zero-based
`clip_frame_index`. Input frames must be in increasing BOP ID order without
gaps. The supplied clip uses IDs 0 through 23. For a consecutive
subsequence, set `--first-frame-id` to the first BOP ID.
Boundary IoU uses exact pixels. Boundary
precision, recall and F1 use a two-pixel Euclidean tolerance by default.

```sh
"$FFMPEG" -v error -i tless-scene-000001.mkv \
  -pix_fmt gray -f rawvideo scene.gray
python3 reference.py 720 540 scene.gray cpu-contours.gray \
  --tless-panel --first-frame-id 0 --scene test_primesense/000001 \
  --ffmpeg "$FFMPEG" > cpu-metrics.jsonl
"$FFMPEG" -v error -f rawvideo -pixel_format gray -video_size 720x540 \
  -framerate 8 -i cpu-contours.gray -c:v ffv1 cpu-contours.mkv
```

Each position uses a 2×2 group of grayscale pixels. The input planes
follow the order upper left, upper right, lower right, lower left.
The client quantizes the planes to 123 through 133. It checks all eight
returned channels against scalar arithmetic. It then takes horizontal and
vertical differences from two returned channels and marks a position when
the sum of their absolute values reaches three quantized steps. The host
reference computes the same contour directly from neighboring pixels.
Only scoring reads the visible-instance masks.

The score uses all 720×540 pixels and the union of the four visible part
masks. Frame 0 is marked as calibration in the JSON output. Across the
other 23 frames, mean boundary IoU was 0.1039 and mean boundary F1 within
two pixels was 0.6882. Mean recall was 0.6816 and mean precision was
0.6981. For comparison, the raw-gradient baseline averaged 0.1773 boundary
F1 on the same 23 frames. Shadows and dark faces can still lower recall.
The CPU result covers all 24 frames at source resolution. The hardware
checks above cover the 96×96 panel clip and one source-resolution frame.
