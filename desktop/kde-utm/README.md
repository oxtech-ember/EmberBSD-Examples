# UTM framebuffer validation: retired KDE 4 setup

The original EmberBSD framebuffer test used KDE Workspace 4.11.22 from
NetBSD's binary package catalog. That choice established visible X11 output,
application windows and input; it did not establish a current desktop stack.

The KDE 4/Qt 4 setup is retired. Its installation and verification scripts
have been removed so they cannot reinstall an obsolete desktop. Their
original versions remain in Git history. Existing login configuration must
not be switched back to `startkde` after those packages are removed.

The current mobile-shell target is **Plasma Mobile 6.7.5**, the stable upstream
release on 2026-10-06. The port and its shared dependencies belong in
[EmberBSD Ports](https://github.com/oxtech-ember/EmberBSD-Ports), with original
source archives, checksums and portability patches. A missing binary package
is a porting task, not a reason to select KDE 4.

The modern Qt 6 applications Dolphin, Konsole and Kate remain useful test
clients. The previous GNOME 40/Xorg session is a recovery environment, not
the target GNOME release for EmberBSD. Neither old desktop is a dependency
of Plasma Mobile.

For the underlying display fix, see the
[EmberBSD framebuffer documentation](https://github.com/oxtech-ember/EmberBSD/blob/main/sys/dev/fdt/docs/utm-framebuffer.md).
The preserved original test used UTM on Apple Silicon, QEMU `virt`,
NetBSD 11/aarch64 userland and an EmberBSD kernel. Framebuffer/X11 validation
does not prove GPU acceleration, native Wayland, or physical-board support.

Source: [KDE Plasma 6.7.5 release](https://kde.org/announcements/plasma/6/6.7.5/).
