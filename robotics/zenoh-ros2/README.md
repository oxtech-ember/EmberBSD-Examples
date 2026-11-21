# EmberBSD controller to ROS 2 through Zenoh

A C controller publishes **simulated** temperature and accepts a simulated
setpoint. A C++ bridge exposes typed ROS 2 topics on Ubuntu 24.04 / Jazzy.
Both endpoints use Zenoh-Pico 1.10.2 from EmberBSD Ports. No actuator is used.

```text
EmberBSD: robot-controller (C, Zenoh peer TCP listener)
    <---- state / command / acknowledgement ---->
Ubuntu: robot-bridge (C++, Zenoh client + rclcpp)
    <---- typed ROS 2 topics, Fast DDS ---->
Ubuntu: robot-probe or another ROS 2 application
```

This is an explicit application bridge, not an implementation of the
`rmw_zenoh` wire protocol or a ROS 2 port to EmberBSD. The bridge is a Zenoh
**client**, which activates automatic reconnect to the listening peer.
No `zenohd` process is required. Zenoh multicast scouting is disabled;
the device endpoint is configured explicitly. ROS discovery runs on the ROS host.

## Requirements

- Device: EmberBSD/NetBSD 11 AArch64, C11 compiler, CMake, POSIX threads,
  `/dev/urandom`, TCP, and the [Ports robotics package](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/robotics).
- ROS host: Ubuntu 24.04, ROS 2 Jazzy, `rclcpp`,
  `rosidl_default_generators`, `rmw_fastrtps_cpp`, a C++17 compiler and CMake.
  Jazzy is the supported LTS chosen for this integration.
- Both hosts: the same patched Zenoh-Pico 1.10.2. One installation per host
  serves all example binaries. The TCP send-timeout patch is required.
- A trusted test network and an unused TCP port. This example has no
  authentication, encryption or authorization for commands.

ROS upstream tooling and generated bindings require Python. Project-owned
sources and checks use C, C++, CMake and shell.

## Build the controller

Clone this repository. Install `zenoh-pico` with pkg_tools as described in
Ports. With the default `/usr/pkg` prefix:

```sh
example=$PWD/robotics/zenoh-ros2
work=/var/tmp/ember-robot-demo
mkdir "$work"
cmake -S "$example" -B "$work/controller" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr/pkg
cmake --build "$work/controller" --parallel 2
LD_LIBRARY_PATH=/usr/pkg/lib ctest --test-dir "$work/controller" --output-on-failure
LD_LIBRARY_PATH=/usr/pkg/lib "$work/controller/robot-controller" tcp/0.0.0.0:17447
```

Use the private runtime prefix instead of `/usr/pkg` when testing an isolated
package installation. Keep the controller running in its terminal. Ctrl-C
stops it. It does not install or start a system service.

The controller publishes state every 250 ms. Temperatures are integer
millidegrees Celsius; the measurement is synthetic and varies around 22 °C.
The initial setpoint is 20 °C. Only the simulated setpoint is changed.

## Build the ROS side

