# H.265 RTP through installed C APIs

E35 sends changing lossless video between FFmpeg and GStreamer over localhost
UDP. The receivers call installed C APIs and write decoded I420 pixels,
frame timestamps and JSON reports. GStreamer reports the actual loaded plugin
paths and versions. The independent relay records original datagrams and
exact forwarded datagrams before fault injection and after it.

Use the [CPU SDK build instructions](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/common-media/rtp)
and its [results.md](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-media/rtp/results.md)
for package inputs and per-platform acceptance. Cross-compilation and native
controls alone do not establish target acceptance.

## Build and run

The selected SDK uses GCC16.2, FFmpeg 9.0.2 with shared x265 4.2,
GStreamer 1.28.8 and GLib 2.90.1. It needs the app/video, RTP, RTP manager,
UDP, video parser, x265 and libav plugins. The target also needs matching
EmberBSD base libraries, libxml2 and GCC16 runtime libraries, including
`libstdc++.so.7`. Ordinary desktop package options have separate coverage.
Include the coherent base `/usr/lib/i18n` modules and `/usr/share/i18n` data;
GLib must convert C-locale `646`/US-ASCII error text to UTF-8.
The example does not select hardware codecs or require a display server.
The separate upstream suites require their Meson test whitelist adapted to
installed providers, a fresh registry, the installed scanner and default
Check fork isolation. See the SDK README's test environment commands. Keep
that narrower whitelist out of this API suite's environment.

Build with CMake 3.20+, pkg-config and the installed SDK. Ruby generates
fixtures and independently checks captured RTP. For a native control:

```sh
MEDIA_PREFIX=/absolute/native-media-prefix
PKG_CONFIG_PATH="$MEDIA_PREFIX/lib/pkgconfig:$MEDIA_PREFIX/lib/ffmpeg9/pkgconfig" \
  cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX="$MEDIA_PREFIX"
cmake --build build --parallel 2
```

For cross-compilation, use a GCC16 toolchain file with `CMAKE_SYSTEM_NAME`
set to `NetBSD`, matching `CMAKE_SYSROOT`, and target-only library/header
searches. Set `PKG_CONFIG_PATH` empty, `PKG_CONFIG_SYSROOT_DIR` to that sysroot,
and `PKG_CONFIG_LIBDIR` to its target pkg-config directories, including
`usr/pkg/lib/ffmpeg9/pkgconfig`. Set installation prefix `/usr/pkg`.
`DESTDIR` stages installation without writing to the host's target prefix.
Warnings are errors by default; `compile_commands.json` records the context.

Generate a fresh fixture, then run inside one network namespace:

```sh
mkdir case-inputs
ruby ./make-fixture.rb case-inputs/fixture.i420 case-inputs/fixture.json
sh ./run-api.sh case-inputs/fixture.i420 new-results /usr/pkg/bin /usr/pkg
```

For a native control, replace the last two arguments with the native build's
binary directory and media prefix. Ensure the native loader finds that
prefix's libraries and compiler runtime. An isolated plugin registry is
created inside each new results directory. Do not run concurrent suites
on ports 5004/5006. Result paths must be new, and individual consumers also
require fresh decoded/CSV outputs.

```text
run-api.sh FIXTURE.i420 NEW_RESULTS APP_BINDIR MEDIA_PREFIX
```

The fixed suite requires 40 frames, 96x64 I420 at 10 fps. The generator also
accepts `--frames N` for focused tests; other frame counts are outside this
suite's fixed oracle. Every source pixel is defined independently of encoding.
The sender uses lossless closed GOP 4, repeated headers and no B frames.
FFmpeg transmits from a timestamped Matroska fixture; copying a raw HEVC file
without valid source timestamps is not an equivalent sender test.

| Case | Sender → receiver | Fault and required exact recovery tail |
| --- | --- | --- |
| `clean-ff-gst` | FFmpeg → GStreamer | All 40 frames, IDs 0..39 |
| `clean-gst-ff` | GStreamer → FFmpeg | All 40 frames, IDs 0..39 |
| `loss-ff-gst` | FFmpeg → GStreamer | First middle VCL FU dropped; IDs 8..39 |
| `loss-gst-ff` | GStreamer → FFmpeg | First middle VCL FU dropped; IDs 8..39 |
| `combined-ff-gst` | FFmpeg → GStreamer | FU loss, duplicate, reorder, blackout; IDs 20..39 |
| `combined-gst-ff` | GStreamer → FFmpeg | Same combined faults; IDs 20..39 |
| `receiver-restart-ff-gst` | FFmpeg → GStreamer | Receiver teardown/pause/rebind; IDs 28..39 |

`E35_CLEAN_ONLY=yes` runs only the first two cases. `E35_MAX_MS` and
`E35_IDLE_MS` set bounded process deadlines (defaults 90000 and 2500 ms).
`API.PASS` means the selected script cases and pixel oracles passed. It does
not mean every independent wire, analysis or package check was executed.
The combined fault drops the first middle VCL FU, duplicates datagram 20,
reorders datagram 40 and suppresses traffic 800..1200 ms after first input.
The restart tears down 1400 ms after the first decoded frame and pauses
600 ms before creating a fresh pipeline/socket.

