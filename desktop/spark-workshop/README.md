# Spark Workshop

Spark Workshop presents a small local worktree guide as a touch-operated
workshop. It reads the selected revision's projected Markdown file, shows one
step at a time, and can attach a camera image to a step. The screen and event
flow are written in EmberBSD QML; the C application loads the guide, owns the
camera operation and passes its thumbnail to the QML Runtime.

## Guide and worktree projection

The application takes an absolute path to a local Markdown file as its first
argument. It does not depend on the current working directory or contact a
server. The guide format is intentionally small: one `#` title, then one or
more `##` step headings, each followed by one short paragraph. The reader
rejects unsupported Markdown, non-ASCII text, oversized files and guides that
do not fit the screen.

`demo-worktree.lua` exercises the public Spark worktree/view API with a
separate local cache. From this directory, seed the cache from the public
sample and export the selected revision while offline. Set the host Lua path
to the Spark modules and the installed `dkjson` module first, and point
`AGENT_LUA` at the native Spark host executable:

```sh
SPARK_ROOT=/path/to/EmberBSD-Spark
HOST_PREFIX=/path/to/host-pkg-llvm
AGENT_LUA=/path/to/agent-lua
export LUA_PATH="$SPARK_ROOT/lua/?.lua;$HOST_PREFIX/share/lua/5.5/?.lua;;"
"$AGENT_LUA" demo-worktree.lua /tmp/spark-workshop-cache notebook/connector.md seed
"$AGENT_LUA" demo-worktree.lua /tmp/spark-workshop-cache - offline > /tmp/connector.md
```

The second command enters the cached worktree and calls
`view:show(view.revision, "notebook/connector.md")`; the resulting file is the
application input. The seed response is a local demo fixture; this checks the
real Spark cache and projection code without a live server or account. It is
not the full Spark application or a live synchronization client. The public
guide and demo contain no private worktree data.

The example was checked with EmberBSD-Runtime revision
`23d00961f6bb8b5a022c0dafefb68ddd4808a75f` (including the application bitmap
setter) and EmberBSD-Spark revision
`a72ccec17a92e5edf2e5b3284c3054d6f245eb67` (lazy HTTP initialization).

## Build and run

Use the normal RV32 QML build environment described by
[`qml-wscons-rv32.sh`](../qml-wscons-rv32.sh). Set `OUT`, `HOST_PREFIX`,
`SYSROOT`, `BUILTINS`, `UNWIND`, `EMBER_SDK_SOURCE`, and
`EMBER_RUNTIME_SOURCE`, then run:

```sh
desktop/spark-workshop/build.sh
```

The script builds a static RV32 executable. Run it on a 480×480 wsdisplay
with the projected guide path:

```sh
/path/to/spark-workshop /tmp/connector.md
```

The default devices are `/dev/ttyE0`, `/dev/wsmouse0`, and `/dev/video0`; pass
`-d`, `-m`, or `-c` to select other device paths. Only one application should
own the display at a time. The camera page takes one 640×480 UYVY frame, shows
a thumbnail, and offers Retake or Use Photo. Back cancels capture and returns
to the same step. A used image remains attached while navigating the guide.

Accepted full-resolution BMP files are kept in `/tmp` while the application is
running so each step retains its photo for a later revisit. The app removes
those files when a photo is replaced or the app exits; photos are not durable
worktree attachments and are never written into the public example. The
thumbnail is held in memory. This example does not implement a video stream,
guide editing, or the full Spark client.

Temporary photo and Ask-conversion files use the absolute `TMPDIR` environment
variable when set, or `/tmp` otherwise. The application rejects a relative or
overlong directory path before opening the display. Both files are created
with `mkstemp` permissions and removed when replaced, cancelled or closed.

## Optional Spark answer

The local camera and guide workflow works without network access or an Ask
configuration. To enable the `Спросить` (Ask) action, provide absolute paths to the native
Spark Lua executable, its `examples/ask-photo.lua` worker, and a separate
configuration file. Build the [Spark runtime](https://github.com/oxtech-ember/EmberBSD-Spark/blob/main/docs/cross-build.md)
for the target and use the [photo worker packaging command](https://github.com/oxtech-ember/EmberBSD-Spark/blob/main/docs/runtime.md#ask-about-an-accepted-photo)
to install a standalone worker on the board. The example below uses an unpacked Spark source tree.

```sh
/path/to/spark-workshop /tmp/connector.md \
	-a /path/to/agent-lua \
	-s /path/to/EmberBSD-Spark/examples/ask-photo.lua \
	-f /path/to/private-spark-config
```

The `Спросить` action starts one worker for the current step and accepted photo. It
converts the saved BMP to PNG on a private temporary file descriptor and gives
the PNG on worker standard input; the worker receives the guide title, step
number, title, and paragraph as separate arguments. It returns one bounded
response line. The configuration and credentials remain outside the UI and
application binary. A request can be cancelled, retried after an error, and
read page by page. The worker's HTTP timeout is selected in its configuration;
the UI does not impose a shorter deadline. The current QML bitmap font accepts
printable ASCII, so the worker requests a short English response; unsupported
output is reported as an error rather than displayed as corrupted text.

## Checks and limits

Run the parser and BMP tests separately. To include the generated scene's
host-side touch-target test, pass the generated directory from a completed
build and set `EMBER_RUNTIME_SOURCE` and `CC` to the selected SDK inputs:

```sh
desktop/spark-workshop/tests/run.sh
CC="$HOST_PREFIX/bin/clang" HOST_SPNG_PREFIX="$HOST_PREFIX" \
	EMBER_RUNTIME_SOURCE="$EMBER_RUNTIME_SOURCE" \
	desktop/spark-workshop/tests/run.sh "$OUT/generated"
```

The tests cover guide validation, BMP-to-RGB565 conversion and, when requested,
the generated screen's navigation/camera hit targets. Set `HOST_SPNG_PREFIX`
to the native Ports prefix to explicitly include the BMP-to-PNG encoder test.
The guide reader accepts only the document subset above, and the camera path
requires the stated V4L2 format.
On Mosaico R5, the demo fixture was seeded and projected offline through Spark;
the 335-byte projected guide compared byte-for-byte with the expected content.
The target app started and presented its Home screen without wsdisplay errors.
The operator then confirmed the second step, `Inspect the latch` (`2 / 3`),
with a thumbnail of the captured workshop detail. This is a single-device
interaction check; it does not establish camera behavior on other boards or
durable photo storage.

The [Mosaico transport check](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/esp-mosaico/evidence/spark-workshop-ask-transport.json) records the board's HTTP and HTTPS results and a full-size synthetic photo request.
