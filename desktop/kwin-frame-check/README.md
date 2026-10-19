# A hardware Qt Quick client through KWin

This check draws four fixed colour quadrants in a fullscreen Wayland window.
It requires a real GLES client context and the selected hardware renderer.
It captures the composed workspace through the existing KWin Screenshot2 API.
It checks all 307200 RGB pixels and all alpha values before it saves a PPM file.
The QML reference and the C++ expected channel values are independent inputs.

Build on the developer host. Use the shared GCC16 and an EmberBSD application
sysroot that supplies Qt 6 Quick, Gui, Qml and DBus. Do not build on the board.

```sh
CXX=/path/to/aarch64--netbsd-g++ SYSROOT=/path/to/sysroot \
    sh build.sh /new/frame-check-output
```

Run in a private D-Bus session with a KWin Wayland virtual output of 640 by 480.
Use scale 1. Use the same accepted Mesa and driver cohort for KWin and the client.
First require the hardware renderer in `org.kde.KWin.supportInformation`.
Do not accept a compositor fallback to QPainter or a software GL renderer.
The helper separately checks its own client renderer.

```sh
QT_QPA_PLATFORM=wayland /path/to/frame-check \
    /absolute/path/to/Quadrants.qml /new/frame.ppm PowerVR
```

Replace `PowerVR` with the required hardware renderer substring for the target.
The client rejects known software renderer names and Mesa version overrides.
It selects the basic Qt Quick render loop and requests a real GLES2 context.
It does not change Mesa feature bits or the compositor renderer.
KWin Screenshot2 must be available on the same session bus.
Use this only in the compositor session that you intend to capture.
The output path must not exist. The helper does not replace an existing file.
A failed capture or pixel mismatch returns a nonzero status.

The API transfers a Unix descriptor through Qt D-Bus. Qt duplicates the local
write descriptor. The helper retains each descriptor until its owner releases it.
It bounds the metadata, image allocation, D-Bus call and pipe read.
The wire metadata follows
[KWin 6.8 Screenshot2](https://invent.kde.org/plasma/kwin/-/blob/v6.8.0/src/plugins/screenshot/screenshotdbusinterface2.cpp).
Descriptor ownership follows
[QDBusUnixFileDescriptor](https://doc.qt.io/qt-6/qdbusunixfiledescriptor.html).

This source check is not physical acceptance. A successful virtual run proves
one client and its composed frame. It does not prove HDMI scanout, input,
long runtime stability or a complete Plasma session.

Pass `red`, `green`, `blue` or `white` as the last argument to check a uniform
frame. Run those four modes in order in one compositor session. Require a
new output file and exit zero for each mode. This checks frame updates and all
colour channels without texture edges. The QML colour table and C++ channel
values remain independent. The default quadrant check stays exact.

KWin can use linear texture filtering. A quadrant edge can expose sampling
precision even when both uniform regions are correct. Do not change the
quadrant reference to match a measured result. Report uniform-frame acceptance
and quadrant-edge precision as separate results.