## Independent contracts

`check-frames` computes expected pixels from frame identity and the original
fixture formula. Clean mode requires every frame from zero, with all bytes
exact. Loss modes record damaged output and missing IDs, require increasing
exact IDs, and require every byte of the specified complete recovery tail.
`--allow-damaged` permits damage only before that required tail. It does not
turn concealed or missing frames into successful decodes.

`check-rtp.rb` parses RFC 3550 headers and RFC 7798 single NAL/AP/FU packets.
It checks sequence/SSRC/PT, marker boundaries, timestamps and complete NAL
reassembly. DONL is absent in this profile. It can reconstruct an Annex B
stream with `--nal-out`. Run it separately from the API script, for example:

```sh
ruby ./check-rtp.rb new-results/clean-ff-gst/received.rtp \
  --events received --expect-ssrc 123456 --clock-step 9000 > sender-wire.json
ruby ./check-rtp.rb new-results/combined-ff-gst/forwarded.rtp \
  --compare-received new-results/combined-ff-gst/received.rtp \
  --expect-ssrc 123456 --clock-step 9000 --allow-loss > fault-wire.json
```

PT defaults to 96. The GStreamer sender's SSRC is 324478056 (`0x13572468`).
`--clock-step 9000` checks the 90 kHz RTP clock at 10 fps; loss mode permits
whole-frame multiples. `--compare-received` proves forwarded packet bytes
match the corresponding received ordinal, including duplicates. Inspect
reported access-unit counts and fault counters alongside receiver results.
Run the RTP checker controls against a real clean producer capture:

```sh
ruby ./test-checker.rb new-results/clean-ff-gst/received.rtp new-checker-results
```

FFmpeg rejects missing or non-increasing decoded timestamps. GStreamer
requires valid, nondecreasing PTS within each receiver segment and reports
`equal_pts`: its default slave jitter buffer can clamp successive local PTS.
Wire-clock progression remains a separate strict check. Restart creates a
new local segment; do not compare its PTS epoch to the previous pipeline.

The GStreamer receiver uses the real jitter buffer, H.265 depayloader/parser,
libav decoder, conversion and appsink. It requests/waits for a keyframe after
loss; periodic IDRs provide bounded recovery without a negotiated feedback
channel. UDP has no connection handshake. Restart means a real receiver
pipeline/socket teardown and rebind, not a transport reconnection promise.

Reports distinguish observed bus EOS, locally injected EOS, errors and idle
termination. Sender completion does not transmit UDP EOS. Relay TX means the
local kernel accepted `sendto`; decoded pixels prove delivery to the receiver.
The relay's ready file follows successful socket binding, and the receiver
starts before transmission. All test traffic stays on 127.0.0.1.

## Focused interfaces and limits

```text
gst-h265-rtp send RAW_I420 WIDTH HEIGHT FPS IPV4 PORT REPORT MAX_MS
gst-h265-rtp receive PORT RAW_OUT CSV REPORT MAX_MS IDLE_MS READY_FILE \
  [RESTART_AFTER_MS RESTART_PAUSE_MS]
ffmpeg-h265-receive INPUT.sdp OUT.i420 FRAMES.csv REPORT.json MAX_MS IDLE_MS READY
rtp-fault-relay --listen PORT --forward PORT --capture RX.rtp \
  --forward-capture TX.rtp --stats FILE --max-ms N --idle-ms N [fault options]
check-frames RAW.i420 [--frames N] [--require-count N] \
  [--allow-damaged --require-tail-start N --require-tail-count M]
```

Relay fault options are `--drop A,B,...`, `--drop-fu N`, `--drop-vcl-fu N`,
`--duplicate N`, `--reorder N`, `--blackout A:B`, `--blackout-ms A:B`, and
optional `--ready-file FILE`. Ordinals start at one; blackout ranges include
both endpoints. VCL FU selection counts only middle fragments of NAL types
0..31. Captures retain disposition and monotonic arrival/forwarding time.

Native instrumentation is bounded. Raw positive GStreamer Memcheck runs
retain process-lifetime GLib TLS contexts. Only the exact reviewed native
build may use the two possible-leak stack suppressions described in
[tests/memcheck](tests/memcheck/README.md). Raw and filtered results stay
separate; intentional leak/invalid-access controls must still fail. Those
suppressions are not for target libraries. Negative application paths and
the small GstMemory-map regression have separate unsuppressed checks.

Source diagnostics retain baseline findings and analyzer gaps. The production
memory guard removes two baseline Clang warnings; GCC still reports analysis
complexity limits. Cppcheck stops in an inherited GLib cleanup header, also
reproduced by a header-only control. No whole-stack static-analysis or
zero-retained-memory claim follows. Consult the linked acceptance record
for executed platforms, package lifecycle, test counts and remaining scope.
