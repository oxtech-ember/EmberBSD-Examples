# Plan a path around a rectangle with OMPL

This C++17 example uses OMPL 2.0.2 RRTConnect to route a point around an
obstacle. It saves the complete polyline as CSV. A separate continuous
segment checker rejects a path that touches or crosses the closed obstacle.
It does not reuse OMPL's sampled motion checker.

The example belongs to EmberBSD Examples. The shared library, cross-build
helper, dependency recipes and package checks belong to
[EmberBSD Ports](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/ompl).
The application and fixtures were developed with AI assistance.

## Dependencies and build

Use GCC 16.2.0, OMPL 2.0.2, Eigen 5.0.1 and Boost 1.92.0 from Ports.
All C++ libraries must use the same compiler and runtime ABI.
The example requires Boost serialization and program_options CMake packages.
It links the installed shared `ompl::ompl` target. It does not build a private
copy of OMPL. No Python, graphics or robot hardware is required.

For a native EmberBSD build, install the common dependencies first.
Run these commands from the Examples checkout:

```sh
work=/absolute/new/ompl-example-build
cmake -S robotics/ompl-path -B "$work" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/pkg/gcc16/bin/g++ \
  -DCMAKE_PREFIX_PATH=/usr/pkg \
  -DCMAKE_INSTALL_PREFIX=/usr/pkg \
  '-DCMAKE_INSTALL_RPATH=/usr/pkg/lib;/usr/pkg/gcc16/lib' \
  -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
cmake --build "$work" --parallel 2
ctest --test-dir "$work" --output-on-failure
"$work/ember-ompl-path" --output path.csv
```

For a Linux-to-EmberBSD AArch64 cross-build, first run the Ports
`probes/ompl/cross-build.sh` helper. That helper takes the GCC16 prefix,
EmberBSD sysroot, verified OMPL source archive and a new output directory.
Use its generated toolchain and staged installation:

```sh
OMPL_OUTPUT=/absolute/ompl-cross-output
SYSROOT=/absolute/emberbsd-sysroot
work=/absolute/new/ompl-example-cross
cmake -S robotics/ompl-path -B "$work" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$OMPL_OUTPUT/toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$OMPL_OUTPUT/stage/usr/pkg;$SYSROOT/usr/pkg" \
  -Dompl_DIR="$OMPL_OUTPUT/stage/usr/pkg/share/ompl/cmake" \
  -DCMAKE_INSTALL_PREFIX=/usr/pkg \
  '-DCMAKE_INSTALL_RPATH=/usr/pkg/lib;/usr/pkg/gcc16/lib' \
  -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
cmake --build "$work" --parallel 2
```

Run the cross-built executable and geometry test in EmberBSD AArch64.
Linux cannot run these ELF files directly. CTest also needs target CMake;
the individual CLI cases below can run without CMake in a minimal guest.
The staged library is a build input. Install its package and the shared
GCC16/Boost dependencies on the target before running the application.

## Geometry and output

The world is the closed unit square `[0,1] × [0,1]`. The start is `(0.1,0.5)`
and the goal is `(0.9,0.5)`. The normal obstacle is the closed rectangle
`[0.4,0.6] × [0.15,0.85]`. The point must go above or below it.
Coordinates have no physical unit. The point has zero radius.

```sh
ember-ompl-path --output path.csv
ember-ompl-path --output - > path.csv
```

CSV has a header `x,y` and one pair of finite coordinates per row.
The first row is the start. The last row is the exact goal, within `1e-8`.
The default output is stdout. OMPL messages and the path summary go to stderr.
`--output FILE` replaces an existing file after a valid path is found.
An open, write, flush or close failure produces a nonzero exit status.
A failed write can leave a partial file.

The seed is `104729`, the planner range is `0.07`, and the normal iteration
budget is `20000`. OMPL samples motion validity at resolution `0.0005`
of the state-space extent. The final oracle clips every complete segment
against the rectangle slabs. Tangency counts as collision. It also checks
the domain bounds, endpoints and independently calculated path length.
The oracle expands the rectangle by `32 × double epsilon` in each direction
to reject contact conservatively within floating-point roundoff.
An oracle failure prevents CSV output.

A fixed seed repeats the random sequence within a fixed build. It does not
promise identical coordinates across library versions or architectures.
The returned path is an unsmoothed polyline. It is not a shortest-path result.

## Negative cases and checks

```sh
ember-ompl-path --case invalid-goal --output invalid.csv
ember-ompl-path --case blocked --output blocked.csv
ember-ompl-path --output /missing-directory/path.csv
ember-ompl-path --output /dev/full
```

`invalid-goal` puts the goal at `(0.5,0.5)` inside the normal obstacle.
It uses a 256-iteration budget and requires OMPL's invalid-goal status.
`blocked` uses the closed wall `[0.45,0.55] × [0,1]` and a 4000-iteration
budget. A wall that joins the opposite domain boundaries separates the
start and goal. The program rejects any exact solution in this case.
An approximate result is not exported as a successful path.
Neither negative planning case creates or modifies its output file.

Exit statuses are `0` for a validated exact path, `1` for an internal or
geometry failure, `2` for invalid arguments, `3` for no exact path, and `4`
for an output failure. `--help` also returns zero when its output succeeds.
Unknown, repeated and incomplete options are errors.
The `/dev/full` check applies on systems that provide that device.

CTest checks the continuous oracle with crossing, tangent, reversed,
degenerate and non-finite fixtures. It checks seeded file/stdout agreement,
the negative planning cases, CLI errors and output failures.
The CLI test has a 600-second timeout for emulated AArch64 execution.

## Validation limits

On 2026-10-15, the Linux x86_64 build used GCC 16.2.0, C++17 and the actual
shared OMPL 2.0.2 library with the dependency versions above. Both CTest tests
passed. Valgrind Memcheck 3.24.0 checked the normal path, both negative
planning cases, invalid arguments, missing output directory, `/dev/full`
and the geometry fixture. All seven cases returned their expected statuses.
Each had zero memory errors, zero bytes allocated at exit and no suppressions.
The native path had 24 states and length `1.496893253146`. The independent
Ports CSV checker also accepted every continuous edge.

EmberBSD AArch64 execution and full package checks are recorded with the
[Ports acceptance evidence](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/ompl).
The current example does not control a robot. It does not model body shape,
clearance, acceleration, sensing error, moving obstacles or actuator timing.
The model and its checks do not establish physical collision safety or
real-time performance. Robot hardware needs separate validation.

## License

The original example is [MIT licensed](LICENSE). OMPL is a separate
BSD-3-Clause dependency. Preserve its notices and the Eigen/Boost notices
when distributing the linked program. Ports records dependency provenance.
