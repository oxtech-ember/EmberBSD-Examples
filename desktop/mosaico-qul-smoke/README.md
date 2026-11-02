# Qt Quick Ultralite panel and touch smoke test

This small QML application draws four color bars on a 480 × 480 view and shows
the touch state, press count, and last coordinates. The panel geometry is
documented in the [ESP-Mosaico board profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/esp-mosaico/interfaces.md).

## Platform boundary

This directory contains application source only. It does not include an
EmberBSD Qt Quick Ultralite platform port or a QUL SDK. The application cannot
be built or run for EmberBSD until both are available.

The target platform must provide the QUL screen and framebuffer presentation
interfaces and translate the board's touch input into QUL touch events. The
application consumes those events through QUL's `MouseArea`; it does not open
`wsmouse` or access display registers. Use the QUL event queue/dispatcher from
the platform event-loop context, not directly from an interrupt handler.

The existing EmberBSD LVGL/wscons example is a separate implementation and is
not a QUL platform adapter. This smoke test does not exercise the camera,
audio codec, sensor bus, or network.

## Project format and validation

The `.qmlproject` and CMake target use Qt's documented Qt Quick Ultralite
project structure. Build with the Qt for MCUs SDK and an EmberBSD QUL platform
kit after that platform is implemented. No QUL build, target execution, or
hardware result is claimed here. A host Qt `qmllint` check is syntax feedback
only; it does not validate QUL compatibility or EmberBSD platform integration.

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
