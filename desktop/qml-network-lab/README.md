# Network Lab

Network Lab is an original EmberBSD QML application for a 480 × 480 RGB565
wsdisplay and a wsmouse touchscreen. The two tabs share one running process.

- **Clock** asks `time.google.com` for UTC over SNTP at startup and every
  five minutes. Between replies the clock and the animated seconds marker
  advance from monotonic time. If the server stops replying, the clock
  continues from its last sample and displays `HOLDOVER / RETRY`. The
  example accepts server dates from 2020 through 2099 and only updates
  the displayed time.
- **DNS Pulse** resolves `example.org` through the system IPv4 resolver.
  The meter and its trailing marker animate to the measured lookup time.
  The scale ends at 500 ms; the numeric readout also shows longer lookups.
  Tap **Refresh** for another measurement or wait for the 30-second update.
  If a lookup fails, the status changes and the last successful address and
  duration stay on screen. The duration includes local resolver processing.

## Build and run

Set `HOST_PREFIX` to the native Ports tools prefix, `SYSROOT` to the compact
RV32 sysroot, and `BUILTINS` and `UNWIND` to its compiler runtime archives.
Set `EMBER_SDK_SOURCE` and `EMBER_RUNTIME_SOURCE` to the corresponding
checkouts. The build uses Clang, LLD, Python and Netpbm from the native
prefix and links a static executable against EmberBSD libc.

```sh
OUT="$PWD/out/network-lab" sh desktop/qml-network-lab/build.sh
```

Copy `out/network-lab/network-lab` to the board and run it from SSH with
access to `/dev/ttyE0` and `/dev/wsmouse0`. Stop any other application
using those devices first. Use Ctrl-C to return the display to its console.
To select endpoints on your own network, run
`./network-lab -s time-server -p 123 -n lookup-host`.

## How the example fits together

[NetworkLab.qml](NetworkLab.qml) owns layout, touch targets, artwork and
animation durations. Its `refreshRequested()` signal triggers a lookup.
The SDK compiles the scene and exports its named controls in a generated C
header. [network-lab.c](network-lab.c) updates these controls and polls
touch input, query completion and the next animation deadline. It runs at
most one bounded query worker at a time, so a slow resolver leaves the
display responsive. The PNGs in [art](art/) are compiled into the scene.

[sample.c](sample.c) implements IPv4 lookups and a read-only UDP time query.
The host test uses a loopback responder and exercises valid, missing and
malformed replies. Run it with the native Ports compiler:

```sh
CC="$HOST_PREFIX/bin/clang" OUT="$PWD/out/network-lab-tests" \
    sh desktop/qml-network-lab/tests/run.sh
```
