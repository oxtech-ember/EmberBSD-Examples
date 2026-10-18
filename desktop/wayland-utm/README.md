# Native Wayland and VirGL in UTM

Experimental runtime checks for the current EmberBSD/NetBSD 11 aarch64 guest.
Build recipes, original source URLs, hashes and portability patches belong to
[EmberBSD Ports](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/wayland-utm).
The kernel integration belongs to
[EmberBSD](https://github.com/oxtech-ember/EmberBSD/blob/main/ember/boot/utm-virgl-design.md).

## Current evidence

On 2026-10-06, the private libdrm 2.4.134, Mesa 21.3.9 (VirGL and softpipe),
wlroots 0.19.3 and labwc 0.9.7 stack compiled natively. Mesa passed 81 tests,
labwc passed three, and libdrm passed three with one device-dependent skip.
The EGL probe below passed all pixels using softpipe and exited normally.
The loaded EGL, GBM, GLES and libdrm libraries came from the private prefix.

Native labwc/Pixman ran Kate without an Xorg parent on experimental kernel
`67a611acb9b`. Absolute pointer motion, a client-menu click and saved text
were checked; the user also confirmed physical mouse and keyboard operation.
VT handoff, visible console recovery and VirGL acceleration remain unverified.
Preserve the framebuffer/GNOME/Xorg recovery path. A software or nested check
is not GPU support. Mesa 21.3.9 is a temporary comparison/recovery stack;
replace it after the current common Mesa and its rebuilt consumers pass the
same lifecycle, pixel and native session checks.

## EGL allocation and readback

Build the probe as an ordinary user against the private graphics prefix:

```sh
sh build-probe.sh "$HOME/.cache/emberbsd-wayland-build/install" ./egl-readback
LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=softpipe ./egl-readback --software
```

The probe creates 64 texture/framebuffer pairs, paints four quadrants,
waits for rendering, checks every RGBA pixel, and destroys its context.
This detects transfer/lifetime failures and premature process-exit crashes.
The build helper checks pkg-config provenance for EGL, GLES, GBM and libdrm.
NetBSD's system libm may emit its existing compatibility `cabs`/`cabsf` and
`_tgammal` precision warnings while linking this Mesa stack.

Once the kernel implements the standard VirtGPU 3D ABI and UTM provides a
GL-enabled device, run without inherited software overrides:

```sh
unset LIBGL_ALWAYS_SOFTWARE GALLIUM_DRIVER MESA_LOADER_DRIVER_OVERRIDE
./egl-readback /dev/dri/renderD128
```

GPU mode requires a VirGL renderer and rejects softpipe/llvmpipe. It still
requires separate evidence that UTM's host renderer uses the Mac GPU.
Record the actual host backend and device settings; the Metal presentation
window alone does not establish acceleration. An inaccessible render node
is a failed GPU check, not an invitation to widen CREATE_DUMB permissions.

## Zink shader and pbuffer check

Use the current common Mesa package with Zink. Select one Vulkan ICD manifest.
First run the Ports PowerVR enumeration check against that manifest and the
selected render node. Require the hardware vendor, open-source PowerVR driver
ID and render-node identity. Record the manifest and loaded library hashes.
This EGL check does not replace that Vulkan identity check.

Build the existing consumer with the common GCC16 cross compiler. Use the
application sysroot that supplies the target EGL, GLES2 and GBM headers and
libraries. Do not use host library paths.

```sh
"$CC" --sysroot="$SYSROOT" -std=c99 -D_NETBSD_SOURCE -Wall -Wextra -Werror \
  -I"$SYSROOT/usr/pkg/include" egl-readback.c \
  -L"$SYSROOT/usr/pkg/lib" -Wl,-rpath-link,"$SYSROOT/usr/pkg/lib" \
  -Wl,-rpath,/usr/pkg/lib -lEGL -lGLESv2 -lgbm -o "$OUTPUT"
```

Set `CC` to the GCC16 target compiler. Set `SYSROOT` to the application sysroot.
Set `OUTPUT` to a new target output file. Record the compiler version and hashes.
The native `build-probe.sh` interface remains available for its original case.

On the target, use the exact device name from the accepted Vulkan check:

```sh
unset LIBGL_ALWAYS_SOFTWARE VK_ICD_FILENAMES VK_ADD_DRIVER_FILES
unset VK_ADDITIONAL_DRIVER_FILES
VK_DRIVER_FILES="$ICD_MANIFEST" MESA_LOADER_DRIVER_OVERRIDE=zink \
  GALLIUM_DRIVER=zink ./egl-readback --zink "$VULKAN_DEVICE_NAME" ./zink.ppm
```

Use a new image path. The consumer refuses an existing file. It requires one
absolute `VK_DRIVER_FILES` path. It rejects conflicting legacy ICD settings
and software overrides. It requires Zink and the expected device name in
`GL_RENDERER`. It rejects llvmpipe, softpipe, lavapipe and SwiftShader names.
Keep the ICD and library cohort unchanged between enumeration and rendering.
Record actual loaded library paths during hardware acceptance.

The check creates an EGL surfaceless display and a 64 by 64 pbuffer. It compiles
and links GLES2 shaders. It draws four quadrants and checks every RGBA channel
against an independent colour pattern. It saves the checked pbuffer as a PPM
image. It then repeats shader drawing and readback over 64 texture/FBO pairs.
The Zink modes also record the GLSL, GL and EGL extension strings.
Use `--zink-es3` in place of `--zink` to require a real GLES3 configuration
and context. It fails when that profile is unavailable. It does not change
Mesa capability bits or accept a version override. A GLES2 pass does not
establish GLES3 or the capabilities required by a compositor.

A final success requires context, surface and display cleanup. This check does
not establish KMS scanout, dma-buf sharing, synchronization or a Plasma session.

On 2026-10-18 UTC, consumer `8c1e33a` passed on one physical A733 board.
The kernel was `e3d9ba3a344579c12ee9398ab3a3a5bc64a67b0d`.
A private MesaLib 26.2.4nb8 payload used the existing target providers.
The check selected the open-source PowerVR ICD and reported EGL 1.5,
OpenGL ES 2.0 and `zink Vulkan 1.2(PowerVR B-Series BXM-4-64 MC1
(IMAGINATION_OPEN_SOURCE_MESA))`. The pbuffer and all 64 FBO checks passed.
An independent comparison matched all 4096 PPM pixels. Package installation,
HDMI scanout, PRIME and Plasma were outside this check. The PCB revision
was not recorded. Mesa required its explicit upstream experimental option
`PVR_I_WANT_A_BROKEN_VULKAN_DRIVER=1`; Vulkan conformance was not established.

Run the pure renderer and pixel contracts on the host:

```sh
"$HOST_CC" -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/readback-checks.c -o "$TEST_OUTPUT"
"$TEST_OUTPUT"
```

These contracts reject software identities and corrupted or stale images.
They do not execute a GPU. Check the actual consumer separately with Cppcheck,
GCC `-fanalyzer` and Clang Static Analyzer using the target headers and flags.
Record each analyzer version and its scope. Hardware execution remains required.

## GEM and PRIME lifetime probe

After booting a reviewed experimental DRM kernel, first run the memory probe
without a compositor. The caller needs access to the primary DRM node:

```sh
sh build-memory.sh "$HOME/.cache/emberbsd-wayland-build/install" ./drm-memory
./drm-memory /dev/dri/card0
```

It rejects malformed dimensions and invalid handles, then repeats one-page
and 8 MiB dumb-buffer allocation. A child imports the exported PRIME fd into
a fresh DRM file and maps through both the dma-buf fd and GEM handle. Both
processes close every handle/fd before the child verifies all mapped words
and shared writes. Only mappings keep the final object alive. This checks
native mmap offsets, cross-process sharing and fork/close lifetime, without
claiming scanout or GPU rendering. Kernel memory counters are still needed
to prove all objects are reclaimed. On 2026-10-06, malformed requests and
32 cross-process mapping lifetime cycles passed on experimental kernel
`67a611acb9b`. This does not establish 3D command execution or fenced GPU
buffer sharing.

## Native session probe

Preserve the known-good kernel, UTM display settings and login configuration.
Use the existing desktop account. The kernel must expose working DRM/KMS,
and seatd must correctly restore NetBSD's wscons keyboard mode on exit; see
the companion Ports probe. Complete visible KMS checks before this step.

Log out of the graphical desktop and stop its display manager through a
separate administrator connection. Run from a text console as the ordinary
desktop user, keeping that recovery connection available:

```sh
sh run.sh "$HOME/.cache/emberbsd-wayland-build/install" software
# For a QEMU USB Tablet, use Ports' separately built wscons input fix:
sh run.sh "$HOME/.cache/emberbsd-wayland-build/install" software \
    "$HOME/.cache/emberbsd-libopeninput-build/install"
# After successful software KMS/input and VirGL pixel checks:
sh run.sh "$HOME/.cache/emberbsd-wayland-build/install" virgl \
    "$HOME/.cache/emberbsd-libopeninput-build/install"
```

The launcher forces the DRM/libinput backends and card0, creates a private
runtime directory, and uses seatd to give the ordinary user device access.
It starts a separate D-Bus session and native Wayland Kate. Save a sentence,
use menus, test modifiers and pointer coordinates, then close Kate to exit.
Ctrl+Alt+Escape also terminates the compositor. Logs, library paths and the
saved file remain in the printed mode-0700 temporary directory.
The optional third argument selects an absolute private input prefix built
with [Ports' libopeninput recipe](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/wayland-utm/build-libopeninput.sh).
It must contain a readable `lib/libinput.so.10`. Its library directory is
prepended to the compositor's loader path after the setuid launcher boundary.
Two-argument invocation retains the packaged input library path.
`prefixes.txt` records the selected graphics/input prefixes and requested
loader path; confirm actual mapped paths separately during acceptance.
The compositor's private library path is restored after `seatd-launch` drops
privileges, because NetBSD removes it at the setuid boundary. Packaged Qt
links base EGL/GL SONAMEs that differ from the private Mesa. Kate therefore
runs as a native software/SHM input client with its packaged libraries and
cleared private loader paths. The explicit EGL probe tests the private GPU
stack separately. A client completion receipt
detects failed startup or crashes even when labwc itself returns success.
Exit through the compositor shortcut before Kate completes is reported as
an interrupted probe, with its evidence retained.

Software mode selects wlroots Pixman. VirGL mode selects GLES and removes
the compositor's known software overrides. Kate remains a software/SHM
input client in both modes; this does not test accelerated client sharing. The
launcher never creates users, changes passwords, enables automatic login,
or changes the default display manager. Xwayland is disabled in this first
build; native GNOME Wayland remains a separate porting task.

The patched wscons input library has been checked with QEMU USB Tablet
absolute motion and a left click opening Kate's Edit menu. CUA/UTM automated
clicks may warp the host pointer without delivering guest motion, and a drag
may press before its first coordinate update. A click then uses the previous
guest position. Deliver motion to the target first and click at that same
point, or test with a physical pointer. Captured guest events distinguish
this automation artifact from a guest input bug. Automated keyboard batches
also dropped keys. The user subsequently confirmed physical mouse and
keyboard operation after a typing/save check; the saved client file was
verified separately. Modifier press/release traces were also checked.
These input checks establish neither VT handoff nor VirGL acceleration.

Acceptance also requires VT handoff, crash/exit recovery, same-device
cross-process PRIME sharing, malformed-ioctl checks, allocation stress,
in-flight client termination and a thirty-minute runtime test. Retest the
saved framebuffer/Xorg recovery path after changing the UTM display device.
