# EmberBSD Examples

Standalone examples for [EmberBSD](https://github.com/oxtech-ember/EmberBSD),
a NetBSD-derived Unix for intelligent devices.

## Purpose

This repository turns EmberBSD capabilities into small, reproducible user
workflows. Each example owns its application code, prerequisites, build and
run instructions, tests and validation limits. Reusable third-party recipes
belong in Ports; kernel and driver fixes belong in the central OS project.

## Examples

- [Local text generation and speech recognition with llama.cpp and whisper.cpp](ai/local-inference/README.md)
- [ONNX Runtime CPU inference through installed C and C++ APIs](ai/onnx-cpu/README.md)
- [Answers from local documents with SQLite FTS5 and llama.cpp](ai/local-knowledge/README.md)
- [Simulated device telemetry and commands with Zenoh and ROS 2 Jazzy](robotics/zenoh-ros2/README.md)
- [A software BPSK radio channel with GNU Radio](robotics/gnuradio-channel/README.md)
- [Factor-graph optimization with known scalar and planar-pose solutions](robotics/gtsam-graph/README.md)
- [Point-cloud filtering and rigid registration with PCL](robotics/pcl-cloud/README.md)
- [Offline RGB-D SLAM and trajectory evaluation with ORB-SLAM3](robotics/orb-slam3-rgbd/README.md)
- [Map recorded RGB-D frames and reference poses with RTAB-Map](robotics/rtabmap-recorded-map/README.md)
- [Plan a path around a rectangle with OMPL](robotics/ompl-path/README.md)
- [UTM framebuffer validation (retired KDE 4 setup)](desktop/kde-utm/README.md)
- [GNOME desktop in the same UTM guest](desktop/gnome-utm/README.md)
- [Wayland client and compositor nested inside Xorg](desktop/wayland-nested/README.md)
- [Native Wayland and VirGL runtime probes](desktop/wayland-utm/README.md)
- [Plasma Mobile source configuration probe and current blockers](desktop/plasma-mobile/README.md)
- [Native nested Phosh session in EmberBSD Ports](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/phosh)

Examples contain no credentials or machine images. Review each example's
requirements and validation limits before applying it to a system.

## Related EmberBSD projects

[EmberBSD](https://github.com/oxtech-ember/EmberBSD#emberbsd-ecosystem) is the
central project and the entry point for the ecosystem.

- [EmberBSD](https://github.com/oxtech-ember/EmberBSD) — OS, drivers, boards and system builds.
- [EmberBSD-Ports](https://github.com/oxtech-ember/EmberBSD-Ports) — third-party recipes, patches and native dependencies used by examples.
- [EmberBSD-Runtime](https://github.com/oxtech-ember/EmberBSD-Runtime) — application execution and device operations; design stage.
- [EmberBSD-SDK](https://github.com/oxtech-ember/EmberBSD-SDK) — application contracts and development tools; design stage.
- [Ember-Agent-Skills](https://github.com/oxtech-ember/Ember-Agent-Skills) — portable developer skills for AI coding assistants and tested contributions.

## Connect developer skills

[Ember Agent Skills](https://github.com/oxtech-ember/Ember-Agent-Skills) provides
portable Agent Skills packaged with Agent Plugins. Load the package or the
complete skill directory using your development environment's supported
mechanism. Follow the [installation and validation guide](https://github.com/oxtech-ember/Ember-Agent-Skills#use-in-your-development-environment)
for the shared format and the separately tested Codex adapter.

Ask the `emberbsd-repository-guide` skill to select an example, verify its
dependencies and explain its tests.
The package supplies assistant instructions; it does not install a device runtime
or create missing SDK interfaces.
