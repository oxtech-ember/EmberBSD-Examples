# LVGL wscons status screen

A small C status screen that reports touch coordinates. It uses LVGL 9.6's
software RGB565 renderer, a 16-line partial draw buffer, a `wsdisplay`
framebuffer mapping, and `wsmouse` absolute X/Y plus button 0. It does not
use board-specific registers, a display server, or a camera.

The display driver must expose native-endian RGB565 pixels through
`WSDISPLAYIO_GINFO`, `WSDISPLAYIO_GET_FBINFO`, `WSDISPLAYIO_GMODE`/`SMODE`,
`WSDISPLAYIO_LINEBYTES`, and shared `mmap`. A 16-bit depth by itself does
not identify RGB565: use this program only with a driver that documents that
format. The program verifies RGB565 channel masks, size and geometry, and
rejects nonzero visible framebuffer offsets. The driver must make writes to the mapping visible on the panel.
The input driver must provide calibrated absolute pixel coordinates and
button 0 as current-version `wscons_event` records. Other pointer devices,
multitouch, rotation, and scaling are outside this example.

The ESP-Mosaico display and input drivers are still under development. This
example has been compiled and statically linked for RV32 but has not been
run on the board. The separate display framebuffer and driver staging memory,
panel update rate, touch calibration, and visible output have not yet been
measured on hardware.

## Build against the RV32 C foundation

Use a target LVGL 9.6 package built from the common EmberBSD Ports recipe
and the matching EmberBSD C foundation. Set all paths below to the selected
SDK inputs. `LVGL_PREFIX` holds the target archive and installed headers;
`BUILTINS` and `UNWIND` are the matching target compiler-runtime archives.
The target does not need a full C++ SDK.

```sh
"$CLANG" --target=riscv32--netbsd --sysroot="$SYSROOT" \
  -march=rv32imac -mabi=ilp32 -Os -ffunction-sections -fdata-sections \
  -DLV_CONF_INCLUDE_SIMPLE -DLV_KCONFIG_IGNORE -I"$LVGL_PREFIX/include" \
  -c main.c -o main.o
"$CLANG" --target=riscv32--netbsd --sysroot="$SYSROOT" \
  -march=rv32imac -mabi=ilp32 -fuse-ld=lld -static -nostdlib \
  -Wl,--gc-sections -o lvgl-wscons \
  "$SYSROOT/usr/lib/crt0.o" "$SYSROOT/usr/lib/crti.o" \
  "$SYSROOT/usr/lib/crtbegin.o" main.o -Wl,--start-group \
  "$LVGL_PREFIX/lib/liblvgl.a" "$SYSROOT/usr/lib/libc.a" \
  "$BUILTINS" "$UNWIND" -Wl,--end-group \
  "$SYSROOT/usr/lib/crtend.o" "$SYSROOT/usr/lib/crtn.o"
```

Select Clang and LLD from the compatible SDK prefix. This explicit C-only
link is for the intermediate foundation, not a claim of a complete SDK.

## Run and stop

Once suitable drivers and device nodes exist, provide their paths explicitly:

```sh
./lvgl-wscons /dev/ttyE0 /dev/wsmouse0
```

Device names and permissions depend on the installation. The program restores
the previous display mode on a normal exit or SIGINT/SIGTERM. A forced kill
cannot restore it. A display shared with another console or graphics process
must be managed by the operator.

## Host regression

With the native LVGL package staged or installed, the following exercises the
actual framebuffer row-copy and mouse event handling on synthetic memory and a
pipe. The script derives event numbers from the provided OS header; its small
host shim is not an OS ABI or hardware test. The test C is instrumented with
ASan/UBSan; the separately built LVGL archive is not instrumented.

```sh
"$HOST_PREFIX/bin/python3.15" tests/run-native.py \
  --host-prefix "$HOST_PREFIX" --lvgl-prefix "$NATIVE_LVGL_PREFIX" \
  --wscons-header "$OS_SOURCE/sys/dev/wscons/wsconsio.h"
```
