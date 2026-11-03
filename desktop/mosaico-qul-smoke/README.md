# Qt Quick Ultralite panel and touch smoke test

This small QML application draws four color bars on a 480 × 480 view and shows
the touch state, press count, and last coordinates. The panel geometry is
documented in the [ESP-Mosaico board profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/esp-mosaico/interfaces.md).

## Platform boundary

This directory contains application source only. It does not include an
EmberBSD Qt Quick Ultralite platform port or a QUL SDK. The application cannot
be built or run for EmberBSD until both are available.

The standard EmberBSD CO5300 framebuffer/genfb and touch path is already
confirmed on one V1.2 sample: the operator saw the LVGL interface and changing
touch coordinates in a RAM boot ([DevBoards evidence](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/esp-mosaico/evidence/row-pair-display-touch.json)).
What is missing here is the QUL platform adapter that presents those existing
display and input interfaces through QUL's screen and event APIs. The
application consumes touch through QUL's `MouseArea`; it does not open
`wsmouse` or access display registers. Dispatch QUL input from the platform
event-loop context, not directly from an interrupt handler.

The existing EmberBSD LVGL/wscons example is a separate implementation and is
not a QUL platform adapter. This smoke test does not exercise the camera,
audio codec, sensor bus, or network.

## QUL SDK and platform inputs

The example files are BSD-2-Clause project source and do not include the QUL
SDK, generators, or runtime libraries. Their license does not grant access to
those Qt components. Any EmberBSD-authored platform adapter would remain
separate project code; QUL terms govern access to and use of the Qt components.

To build this target application, the following inputs are still needed:

- A Qt for MCUs SDK version whose license permits the intended development;
  its `Qul` CMake package, headers, and runtime sources needed to build for
  RV32 (or compatible target libraries if that licensed package supplies them).
- The matching host generators, including `qmlprojectexporter`, `qmltocpp`,
  `qmlinterfacegenerator`, and resource/font tools used by the project.
- An EmberBSD RV32 QUL platform package: CMake target setup, target toolchain
  and C++ runtime integration, plus an adapter from the existing framebuffer
  and touch interfaces to QUL rendering and event dispatch.

Qt's [licensing model](https://doc.qt.io/QtForMCUs/qtul-licensing.html)
describes commercial licenses and a time-limited evaluation license. Its
[source-build instructions](https://doc.qt.io/QtForMCUs/qtul-building-from-sources.html)
make full QUL source builds conditional on a license that grants source access;
they also identify RISC-V as requiring a QUL source build. The documentation
does not list QUL under an open-source Qt license. Qt documents platform-library
work against evaluation packages separately, but that does not provide an
open-source QUL SDK or establish public redistribution rights for QUL itself.

For downstream device distribution, Qt states that evaluation does not permit
distributing devices. A product shipping QUL therefore needs terms that permit
that use. This repository publishes source code; it does not distribute a
device.

## Project format and validation

The `.qmlproject` and CMake target use Qt's documented Qt Quick Ultralite
project structure. Standard Qt `qmllint` can provide QML syntax feedback; it
does not validate QUL compatibility, generated QUL code, or EmberBSD platform
integration. No QUL build or target execution is claimed here.

On a supported QUL platform, a useful smoke result is four visible bars across
the 480 × 480 panel and changing touch state/coordinates while the finger is
pressed and moved. This does not establish touch calibration, camera capture,
audio playback, or long-term stability.

## References

- [Qt Quick Ultralite minimal example](https://doc.qt.io/QtForMCUs/quickultralite-minimal-example.html)
- [QUL `MouseArea`](https://doc.qt.io/QtForMCUs/qml-qtquick-mousearea.html)
- [QUL platform architecture](https://doc.qt.io/QtForMCUs/platform-porting-guide-architecture.html)
- [QUL screen rendering](https://doc.qt.io/QtForMCUs/platform-porting-guide-screen.html)
- [QUL touch input](https://doc.qt.io/QtForMCUs/platform-porting-guide-input-events.html)
