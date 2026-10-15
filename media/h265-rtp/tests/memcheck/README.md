# Narrow native GLib TLS controls

The installed native GStreamer test uses GLib 2.90.1 and GStreamer 1.28.8.
GLib creates a global pool-spawner thread for non-exclusive thread pools.
That thread runs for the process lifetime. Its source calls
`g_ignore_leak(pool_spawner)`. There is no public API to join this global
spawner. GStreamer `gst_deinit()` already cleans its task pools, waits for
tasks, and disables unused pool threads.

The independent [control](pool-control.c) loads six installed plugins and runs one public
`GstTaskPool` task. It cleans the task pool, elements and GStreamer through
their public APIs. It performs no encoding, decoding or network I/O.
Raw Memcheck reports the same two possible TLS contexts as the RTP sender:
608 bytes in two blocks, with zero definite or indirect leaks. A separate
GLib-only pool control reports the same allocation stacks with 544 bytes.
The TLS vector size depends on the loaded modules.

The suppression file contains only those two exact allocation stacks.
It matches `Memcheck:Leak` with leak kind `possible`. It contains no wildcard
or ellipsis frame. One stack requires GStreamer's `default_prepare` caller.
The other requires GLib's full pool-worker creation chain. No invalid access
or definite/indirect leak is suppressed. Keep the raw result and the filtered
result as separate evidence. This is not a claim that GLib frees all state.

Use the file only for this native library set, with Valgrind 3.24.0,
`--keep-debuginfo=yes`, and strict definite/indirect/possible leak checks.
Check these SHA256 values before each filtered run. A different build needs
new raw proof and review, even if its version string is unchanged.

| Native library | SHA256 |
| --- | --- |
| `libglib-2.0.so.0.9000.1` | `f3a4c79633309c869c3045b51f9aafdccd8fa011c8db3fd9f25273a7423b5f41` |
| `libgstreamer-1.0.so.0.2808.0` | `4e16b2ecd71531a169777e226e1997f993424897ec9ae406cdeedcde7bc65abf` |
| glibc 2.41 `libc.so.6` | `9792e3cbb541c8f44c7acf5f14f4022ea62998ecc787d326bed4d8b6547dfd92` |
| glibc 2.41 `ld-linux-x86-64.so.2` | `c8438e4fde1934e61c88311633f00949ff645d5c04cdb8671fa3d78164d2f307` |

These hashes describe the native analysis build. They do not describe the
EmberBSD target libraries. Do not use this file for target execution.
The leak and invalid-write controls must still exit with Memcheck's error
code when the file is enabled.

Build the control with the selected native `gstreamer-1.0` pkg-config entry.
Run its normal mode both without and with the suppression file.
Run `--leak` and `--invalid-write` with the file enabled. Each negative mode
must still fail. Keep definite, indirect and possible leaks as errors.

Sources:

- [GLib 2.90.1 thread pool](https://github.com/GNOME/glib/blob/2.90.1/glib/gthreadpool.c)
- [GStreamer 1.28.8 task pool](https://github.com/GStreamer/gstreamer/blob/1.28.8/subprojects/gstreamer/gst/gsttaskpool.c)
- [GStreamer 1.28.8 cleanup](https://github.com/GStreamer/gstreamer/blob/1.28.8/subprojects/gstreamer/gst/gst.c)
