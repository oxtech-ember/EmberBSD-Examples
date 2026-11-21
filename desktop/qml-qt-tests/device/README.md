# Device pointer run

`mousearea.c` renders the unchanged upstream `mousearea.qml` through the
EmberBSD Runtime and reads physical wscons pointer events. It centers the
320 × 480 scene on the 480 × 480 Mosaico display, forwards monotonic event time
to the QML UI state machine, and restores the previous wsdisplay mode on
SIGINT, SIGTERM or SIGHUP. The log contains real press/release and QML status
events; it does not synthesize input.

## Build

Use the SDK's native Ports tools and the RV32 static base selected for the
device. `SYSROOT`, `BUILTINS`, `UNWIND`, `HOST_PREFIX`, `EMBER_SDK_SOURCE`,
`EMBER_RUNTIME_SOURCE`, and `OUT` are explicit build inputs. For example:

```sh
HOST_PREFIX=/path/to/host-pkg-llvm \
EMBER_SDK_SOURCE=/path/to/EmberBSD-SDK \
EMBER_RUNTIME_SOURCE=/path/to/EmberBSD-Runtime \
SYSROOT=/path/to/rv32/sysroot \
BUILTINS=/path/to/libclang_rt.builtins-riscv32.a \
UNWIND=/path/to/libunwind.a \
OUT=/tmp/mousearea-rv32 \
sh desktop/qml-qt-tests/device/build.sh
```

The build runs the SDK's `qml-compile.py` for the original QML and links the
generated `eqml_mousearea` scene, Runtime renderer/input/display/wscons adapter,
and device runner as a static RV32 ILP32 soft-float executable.

## Upstream touch animation

Use the same build inputs and pass `behavior` to `device/build.sh`. This builds
`$OUT/behavior` from the unmodified Qt manual accessibility test. Run it with
the display and pointer device paths as for `mousearea`. On the 480 × 480
display, the 400 × 400 scene starts at (40, 40). Tap the red square near
(100, 190), let it stop, then tap it near (175, 190). Each tap moves it 50 pixels
to the right over one second. The runner reports physical input. Pass `--trace-frames` before the device
paths to also log the red square's x coordinate after each frame. Leave the
option off when measuring animation timing. Coordinates in the log are relative
to the scene. The original test continues moving the square to the right on
each click. Restart the application to restore its initial position.

## Upstream opacity and visibility

With the same build environment, pass `visual-opacity` to `device/build.sh`.
Run `$OUT/visual-opacity` on the target with the display and pointer paths.
The 320 × 480 scene starts at (80, 0) on a 480 × 480 display. In the lower
half, a translucent blue square overlaps the lower-right corner of a red
square. Tap the overlap near (205, 375). The blue square disappears and
the red square remains. Tap the red square near (150, 320) to restore the
original overlap. A tap near (90, 10) leaves either state unchanged.

## Upstream Row spacing

Build the two static target executables from the same unchanged Qt fixture:

```sh
HOST_PREFIX=/path/to/host-pkg-llvm \
EMBER_SDK_SOURCE=/path/to/EmberBSD-SDK \
EMBER_RUNTIME_SOURCE=/path/to/EmberBSD-Runtime \
SYSROOT=/path/to/rv32/sysroot \
BUILTINS=/path/to/libclang_rt.builtins-riscv32.a \
UNWIND=/path/to/libunwind.a \
OUT=/tmp/qml-row-rv32 \
sh desktop/qml-qt-tests/device/build-row-spacing.sh
```

`row-spacing-smoke` renders the initial LTR `eqml_horizontalspacing` scene to
RGB565 rows and prints their CRC32 without opening the display. `row-spacing-display`
clears the 480 × 480 display to white, centers the 320 × 240 QML viewport, draws
the original Row scene and waits for SIGINT, SIGTERM or SIGHUP before restoring
the prior wsdisplay mode. It only opens the supplied wsdisplay path; it does not
create device nodes or modify persistent configuration.

## Run on an image without a mouse node

The EmberBSD OS tree assigns `wsmouse` character major 212 and `MAKEDEV`
creates unit 0 as minor 0. If the selected image omitted `/dev/wsmouse0`, a
temporary mode 0600 node on tmpfs can expose the already attached controller
without changing the image. Create it using the OS `mknod` utility when it is
available, then pass its path as the second argument:

```sh
mkdir -m 700 /tmp/qml-pointer
mknod /tmp/qml-pointer/wsmouse0 c 212 0
chmod 600 /tmp/qml-pointer/wsmouse0
/tmp/qml-pointer/mousearea /dev/ttyE0 /tmp/qml-pointer/wsmouse0
```

For this OS build, major 212 and unit-0 minor 0 come from
`sys/conf/majors.ws` and `etc/MAKEDEV.tmpl`. This app only opens the supplied
device path; it does not create or change device nodes. The acceptance image
used a one-shot temporary node helper because its private root omitted the
`mknod` utility and `/dev/wsmouse0` entry.

The runner opens the node read-only and nonblocking, selects the current wscons
event ABI, then waits for physical events. Touch `Click` to exercise press,
release and click state; hold it to exercise the hold timer; drag the blue
square to its bounds. A first move outside the window is kept outside the QML
viewport so drag exit/release behavior remains testable. Stop with SIGINT,
SIGTERM or SIGHUP to restore the display mode. The temporary node disappears
when `/tmp` is cleared; no startup or persistent device configuration is
written.

## Analog dial control

Pass `dial` to `device/build.sh` and run the resulting `$OUT/dial` with
the same wsdisplay and wsmouse paths. The native Ports prefix supplies Netpbm
for the PNG build inputs. On a 480 × 480 screen, the centered 352 × 376 panel
shows a silver dial with a red needle. At y = 385, tap x = 364 (100%),
178 (25%), then 240 (50%). The needle points down-right, then
up-left, then straight up. The digital value and amber
marker follow each tap. The five discrete selections use the artwork and
settled angles of the original Qt slider example.
