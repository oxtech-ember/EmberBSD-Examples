# Four Qt Multimedia streams

This C++17 example uses four real `QMediaPlayer` and `QVideoSink` pairs.
Each sink supplies decoded `QVideoFrame` objects through a bounded mailbox.
The application converts actual decoded pixels and paints four images in one
`QWidget` window with `QPainter`. This display path does not require OpenGL.
Use the current shared EmberBSD Qt 6.12 and FFmpeg providers.
Select a software decoder in the multimedia backend configuration.
The viewer also links the current FFmpeg9 `libswscale` and `libavutil` APIs.

## Build and run

Build with the Qt toolchain and sysroot from the matching Ports SDK:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_PREFIX_PATH=/usr/pkg/qt6 -DCMAKE_INSTALL_PREFIX=/usr/pkg
cmake --build build -j2
DESTDIR="$STAGE" cmake --install build
```

The toolchain must select the target `pkg-config` metadata for FFmpeg9.
For a native build, select the matching native FFmpeg9 metadata instead.

Run the installed program on a display with four different local files:

```sh
QT_MEDIA_BACKEND=ffmpeg qt-four-streams \
    channel0.mkv channel1.mkv channel2.mkv channel3.mkv
```

The default window is 1280×800. `--window-size WIDTHxHEIGHT` changes it.
All players start together. The program exits when all four report
`EndOfMedia`. A player error, invalid input or early window close fails.
Four different canonical file paths are required.
No audio output is attached.

`--ffmpeg-cpu-count N` accepts 1..64 and sets FFmpeg's process-wide CPU-count
hint before player creation. Automatic thread pools use this hint;
it does not change the machine's physical CPU count.
The default has no override. The report records the requested hint
(zero means no override) and the effective `av_cpu_count()` value.

For SDR YUV420P frames with explicit BT.709 transfer, matrix and video range,
the default CPU path scales luma and chroma to the actual image rectangle
before RGB conversion. It uses the current public `sws_scale_frame()` API,
nearest-neighbor scaling and one scaling thread per channel.
`--scaling-filter point|bilinear` selects the filter; the default is `point`.
The full-resolution decode and optional full-pixel oracle stay unchanged.
A cached padded YUV buffer copies only actual mapped pixels: Qt does not
guarantee the extra plane padding required by libswscale's SIMD readers.
The output is a viewport-sized native RGB32 `QImage`.

Other formats, HDR, cropped or transformed frames use Qt `toImage()`.
`--qt-conversion` forces that path for a conversion-only comparison.
The report records the actual filter, both conversion counts, the final image dimensions,
the loaded libswscale version, and verification/conversion wall microseconds.
It makes no performance claim until the selected target has been measured.

Sink notifications enter a mutex-protected mailbox of at most 16 frames
per channel. A precise GUI timer processes at most one frame per channel
every ten milliseconds. Producer callbacks access no widgets or GUI state.
An overflowing mailbox drops its oldest frame and records the coalescence.
Processed frames retain their actual timestamps and pixels; strict fixture
acceptance still fails when any expected ID is absent.
EOF validation waits for the bounded mailboxes to drain and paint requests
to settle, within the existing overall timeout.
The report separates sink notifications, dequeued frames, coalescences,
frames discarded on shutdown and each mailbox's peak occupancy.

## Finite acceptance mode

Build the streaming fixture generator with the **native** compiler.
It does not need Qt or a target sysroot:

```sh
cmake -S . -B build-fixture-native -DBUILD_VIEWER=OFF
cmake --build build-fixture-native --target i420-fixture -j2
ruby make-fixtures.rb "$NATIVE_FFMPEG" \
    "$PWD/build-fixture-native/i420-fixture" "$PWD/fixtures"
```

Use a native FFmpeg with the current `libx265` encoder.
Do not pass a cross-compiled generator to this host-side runner.
The default fixtures are four 2048×1500, 36-frame, 9 fps MP4 files.
For a small diagnostic run, add `64x64:36:9` as the fourth argument.
Each generator streams I420 through Ruby into one actual FFmpeg process.
The four encodes run sequentially. No full raw-video file is stored.
The generator allocates one luma row and one chroma row.

The encoder uses `lossless=1`, `keyint=9`, `min-keyint=9`, `scenecut=0`,
`bframes=0`, `pools=1` and `frame-threads=1`.
BT.709 limited-range metadata describes the output.
The MP4 video time base is 1/90000; at 9 fps each source step is 10000 ticks.
`fixtures.json` records actual raw-stream and encoded-file SHA256 values,
byte counts, tool binary hashes, the native FFmpeg version, exact argument
arrays and both subprocess exit statuses. Failed outputs remain marked failed.
Independently decode and inspect the files before using them as acceptance inputs.

```sh
QT_MEDIA_BACKEND=ffmpeg qt-four-streams --accept results \
    --timeout-ms 60000 --verify-fixtures 2048x1500:36:9 \
    --require-all-frames \
    fixtures/channel0.mp4 fixtures/channel1.mp4 \
    fixtures/channel2.mp4 fixtures/channel3.mp4
