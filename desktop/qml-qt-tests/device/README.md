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
