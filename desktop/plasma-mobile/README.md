# Historical Plasma Mobile 6.5.2 feasibility probe

**Historical result: 6.5.2 stopped at source configuration.**

The current port targets stable Plasma Mobile 6.7.5 in
[EmberBSD Ports](https://github.com/oxtech-ember/EmberBSD-Ports). This old probe
records an earlier investigation. Choose the EmberBSD desktop version from
the current common Ports recipe and upstream release policy. KDE 4 setup is retired.

This probe configures the unmodified Plasma Mobile 6.5.2 release on
EmberBSD/NetBSD. Version 6.5.2 matches the Plasma libraries in the tested
NetBSD 11/aarch64 `11.0_2026Q2` catalog; it is not a recommendation of the
newest upstream release.

It preserves the existing desktop and uses a fresh directory under the
ordinary user's `~/.cache`. It does not install the shell, change login
configuration, or claim to test a physical phone.

## Run

Prerequisites are the Qt 6 and KF6 development packages already present in
the original test environment, including Qt Quick and Qt Wayland.\nThe retired KDE 4 setup is not a prerequisite or a supported installation path.
On that installation, these additional tools are needed (as root):

```sh
pkgin -y install cmake extra-cmake-modules qt6-qtsensors ninja-build curl
```

Then, as an ordinary user:

```sh
sh probe.sh
```

An already downloaded archive can be supplied as the sole argument. The
same mandatory SHA256 check applies before extraction. Sources, original
licenses, configuration output and installed package versions are retained
in the directory printed by the script. A CMake failure is returned as a
nonzero exit status. A successful configuration would still require a build
and runtime verification.

## Observed blockers

The native probe used the `EMBER64` kernel from `b4f718d`, NetBSD 11/aarch64,
GCC 12.5.0, CMake 4.3.3, Qt 6.11.1 and KDE Frameworks 6.26.0.

CMake found Qt/Wayland and the installed KF6 libraries, then failed because
`KF6ModemManagerQt` and `KF6NetworkManagerQt` were absent. Neither was
available in the tested binary catalog. This is the first configuration
failure, not a complete list of work required for a port.

The catalog also lacks Plasma Mobile, `kwin_wayland` and Plasma Workspace
(`plasmashell`). It contains `plasma6-kwin-x11`, which cannot provide the
Wayland compositor expected by Plasma Mobile.

The [pkgsrc 2026Q2 Plasma Workspace recipe](https://github.com/NetBSD/pkgsrc/blob/pkgsrc-2026Q2/x11/plasma6-plasma-workspace/Makefile)
is marked `BROKEN` because it needs `wip/plasma6-kwin`. The experimental
[KWin recipe](https://wip.pkgsrc.org/cgi-bin/gitweb.cgi?p=pkgsrc-wip.git;a=blob_plain;f=plasma6-kwin/Makefile;hb=HEAD)
retrieved on 2026-10-06 lists unresolved libinput functions and passes
`--warn-unresolved-symbols` to the linker. That recipe is evidence of work
in progress, not a validated running compositor.

A port must address these prerequisites before trying upstream's nested
session command. Even a working nested session would not establish native
display, touchscreen, rotation, suspend, battery or modem support.

The probe itself uses POSIX shell. Upstream's CMake/ECM configuration also
detected the installed Python 3.13 interpreter; the upstream build is not
claimed to be Python-free.

## Source provenance

- [Official Plasma Mobile 6.5.2 source archive](https://download.kde.org/stable/plasma/6.5.2/plasma-mobile-6.5.2.tar.xz).
- SHA256: `6eca5d046ed46acdaedc64a1508c06e81cc9a205a0ca1609e88a94e9078b8067`.
- Downloaded on 2026-10-06. The archive is extracted unchanged, retaining
  all copyright notices, SPDX identifiers and bundled licenses.
- [Upstream build and session instructions](https://invent.kde.org/plasma/plasma-mobile/-/blob/v6.5.2/README.md).
