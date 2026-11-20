# Mosaico weather build comparison

The 480 × 480 weather screen starts at 21 C and sunny. Its implementations
use the same labels, colors, positions and 14-pixel font size.
Qt uses the host's default font for the captured frame. The RV32 LVGL package
uses its bundled Montserrat font. The examples work offline.

| Build | Where the UI is drawn | Output |
| --- | --- | --- |
| Qt Quick | Qt Quick evaluates `Weather.qml` and draws it on the target. | `weather-quick` with the QML engine, software renderer and platform plugin linked into the static RV32 build |
| EmberBSD QML | The SDK compiles `Weather.qml` into a C scene. EmberBSD Runtime draws it on the target when model values change. | Standalone `weather-qml` ELF with an RGB565 scanline renderer |
| Captured QML frame | Native Qt Quick draws Weather.qml at build time. The RV32 program expands the resulting RGB565 runs one row at a time. | Standalone `weather-frame` ELF |
| LVGL | LVGL creates and renders the labels on the target using a 16-line draw buffer. | Standalone `weather-lvgl` ELF |

The [BSD 2-Clause license](LICENSE) covers the example's own QML, C and C++
sources. The Qt Quick build uses the open-source Qt 6 modules from
[Qt Base](https://code.qt.io/cgit/qt/qtbase.git/) and
[Qt Declarative](https://code.qt.io/cgit/qt/qtdeclarative.git/).
Those modules retain their respective [Qt licenses](https://doc.qt.io/qt-6/licensing.html).

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

### Build the same QML with EmberBSD

Select the RV32 sysroot and runtime archives as above. Set `EMBER_SDK_SOURCE`
to the [EmberBSD SDK checkout](https://github.com/oxtech-ember/EmberBSD-SDK)
and `EMBER_RUNTIME_SOURCE` to the [Runtime checkout](https://github.com/oxtech-ember/EmberBSD-Runtime).
Use absolute paths. The generator uses the SDK's host Python, and the target
links C, the scene and the RGB565 renderer. This path works without Qt or LVGL.

```sh
sh build.sh qml
wc -c "$OUT/weather-qml"
```

Copy the resulting ELF to the board, open a terminal connected to the device
and run `./weather-qml /dev/ttyE0`. It displays the initial value from QML.
Type `-8` and press Enter to update the center label. Each `presented` line
acknowledges a completed render. EOF or Ctrl-C restores the previous display
mode. For a standalone display, `weather-qml --hold /dev/ttyE0 </dev/null`
keeps the last reading until SIGTERM, SIGINT or SIGHUP. The application uses 960 bytes of scanline storage at this resolution
and a 32-bit value for the temperature, in addition to the system framebuffer
and process memory. See the [compiler's language profile](https://github.com/oxtech-ember/EmberBSD-SDK/blob/main/docs/qml.md).

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

The Qt application links Qt Quick and loads the embedded QML module at startup.
For the RV32 build, set `RV32_QT_PREFIX` to the staging prefix of the target
Qt installation. The build uses its Qt toolchain file, the native Qt from
`HOST_PREFIX` for code generation, and writes a stripped executable to `OUT`:

```sh
sh build.sh quick
wc -c "$OUT/weather-quick"
```

The target Qt installation supplies Qt Core, Gui, Qml, Quick, the QtQuick
import and the bsdfb platform plugin. The widget-free example uses the Qt
Quick software renderer and a font available to the target font database.
For the measured prefix, Qt Base and Qt Declarative came from EmberBSD Ports.
Both used static MinSizeRel builds for RV32IMAC/ILP32. Qt Base used target
zlib, 16-bit PCRE2, double-conversion and FreeType. Qt Declarative used the
matching native Qt tools (including `qsb`) and was built with
`FEATURE_qml_jit=OFF`. Static linking imports the QML plugins found by
`qmlimportscanner`, including QtQuick, QtQml and QtQml.Models.
For a wsdisplay device, select the software backend and point Qt at a directory
containing an installed TrueType or OpenType font:

```sh
QT_QPA_PLATFORM=bsdfb:fb=/dev/ttyE0 QT_QUICK_BACKEND=software \
    QT_QPA_FB_DISABLE_INPUT=1 QT_QPA_FONTDIR="$PWD/fonts" ./weather-quick
```

For a small test image, the native HarfBuzz tool can subset an existing
DejaVu Sans font to the characters used by this sample. Install the resulting
font and its original license together in the `fonts` directory beside the
executable:

```sh
mkdir -p fonts
"$HOST_PREFIX/bin/hb-subset" "$DEJAVU_SANS_TTF" --text='MOSAICO SUNNY -0123456789C' \
    --output-file=fonts/WeatherSans.ttf
cp "$DEJAVU_LICENSE" fonts/LICENSE-DejaVu
```

The measured RV32 profile uses static Qt 6.12, the QML interpreter, the
software scene graph, FreeType and the RGB565 wsdisplay platform plugin.
The app imports `QtQuick`. It uses neither Qt Quick Controls nor image format
plugins. In a shared build, measure the libraries and plugins separately.

For Qt Quick Ultralite, the same directory provides `Weather.qmlproject`.
Select `-DWEATHER_USE_QUL=ON` with the Qt for MCUs SDK, its RV32 toolchain
configuration and a Mosaico platform port. See the [platform prerequisites](../mosaico-qul-smoke/README.md).

### Validation

The native Qt offscreen test checks the background and visible temperature,
exports the frame, changes the QML temperature property to −8 and checks
that the label is redrawn. The native executable also starts its event loop.

All four programs were cross-linked as static RV32 ILP32 soft-float ELF
executables. The Qt Quick build contains the compiled `Weather.qml` module,
Qt Quick, the QML engine, the static imports and the bsdfb platform plugin.
The target Qt prefix used the EmberBSD RV32 C foundation with static libc++,
libc++abi and libunwind. The build requires a target Qt Quick installation
in addition to the C foundation.

| RV32 build | Stripped executable size |
| --- | ---: |
| Qt Quick 6.12.0 | 16,730,652 bytes |
| EmberBSD QML, including renderer, display backend and static libc | 505,052 bytes |
| LVGL 9.6.0 | 992,984 bytes |
| Captured QML frame | 506,872 bytes |
| Empty C main with the same C startup and libc | 498,664 bytes |

The EmberBSD QML executable is 6,388 bytes larger than the empty C
executable with the same startup and libc. Both the model and the text are
evaluated on the target. In the full Qt Quick link the object for the QML
scene itself accounts for about 20.5 kB. The large contributors are Qt Quick,
Qt Core, Qt QML and Qt GUI. With the same Qt archives, omitting link-time
section garbage collection increased the stripped ELF by 1,322,016 bytes.
Safe identical code folding produced the same file size; changing only the
application to `-Oz` increased it by 512 bytes.

Disabling Qt Core's localized time zone names in this target prefix
(`-DFEATURE_timezone_locale=OFF`) saved 3,920,896 bytes in the linked
executable. The scene displays no times or dates. The font subset adds
7,580 bytes. The resulting Qt application exceeds the 10 MiB LittleFS
partition used on the test board. Qt Quick target rendering and peak RAM use
have yet to be measured. The LVGL and captured-frame executables include
their text rendering data.

### Embedded QML acceptance

The RV32 binary compiled from the unchanged `Weather.qml` was run on a
Mosaico V1.2. The process reported a completed initial presentation and a
second presentation after reading `-8` from stdin. An RSS sample after the
second render was **468 KiB**. This is one process sample, not peak memory
or the total RAM used by the OS and display. A host render of the same
generated scene changed 12 rows within the central label; all other rows
matched. The tested ELF SHA-256 was
`82431333c028e8897f487b78fa24feb0cd161d5164edb58917abb517620f86ee`.

### Physical LVGL acceptance

On EmberBSD Mosaico, the 992,984-byte RV32 LVGL executable was copied to
`/tmp` and byte-for-byte read back over the pinned SSH connection before it
was run as `weather-lvgl /dev/ttyE0`. The 480 × 480 RGB565 display showed
`MOSAICO`, `21 C` and `SUNNY`; the program stayed active with the scene on
screen. The tested executable had SHA-256
`35981671ad01d15f9503138bb1e93d522d480cbff7e3aab072cf588517ea3988`.
This is physical acceptance of the LVGL build only. `weather-quick` has been
built and host-validated, but has not been run on Mosaico; Qt Quick's target
rendering and peak memory use remain unverified.
