# QML-to-Qt Quick pixel integration tests

This test ties together the implemented SDK QML compiler, the experimental
EmberBSD Runtime renderer and an independent Qt Quick 6.12 renderer. It uses
only the `QtQuick` types already in the small runtime profile.

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

The unchanged `mousearea.qml` is an explicitly unsupported interactive case.
Qt Quick accepts it; the SDK compiler must reject its first unsupported grouped
`anchors` property with a source-local diagnostic and create no generated C
file. SDK tests separately assert the unsupported `MouseArea` type. This input
is not counted as a render test.

## Run

Set the native Ports prefix with Qt 6.12, SDK source and Runtime source. Choose a
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
Quick's output. It does not exercise the target `bsdfb` QPA, wsdisplay
framebuffer, target display color path, touch input or physical hardware.
