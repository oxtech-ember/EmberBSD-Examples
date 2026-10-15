# PCL point-cloud filtering and registration

This C++17 example uses shared PCL 1.15.1 and common Eigen 5.0.1. It filters
fixed points and registers two synthetic 3-D clouds with a known rigid
motion. No sensor, network service, GUI or Python interpreter is required.

The library recipe, GCC 16 cross setup and package checks belong to
[EmberBSD Ports](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/pcl).
This directory owns the application and independent CSV acceptance checker.

## Build and run

Install the PCL, FLANN, Boost and Eigen packages from the Ports profile.
Use GCC 16 and its matching C++ runtime. This example requires the exact
PCL and Eigen versions.

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr/pkg \
  -DCMAKE_CXX_COMPILER=/usr/pkg/gcc16/bin/g++
cmake --build build
ctest --test-dir build --output-on-failure
./build/ember-pcl-cloud --case filter --output filter.csv
./build/ember-pcl-cloud --case registration --output registration.csv
./build/check-pcl-csv --case filter --input filter.csv
./build/check-pcl-csv --case registration --input registration.csv
```

For a cross build, pass the GCC 16 EmberBSD toolchain file, staged `PCL_DIR`
and common `Eigen3_DIR`. Include the staging prefix and target sysroot in
`CMAKE_FIND_ROOT_PATH` when the toolchain uses root-only library searches.
Run the executables in EmberBSD. The CLI CTest runs
executables directly. Use it with a native build; it adds no cross emulator.

The default case is `registration`. The default output is standard output.
Use `--output -` explicitly to send CSV to another command. `--help` shows
the supported cases. A rejected alignment cannot create or truncate an
output file. An output failure can leave a partial file; remove it before
a retry. The program does not promise atomic file replacement.

## Independent expected results

The `filter` case supplies six points. Four finite points have `z` between
zero and two metres. A distant point has `z=20`; one point contains NaN.
PCL PassThrough removes those two points. VoxelGrid uses one-metre leaves
and produces these two centroids:

```text
(0.2, 0.2, 0.2)
(1.2, 0.2, 0.2)
```

The application and independent checker require each coordinate within
`1e-6`. The `empty-filter` case checks a valid empty cloud. It exports only
the CSV header and exits successfully.

The `registration` case uses 30 asymmetric source points. For integer
`i=0..29`, the coordinates are:

```text
x = (i % 5)*0.8 + (i % 3)*0.03
y = ((i / 5) % 3)*0.9 + (i % 2)*0.07
z = (i / 15)*1.1 + (i % 7)*0.04
```

Integer division applies before multiplication. The target is a direct
evaluation of `Rz(0.012)*p + (0.04,-0.03,0.02)`. Angles are in radians;
coordinates are in metres. The oracle does not call PCL's transformation
operation. ICP has a limit of 80 iterations, a `0.2` metre correspondence
limit, and transformation and fitness stopping tolerances of `1e-12`.

ICP must converge. Each matrix coefficient must match the known motion
within `1e-4`. Every aligned point must be within `1e-4` of its target.
The final mean squared correspondence distance must be finite and below
`1e-8`. All exported point and matrix coefficients must be finite.

The `no-correspondence` case shifts the target 100 metres along X. ICP must
report no convergence with the same correspondence limit. The application
rejects that alignment with status `65` and exports no result.

## CSV and status contract

CSV uses the C numeric locale and 17 significant digits. Its header is
`case,kind,index,a,b,c,d`. All fields are mandatory. Row order is fixed.

| Case and kind | Rows | Meaning of `a,b,c,d` |
|---|---:|---|
| `filter,centroid` | 2 | `x,y,z,0`; index 0 then 1 |
| `registration,source` | 30 | `x,y,z,0`; index 0 through 29 |
| `registration,target` | 30 | `x,y,z,0`; index 0 through 29 |
| `registration,aligned` | 30 | `x,y,z,0`; index 0 through 29 |
| `registration,matrix` | 4 | Four entries of one homogeneous matrix row |
| `registration,fitness` | 1 | `mean_squared_distance,0,0,0`; index 0 |

The homogeneous matrix transforms source coordinates into target
coordinates. Matrix row indices are zero through three. The registration
case exports the kinds in table order. `empty-filter` has no data rows.

| Status | Meaning |
|---|---|
| `0` | Filtering or registration passed numerical acceptance |
| `64` | Invalid command line |
| `65` | No accepted alignment |
| `70` | Unexpected solver, numerical acceptance or internal failure |
| `74` | Output open, write, flush or close failure |

`check-pcl-csv` uses no PCL or Eigen headers or libraries. It evaluates the
source lattice and known rigid motion directly. It checks row identities,
counts, all numerical fields and the closed-form centroid expectations.
It accepts `--input -` for standard input. Its statuses are `0` for accepted
CSV, `1` for invalid CSV, `64` for invalid arguments and `74` for input I/O
failure.

## Tests and validation limits

The CLI CTest covers all four cases, file and standard output, invalid
arguments, existing-output preservation and output open/write failures.
It corrupts each matrix coefficient and representative point and fitness
fields. The independent checker must reject wrong finite values, NaN and
both infinities. `/dev/full` checks run only where that device exists.

Current target execution and memory-analysis results belong to the
[Ports validation](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/pcl).
These small synthetic clouds do not validate a physical lidar or camera,
SLAM, calibration, large datasets, real-time deadlines or sustained use.

## License and origin

This example is MIT licensed. It was developed with AI assistance for
EmberBSD Examples. Its fixtures follow the independent mathematical
contracts in the Ports PCL profile. PCL, FLANN, Boost and Eigen retain their
upstream authorship and separate licenses. Their contributors own the
filtering, search, registration and linear algebra implementations.
