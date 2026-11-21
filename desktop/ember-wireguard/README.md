# ember-wireguard

A Qt 6 Widgets manager for EmberBSD desktops. The window layout follows the
official WireGuard client: a tunnel list with Active/Inactive status, detail
pane with public key and transfer counters, and actions Activate/Deactivate,
Import tunnel(s) from file, Add Empty Tunnel, Edit, and Delete.

## Behavior and dependencies

The tray reads `*.conf` files in priority order from `~/.config/ember-wireguard/`,
`~/.config/wireguard/`, and `/usr/pkg/etc/wireguard/` when readable by the user. Connect and disconnect
run the setuid helper `/usr/pkg/libexec/ember-wireguard-ctl`, which creates a
free `tunN` interface, starts `wireguard-go -f tunN`, applies `wg setconf`, and
adds IPv4 routes for peer `AllowedIPs`. For an IPv4 default route it uses two
more-specific `/1` routes and preserves a direct route to the peer endpoint.
Conflicting routes owned by another tunnel are rejected. Endpoint roaming or a
change of the underlying network requires disconnecting and reconnecting.

Configs may use wg-quick keys such as `Address`, `DNS`, and `MTU`. The helper
applies `Address` and `MTU` with `ifconfig` and strips those keys before
`wg setconf`. DNS changes are not applied. `Table`, `SaveConfig`, and lifecycle hooks are rejected.
IPv6 tunnel addresses and routes are not supported. Import and new
tunnels are stored under `~/.config/ember-wireguard/`.

Runtime needs `wireguard-tools`, `wireguard-go`, `qt6-qttranslations` for Russian
standard dialogs, a graphical session with a
system tray, session D-Bus, Qt 6 Widgets, and KDE Frameworks DBusAddons.
Source uses C++17 for the tray and C for the helper. One instance registers
per user session; a second launch activates the existing window.

The interface uses Russian when the system locale is Russian; otherwise it uses
English. Pass `--language ru` or `--language en` to override the choice for this
application without changing the desktop locale. Helper diagnostics retain their
original technical text.

CLI smoke checks: `ember-wireguard --version` and
`ember-wireguard --language ru --offscreen-smoke`. Building the Qt application
requires the native Qt `lrelease` executable; set `EMBER_WIREGUARD_LRELEASE`
to its path when cross-compiling.

Dependency recipes belong to [Ports](https://github.com/oxtech-ember/EmberBSD-Ports).
Kernel WireGuard (`if_wg`) is optional; this tray targets the userspace path.

Before upgrading from 0.1.2, deactivate all tunnels in that version. The new
helper does not adopt the old runtime state; reactivate tunnels after upgrading.

## Host parser checks

The `tunnel_test` target has no Qt dependency. It checks name validation and
config parsing. From the Examples repository root:

```sh
cmake -S desktop/ember-wireguard -B "$host_build" -G Ninja \
    -DCMAKE_C_COMPILER="$host_cc" -DCMAKE_CXX_COMPILER="$host_cxx" \
    -DBUILD_TESTING=ON -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build "$host_build"
ctest --test-dir "$host_build" --output-on-failure
```

## Source and license

BSD-2-Clause. Pattern follows `desktop/ember-wifi`. Version 0.1.3.