```

`results` must not exist. The program writes `report.json`, `frames.csv`
and a final `window.png`. It also writes the JSON report to standard output.
The report records actual Qt errors, metadata, frame dimensions and formats,
source timestamps, library paths, loaded libraries, EOF and display exposure.
`QT_MEDIA_BACKEND` records a requested selection; it does not prove which
backend loaded. Use the loaded library list and backend diagnostics as evidence.

Acceptance requires an exposed window, a real painted image in each quadrant,
four EOF signals and overlapping frame-delivery intervals and playing states.
`offscreen`, `minimal` and `minimalegl` platforms cannot satisfy this mode.
A VNC QPA display still requires real exposure; save independent client evidence.
`tests/rfb-capture.rb` requests full-frame samples at 1 Hz to reduce observer overhead; samples do not measure presentation FPS.
The default acceptance timeout is 60000 milliseconds.
Without `--accept`, there is no timeout unless `--timeout-ms` is set.

For a bounded live-source observation, add `--live-ms MILLISECONDS` to
`--accept`. Inputs remain four different local files, which can include SDP
descriptions when the actual multimedia backend supports their protocols.
The viewer does not add a general URL parser or guarantee network support.
Protocol support must be proved with real senders and the selected backend.

The observation starts after each sink delivers its first valid frame.
A precise timer checks real monotonic elapsed time before completion.
Every stream must deliver and paint a new frame during that interval.
All four players must still be playing at completion; an early EOF fails.
The global timeout bounds startup and observation together and must exceed
`--live-ms`. Player errors still fail immediately.
`--live-ms` and `--require-all-frames` cannot be combined.
Optional fixture checks still reject repeated or reversed IDs, including wraps.

The report marks successful timed completion as `live-observation`.
Ordinary file completion remains `all-eof`; no EOF flags are synthesized.
The live report includes requested and actual observation duration, monotonic
wall boundaries, and actual source PTS and frame IDs at those boundaries.
Observation delivery counts exclude deliveries already present at the start.
Observation paint counts include distinct images painted after the start.
The existing CPU and interval FPS measurements cover the measured viewer run,
including player startup. They do not become live-window-only measurements.
RSS remains the process lifetime peak.

`frames.csv` contains one row per valid processed sink frame and one per distinct
image painted while the window is exposed. A paint row refers to the latest
delivery number and source timestamp. The application does not invent frames
for decoder, sink or paint drops. Invalid frame notifications are counted
separately. Source timestamps are in microseconds, as reported by Qt.

Delivery and paint rates use `(count-1)/(last-first)` wall seconds.
Delivery timestamps measure GUI processing after bounded handoff.
`sink_signal_span_us` measures the actual producer notification interval.
These are observed application rates, not advertised media rates or decoder
throughput alone. CPU uses `getrusage(RUSAGE_SELF)` user plus system time over
the same measured wall interval. One fully used CPU core equals 100 percent.
Memory is the process peak resident set, not a per-stream allocation total.
The report converts NetBSD and Linux `ru_maxrss` KiB to bytes.
Startup, conversion, drawing and optional pixel checks contribute to these
application measurements. Snapshot and report serialization happen afterward.

## Optional deterministic fixture oracle

The viewer accepts ordinary media without `--verify-fixtures`.
That option enables an independent full-pixel oracle for controlled lossless
I420 fixtures. It does not substitute generated pixels for decoded video.
Use even dimensions of at least 16×16, 2–256 frames and an integer nominal
frame rate of 1–240. File index is channel `c`, from zero through three.

For each frame ID `f`, from zero through `FRAMES-1`, generate:

```text
Y(x,y) = bit(f, x/2) ? 235 : 16      when x < 16 and y < 16
Y(x,y) = 16 + ((x/64 + 3*(y/64) + 5*f + 41*c) % 220) otherwise
U(x,y) = 128
V(x,y) = 128
```

All divisions in this formula are integer divisions. The marker uses the
eight low bits of `f`. Encode these I420 frames with lossless H.265.
The application maps each delivered YUV420P frame, recovers its marker ID,
checks every active luma and chroma byte, then unmaps the frame.
Expected rows use block fills and `memcmp`; checks do not divide per pixel.
Map bounds, dimensions and pixel format are checked before any pixel read.
The subsequent image conversion and painting still use the actual video frame.

Repeated, reversed and corrupt frames fail.
Wrong-channel frames fail when the dimensions include pixels outside the marker.
The report lists every expected frame ID absent from the sink.
`--require-all-frames` fails when any expected ID is absent.
Without that option, missing IDs remain visible in the report.
The nominal frame rate labels the fixture; it never replaces observed rates.
Full-pixel verification adds CPU work and can affect scheduling.

## Checks

`tests/check-fixtures.rb FFMPEG FFPROBE FIXTURES NEW_OUT` decodes each encoded
file and compares every output byte with the generator's streaming hash.
It also checks the actual codec, dimensions, frame count and timestamps.

`tests/negative.rb VIEWER FIXTURES WIDTHxHEIGHT:FRAMES:FPS NEW_OUT` runs the real
viewer with exchanged channels, a truncated file, a missing file and an
existing output directory. Set `MEMCHECK=yes` to check these refusal paths
with Valgrind. Set `QT_QPA_FONTDIR` to the selected licensed font directory.

`tests/analyze.rb SOURCE BUILD NEW_OUT` imports the actual native GCC16
compilation database for the viewer and generator. It runs Cppcheck,
GCC's C analyzer and Clang's C++ analyzer. `CPPCHECK` can select a current
Cppcheck executable. The output keeps raw diagnostics and header-only
controls. A header parser failure remains a nonzero coverage limitation.
It is not a clean source analysis result.

This source has no hardware decoder, GPU, audio, camera, network receiver or
long-duration acceptance claim. Record the target, backend, display transport,
fixture hashes and observed measurements with each real execution.
