# Mosaico weather build comparison

One 480 × 480 screen shows a fixed sample reading of 21 C and sunny weather.
Both layouts use the same text, colors, positions and 14-pixel font size.
Qt uses the host's default font for the captured frame. The RV32 LVGL package
uses its bundled Montserrat font. The examples work offline.

| Build | Where the UI is drawn | Output |
| --- | --- | --- |
| Qt Quick | Qt Quick renders Weather.qml on the system running the application. | `weather-quick` plus that system's Qt libraries and platform plugin |
| Captured QML frame | Native Qt Quick draws Weather.qml at build time. The RV32 program expands the resulting RGB565 runs one row at a time. | Standalone `weather-frame` ELF |
| LVGL | LVGL creates and renders the labels on the target using a 16-line draw buffer. | Standalone `weather-lvgl` ELF |

### Build the RV32 programs

Set `HOST_PREFIX` to the native EmberBSD Ports tools, including Qt 6.12
(qtdeclarative and QtTest) and Netpbm for the frame build. Set `SYSROOT`
to the EmberBSD RV32 C foundation, `BUILTINS` and `UNWIND` to its matching
runtime archives, and `LVGL_PREFIX` to the RV32 LVGL 9.6 package prefix.
Set `OUT` to an absolute output directory. The default target compiler is
`$HOST_PREFIX/bin/clang`; set `TARGET_CLANG` to select another SDK Clang.

```sh
sh build.sh frame
sh build.sh lvgl
wc -c "$OUT/weather-frame" "$OUT/weather-lvgl"
"$HOST_PREFIX/bin/llvm-size" "$OUT/weather-frame" "$OUT/weather-lvgl"
```

Both links use RV32IMAC, ILP32 soft-float, size optimization, section garbage
collection, the same startup objects and static libc. The frame build also
writes `weather.png` for visual review and `Weather.qmlc` to measure Qt's
compiled QML cache. The standalone frame viewer embeds the pixels. Changing
the reading in that build requires regenerating the frame.

Copy one ELF to an executable filesystem on the board and run it with the RGB565
wsdisplay device, for example `./weather-lvgl /dev/ttyE0` or
`./weather-frame /dev/ttyE0`. Each draws the static scene and sleeps until
SIGINT, SIGTERM or SIGHUP. On exit it restores the saved display mode.
Neither program needs a touch device.

### Build the same QML as a Qt application

With native Qt installed in the Ports prefix, run from this directory:

```sh
"$HOST_PREFIX/bin/cmake" -S . -B "$OUT/qt-quick-host" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$HOST_PREFIX/bin/ninja" \
    -DCMAKE_CXX_COMPILER="$HOST_PREFIX/bin/clang++" \
    -DCMAKE_PREFIX_PATH="$HOST_PREFIX/qt6" -DCMAKE_BUILD_TYPE=MinSizeRel
"$HOST_PREFIX/bin/cmake" --build "$OUT/qt-quick-host"
"$OUT/qt-quick-host/weather-quick"
```

The Qt application links Qt Quick and loads the compiled resource on startup.
Running it on EmberBSD RV32 requires Qt Core, Gui, Qml and Quick for that ABI
and a working display plugin. The executable size of the shared-library host
build excludes those dependencies.

For Qt Quick Ultralite, the same directory provides `Weather.qmlproject`.
Select `-DWEATHER_USE_QUL=ON` with the Qt for MCUs SDK, its RV32 toolchain
configuration and a Mosaico platform port. See the [platform prerequisites](../mosaico-qul-smoke/README.md).

### Validation

The native Qt offscreen test checks the background and visible temperature,
then exports the real frame. Both standalone programs were cross-linked for
EmberBSD RV32. Their first physical display run and the target Qt Quick and
Ultralite builds remain open. The frame viewer measures the cost of shipping
a static QML-designed screen. Measuring an on-device QML engine requires the
target Qt or Ultralite build and its dependencies.

With the Mosaico RV32 C foundation, Qt 6.12.0 on the build host and the
LVGL 9.6.0 target archive, the stripped frame viewer was 506,872 bytes and
the LVGL executable was 992,984 bytes. An empty C main linked with the same
startup files and static libraries was 498,664 bytes. These are file sizes.
The 2,788-byte `Weather.qmlc` needs a matching Qt runtime.
