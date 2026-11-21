# QML-to-Qt Quick pixel integration tests

This test ties together the implemented SDK QML compiler, the experimental
EmberBSD Runtime renderer and an independent Qt Quick 6.12 renderer. Four
static fixtures compare RGB565 frames. Three interactive fixtures check
pointer event handling, animated motion and visibility. A touch adaptation
compares an analog instrument with the original Qt dial.

The untouched upstream inputs are kept in
[`upstream/qtdeclarative-v6.12.0`](upstream/qtdeclarative-v6.12.0). Their exact
Qt Declarative tag, peeled commit, original paths, SPDX expressions and SHA-256
values are recorded in [`UPSTREAM.json`](upstream/qtdeclarative-v6.12.0/UPSTREAM.json).
The adjacent `BSD-3-Clause.txt` is the matching upstream license text. The
positive fixtures exercise nested rectangle painting, sibling `z` order and
Grid spacing/centering:

- `visualparent.qml` has two overlapping sibling rectangles.
- `visualparent2.qml` has a red rectangle nested in green with `z: 2000`,
  behind a later blue sibling at `z: 2`. It checks that nested red is not
  implicitly clipped by green and that blue covers red in their overlap.
- `grid-spacing.qml` puts four fixed-size rectangles in a centered two-column
  Grid and checks each tile and the spacing gap.
- `horizontal-spacing.qml` is the unchanged Qt 6.12 positioner test. Its
  `Item` root is shown in a 320 × 240 viewport; the initial LTR `Row` has
  spacing 10 and red rectangles sized 50 × 50, 20 × 10 and 40 × 20. The
  renderer compares the whole image, including the two white spacing gaps and
  the white window background. A temporary test input changes the root's
  boolean initial value to true and compares the RTL branch separately. The
  compiler evaluates this boolean at build time; the generated scene does not
  expose a mutable property or live binding.

The unchanged `mousearea.qml` exercises translated text, pointer coordinates,
containsPress opacity, click, hold and bounded drag. A Qt Quick test driver
delivers real window-system test events to the compiled Qt implementation and
reads the QML object properties. A separate C trace sends the corresponding
input to the EmberBSD runtime and reads its scene. The host test compares the
two results. The embedded bitmap font has its own metrics, so this fixture
compares interaction and geometry rather than glyph pixels.

The unchanged Qt manual accessibility test `behavior.qml` draws a red square and
the label “Behavior animation”. Each click on the square sets a new x coordinate
50 pixels to the right. Its linear `NumberAnimation` takes 1000 ms. The Qt and
Runtime traces click twice, check that intermediate positions occur, and compare
the two endpoints. The Runtime trace also checks the halfway position and that
the timer sleeps after the animation completes.

The Qt documentation snippet `visual-opacity.qml` draws two overlapping
squares. The blue square has opacity 0.5. Tapping their parent area hides
the blue square and reveals the red and background underneath; another tap
restores it. The test compares complete 320 × 480 frames before tapping,
after an outside tap, after hiding and after restoring. All color boundaries
and opaque pixels must match Qt exactly. Within the blue square, the
comparison allows one RGB565 level per channel because Runtime blends packed
components and Qt blends before converting to RGB565. For blue over the dark
background, Runtime produced `0x10b1` and Qt produced `0x10b2`. Both produced
`0x800f` where blue overlapped red.

## Dial control

The touch adaptation in [dial.qml](dial.qml) uses the original face, needle,
shadow and glass from Qt Quick's `customitems/dialcontrol`. Five tap targets
select a percentage. The hand, digital readout and selected marker change
together. The original example uses a continuous slider and a spring rotation.
The target links the face and glass once, plus five cropped positions of the
hand and shadow. The device runner handles exit on a process signal.

The host test drags the original Qt slider, samples the settled face at each
chosen value and compares it with a Runtime touch sequence. It checks the
printed face, the red needle mask, the selected marker, five distinct readouts
and exact restoration of the initial frame. Raster filtering and RGB565
quantization allow small differences. The original input paths and SHA-256
digests are in the upstream manifest. The derived PNGs use the same BSD
license as the [Qt artwork](dial-art/README.md).

## Run

Set the native Ports prefix with Qt 6.12 and Netpbm, SDK source and Runtime source. Choose a
separate output directory:

```sh
HOST_PREFIX=/path/to/host-pkg-llvm \
EMBER_SDK_SOURCE=/path/to/EmberBSD-SDK \
EMBER_RUNTIME_SOURCE=/path/to/EmberBSD-Runtime \
OUT=/tmp/qml-qt-tests \
sh desktop/qml-qt-tests/run.sh
```

For each positive fixture the script invokes the real `scripts/qml-compile.py`,
compiles generated C with `eqml_render()` and writes a row-major RGB565 frame.
A small native `QQuickView` application loads the same unchanged QML under Qt
6.12's offscreen QPA and software scene graph, converts its captured image to
RGB565 and writes the same packed frame format. The test compares every pixel,
then checks selected background, tile and overlap probes. The visual-parent
scenes are 320 × 480; Grid is 112 × 112. Frames are raw little-endian RGB565
without row padding.

The offscreen comparison checks QML layout and the Runtime painter against Qt
Quick's output. A separate RV32 test on one ESP-Mosaico V1.2 produced the Qt
reference CRC32 for each of the three frames. The board also presented
`visualparent2.qml` through wsdisplay.
[Device measurements](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/1c7cef575ecc10e67a84598642da0693a24a556e/boards/esp-mosaico/evidence/qml-qt-reference-scenes.json).

## Physical MouseArea run

[Build and run the RV32 application](device/README.md). With the unmodified
upstream QML on one Mosaico V1.2, CST92xx touch input reached the red
MouseArea: its original `onPressed` handler logged `Pressed (LeftButton
shift=false)`. Leaving while pressed and releasing inside produced distinct
exit messages. The blue rectangle moved from (190, 10) to (194, 61) and then
(200, 89). The operator confirmed the application was working. The host
trace separately checks the resulting click, hold and double-click labels.