Follow the [official Jazzy Ubuntu repository setup](https://docs.ros.org/en/jazzy/Installation/Ubuntu-Install-Debs.html), then install:

```sh
sudo apt install build-essential cmake ninja-build curl ca-certificates \
  ros-jazzy-ros-base ros-jazzy-rosidl-default-generators
```

Clone Ports beside Examples. In Bash, build the same library with its runtime
patch; pkgsrc's test-only patch is not needed for this Linux library build:

```sh
ports=/absolute/path/to/EmberBSD-Ports
example=/absolute/path/to/EmberBSD-Examples/robotics/zenoh-ros2
work=$HOME/ember-ros-demo
mkdir "$work"
cd "$work"
revision=6a881c21dad1f778d0529eb9f297bea339016526
curl --fail --location --output zenoh.tar.gz \
  "https://codeload.github.com/eclipse-zenoh/zenoh-pico/tar.gz/$revision"
printf '%s  %s\n' \
  a1b816a05e5af3d9e50863088987f4b2608c1243507ba833ea5549a35222c9cb \
  zenoh.tar.gz | sha256sum -c -
tar xzf zenoh.tar.gz
patch -d "zenoh-pico-$revision" -p0 < \
  "$ports/pkgsrc/local-robotics/zenoh-pico/patches/patch-src_link_transport_tcp_tcp__posix.c"
cmake -S "zenoh-pico-$revision" -B zenoh-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$work/prefix" \
  -DBUILD_SHARED_LIBS=ON -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF \
  -DBUILD_TOOLS=OFF -DZ_FEATURE_AUTO_RECONNECT=1
cmake --build zenoh-build --parallel 2
cmake --install zenoh-build
source /opt/ros/jazzy/setup.bash
cmake -S "$example/ros" -B ros-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$work/ros-install" \
  -DCMAKE_PREFIX_PATH="$work/prefix"
cmake --build ros-build --parallel 2
cmake --install ros-build
source "$work/ros-install/share/ember_zenoh_bridge/local_setup.bash"
export LD_LIBRARY_PATH="$work/prefix/lib:${LD_LIBRARY_PATH:-}"
export ROS_DOMAIN_ID=86 RMW_IMPLEMENTATION=rmw_fastrtps_cpp
```

This uses a direct CMake install. Its package-specific `local_setup.bash`
exists; a colcon workspace root `install/setup.bash` is not created.
Repeat both `source` commands and the exports in another ROS terminal.

## Exercise the connection

On the ROS host, replace `DEVICE_ADDRESS` with the controller's reachable
address. Use a new output directory for every run:

```sh
sh "$example/tests/smoke-ros.sh" \
  "$work/ros-install/lib/ember_zenoh_bridge" \
  tcp/DEVICE_ADDRESS:17447 "$work/results"
```

The check starts its own bridge, discovers ROS topics, checks telemetry,
sends a valid command and verifies both its acknowledgement and state change.
It rejects duplicate, wrong-boot, expired and out-of-range commands. Then it
stops the bridge process with SIGSTOP for 22 seconds, exceeding the default
10-second Zenoh lease, and continues the same process. Telemetry must recover;
the saved old command must be rejected and a new command must succeed.
A process restart or controller restart does not satisfy this check.

Each probe has a bounded timeout. Failure leaves logs and a nonzero status;
only a completed run creates `SUCCESS`. The script cleans up its own bridge,
including a stopped process. It does not stop the separately launched controller.
This tests a stalled participant and expired connection, not cable removal.

For interactive ROS use, start the bridge instead of the smoke script:

```sh
"$work/ros-install/lib/ember_zenoh_bridge/robot-bridge" tcp/DEVICE_ADDRESS:17447
# In another configured terminal:
ros2 topic list -t
ros2 topic echo /ember/state ember_zenoh_bridge/msg/State
```

Topics are `/ember/state`, `/ember/command`, `/ember/ack`.
All use reliable, volatile KeepLast(10) ROS QoS. A command must copy a recent
state's `boot_id` and `device_ms`, choose `command_id` above the last accepted
ID, and provide `setpoint_millidegrees` in [0, 100000]. Use `robot-probe` for a
repeatable command test; manually copied timestamps normally expire too soon.
One command producer per controller is assumed.

## Wire and command contract

Zenoh keys are `ember/robotics/v1/demo/{state,command,ack}`. Each PUT is exactly
44 bytes, with big-endian integers and no native structure serialization:

| Offset | Field |
| --- | --- |
| 0 | `EBR1` magic, 4 bytes |
| 4 | kind: 1 state, 2 command, 3 acknowledgement; 1 byte |
| 5 | status, 1 byte; zero in state/command |
| 6 | two reserved zero bytes |
| 8 | boot identity, uint64 |
| 16 | device monotonic milliseconds, uint64 |
| 24 | command ID, uint64 |
| 32 | measurement, int32 |
| 36 | setpoint, int32 |
| 40 | applied-command count, uint32 |

The controller chooses a random identity at process start. Accept command timestamps only in the inclusive interval from 1000 ms before
the device's current clock value through that current value.
No wall-clock synchronization is required. IDs must increase within that boot.
Acknowledgement status: 0 applied, 1 wrong boot, 2 expired/future, 3 replay,
4 invalid setpoint, 5 exhausted counter. Rejection does not change state.
Malformed lengths, reserved bytes, kinds, statuses and key/kind mismatches
are rejected before controller state handling.

No retained command history, automatic command retries or exactly-once guarantee
is provided. A successful publish does not prove execution. Read the matching
acknowledgement and the applied count. After reconnect, obtain fresh state and
construct a new command. Counter/ID exhaustion requires an explicit restart.

Zenoh uses DROP congestion control. DROP avoids waiting for the transmit lock;
it does not make a socket send nonblocking. The Ports patch also sets the
existing socket timeout on TCP sends, including accepted connections.
Acknowledgements are sent outside the controller state mutex. This is not a
hard real-time guarantee. The filled-buffer tests exercise both connection paths.

## Validation and boundaries

On 2026-10-06, the installed EmberBSD/NetBSD 11 AArch64 controller passed the
protocol and both TCP timeout regressions. The complete command/telemetry and
reconnection scenario passed across two VMs, with Ubuntu 24.04.5 / Jazzy,
rclcpp 28.1.22 and rmw_fastrtps_cpp 8.4.4 on the ROS side. The applied count
advanced from 0 to 1 and then to 2; all rejected commands preserved it.
The same checks passed with the controller on Linux. An unreachable peer
returned failure without `SUCCESS`. The Ports profile records the native
package's 42 upstream tests, kernel identity, installation checks and hash.

The tests use simulated data in VMs. Physical controllers, sensor drivers,
actuators, a separate analysis board, RViz displays, long-duration operation,
automatic Zenoh discovery, other ROS distributions/RMWs, physical network
interruptions, authentication and TLS have not been validated here.

Own code: MIT, see [LICENSE](LICENSE). Zenoh-Pico keeps its upstream
EPL-2.0 OR Apache-2.0 licensing. This integration and the Ports patches were
prepared with AI assistance; they are not upstream submissions.
