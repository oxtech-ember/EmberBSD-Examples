# GNOME in UTM on Apple Silicon

This example adds a GNOME 40 X11 session to the current EmberBSD UTM demo.
It uses GNOME Shell 40.2, Mutter 40.2 and NetBSD 11/aarch64 binary packages.
It does not replace the EmberBSD kernel, create accounts or enable autologin.
An existing KDE session is saved before GNOME becomes the XDM default.

## Requirements

- The framebuffer boot configuration described in the [KDE example](../kde-utm/README.md):
  QEMU `virt` with HVF, `virtio-ramfb`, USB keyboard/tablet, native X11 sets,
  an EmberBSD kernel containing `b4f718d`, and `userconf disable viogpu` at boot.
  KDE packages themselves are not required.
- Tested VM: four vCPUs, 4 GiB RAM, 24 GiB disk. Allow at least 2 GiB free.
- An existing non-root account with a known password and root shell access.
- `pkgin` configured for the tested package set:
  `https://cdn.netbsd.org/pub/pkgsrc/packages/NetBSD/aarch64/11.0_2026Q2/All`.
  Give this setup exclusive use of the package database until it finishes.
- NetBSD base compiler and network access to official GNOME source archives.

The compatibility code is specific to this package set. Revalidate it after
upgrading GNOME, GLib, introspection, Mutter or the schema package.

## Install

Copy this directory to the guest, then run as root:

```sh
sh setup.sh EXISTING_USER
```

Setup installs the applications, builds private compatibility files under
`/usr/local/share/emberbsd-gnome`, configures D-Bus/XDM and selects GNOME in the
account's `.xsession`. It does not stop the current desktop. Save work, reboot,
then enter the account name in XDM, press Enter, enter the password and press
Enter again. Password characters appear as asterisks.

Keep UTM input capture disabled for the USB tablet. See the KDE example's
input recovery notes if a modifier appears stuck after changing capture mode.

In GNOME Terminal, run:

```sh
sh /usr/local/share/emberbsd-gnome/verify.sh
```

Also open Files (Nautilus), GNOME Terminal and Text Editor (gedit). Check mouse
navigation, keyboard entry, saving/reopening a file, logout and login after
reboot. The command checks cannot establish those visible results by themselves.

## Compatibility fixes

Package-owned files remain unchanged. Three private components are required:

1. `build-schemas.sh` merges installed desktop schemas with missing GNOME 40 keys
   and schema IDs. Schema 50.1 removed `toggle-shaded`, which crashes GNOME 40.
   Existing modern keys/defaults remain intact; a separate session-local default
   file selects installed fonts and wallpaper. Explicit user preferences win.
2. `build-typelib.sh` repairs the installed Shell GIR's missing
   `GioUnix.DesktopAppInfo` metadata for `Shell.App.app_info` and `get_app_info`.
   Without it, JavaScript application enumeration fails. The builder compiles
   a private typelib and asserts both members are accessible from GJS.
3. `build-launcher.sh` rebuilds only upstream GNOME Shell's C launcher against
   the installed libraries. Its original startup overrides `GI_TYPELIB_PATH`;
   the small patch prepends the repaired typelib after the original paths.
   The NetBSD signal-handler patch is preserved. Native `libintl.so.1` is used
   to match the installed Shell, avoiding a second gettext ABI.

GNOME's library, JavaScript resources and Mutter remain the binary packages.
The schema builder checks both restored `toggle-shaded` and retained modern
`accent-color`; the typelib builder fails on the original missing-member bug.
The visible shell and application checks validate the launcher's path priority.
`pkg_admin check` validates package files before selecting the new session.

## Source provenance

| Input | SHA256 |
|---|---|
| [gsettings-desktop-schemas 40.0](https://download.gnome.org/sources/gsettings-desktop-schemas/40/gsettings-desktop-schemas-40.0.tar.xz) | `f1b83bf023c0261eacd0ed36066b76f4a520bbcb14bb69c402b7959257125685` |
| [gnome-shell 40.2](https://download.gnome.org/sources/gnome-shell/40/gnome-shell-40.2.tar.xz) | `4e9d829b039fa0add33bb6583fc7b4e028ed8dcff7af8a577e09cc66988c281c` |
| [NetBSD pkgsrc main.c patch, 2026Q2](https://github.com/NetBSD/pkgsrc/blob/pkgsrc-2026Q2/x11/gnome-shell/patches/patch-src_main.c) | `0e0826872ec8e1fc7955e1c59dbfa75da86087800c385cb939a63ace6ac06936` |

Inputs were retrieved on 2026-10-06. Source archives retain upstream authorship
and licenses; builders copy their COPYING files alongside generated outputs.
The schema AUTHORS file is also preserved. `netbsd-main.patch` is an unchanged
pkgsrc patch with its original revision identifier. `launcher-typelib.patch`
and the XSLT transformations are EmberBSD adaptations, not accepted upstream
changes. Generated sources, typelibs and executables are local build outputs;
do not commit them or distribute them without the upstream license materials.

## Verified scope and limits

GNOME Shell, overview, Nautilus, GNOME Terminal and gedit run on X11 `wsfb`
with Mesa llvmpipe software rendering at 800×600. Hardware acceleration,
Native Wayland, physical-board graphics, sound, suspend and long-duration stability
are not established by this example. Standard NetBSD service gaps can produce
nonfatal GNOME settings-daemon warnings.

The tested binary catalog does not provide `gnome-control-center` or GDM.
The Settings application is therefore unavailable; login uses base XDM.
This is a functional GNOME 40 desktop test, not a complete modern GNOME release.
Background and font defaults are scoped to this GNOME session.
Login through XDM and the application/network/file checks also passed after
reboot. A separate [nested Wayland test](../wayland-nested/README.md) runs a Qt
compositor and Kate inside Xorg; it does not turn GNOME into a Wayland session.

## Restore the previous session

As root, replace `EXISTING_HOME` with the account's home directory:

```sh
cp -p EXISTING_HOME/.xsession.before-emberbsd-gnome EXISTING_HOME/.xsession
```

Log out and log in again. Installed GNOME packages and compatibility files may
remain. Re-running setup keeps the original `.xsession` backup and retains
previous private build directories with timestamped `.before-...` names.

Reference: [GNOME on NetBSD](https://wiki.netbsd.org/GNOME/).
