# ember-wifi

A small Qt 6 Widgets application for EmberBSD desktops. It provides a tray icon
with connection state and a window with nearby wireless networks.

## Behavior and dependencies

The application talks directly to the `wpa_supplicant` control socket under
`/var/run/wpa_supplicant`. The user must have permission to access that socket.
Passwords are sent through the control interface, without a command-line argument.
Saving network changes requires `update_config=1` in the supplicant configuration.

Open and WPA/WPA2-PSK networks are supported. WPA2/WPA3 transition networks use WPA2.
WPA3-only, Enterprise, and WEP networks are displayed as unsupported.
During connection or negotiation, refresh reads cached scan results.
Active scans run only in the disconnected or inactive state.
This avoids starting a scan that can interrupt an established connection.

The application needs a graphical session with a system tray, Qt 6 Widgets,
and a running `wpa_supplicant`. Its source uses C++17.
Dependency recipes belong to [Ports](https://github.com/oxtech-ember/EmberBSD-Ports).
Kernel, radio, and supplicant mechanisms remain in [OS](https://github.com/oxtech-ember/EmberBSD).
Read [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards) for the exact board's radio support limits.
The application does not establish those hardware capabilities.

## Cross-build the application

Use the current GCC16 toolchain and a matching EmberBSD target sysroot.
Prepare the toolchain and Qt dependency through
[SDK](https://github.com/oxtech-ember/EmberBSD-SDK) and Ports.
Set `toolchain` to the target's CMake toolchain file.
Set `qt_prefix` to the matching target Qt installation prefix.
Set `build` to an output directory outside the source tree.
From the Examples repository root:

```sh
cmake -S desktop/ember-wifi -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_PREFIX_PATH="$qt_prefix"
cmake --build "$build" --target ember-wifi wpa_test
```

CMake creates the application target only when it finds Qt 6 Widgets.
Building the named `ember-wifi` target therefore checks that the dependency was found.
Run the target test and application only on the selected compatible system.
Use the CMake install rules to stage `bin/ember-wifi` and
`share/applications/ember-wifi.desktop` for the chosen package or deployment.
Choose desktop autostart policy separately for the intended user.

## Host parser and command checks

The existing `wpa_test` target has no Qt dependency.
It checks reply parsing, network grouping, scan selection, passphrase validation,
and the commands prepared for a network.
Set `host_cxx` to the host GCC16 C++ compiler and `host_build` to a separate output directory:

```sh
cmake -S desktop/ember-wifi -B "$host_build" -G Ninja \
    -DCMAKE_CXX_COMPILER="$host_cxx" -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build "$host_build" --target wpa_test
ctest --test-dir "$host_build" --output-on-failure
```

The existing host test passes with GCC 16.2.0 on macOS arm64.
These checks do not open a supplicant socket or exercise a graphical session.
Target GUI operation, authentication, saved settings, and actual radio behavior need separate checks.

## Source and license

The application and its test were moved without source changes from
[EmberBSD `ember/development/ember-wifi`](https://github.com/oxtech-ember/EmberBSD/tree/4a4c98bb99999836103d3fe63fd6f4c98013f0c5/ember/development/ember-wifi).
The source and CMake files retain their `BSD-2-Clause` SPDX identifiers.
This move changes ownership and documentation; it does not qualify a new target build or device scenario.
