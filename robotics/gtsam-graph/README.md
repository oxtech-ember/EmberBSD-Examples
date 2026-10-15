# GTSAM graph optimization

This standalone C++17 example uses shared GTSAM 4.3.0 and common Eigen 5.0.1.
It estimates scalar states and three planar poses from fixed synthetic
measurements. Each graph has an independent known solution. No sensor,
network service, GUI or Python interpreter is required.

The library recipe, cross compiler setup and package checks belong to
[EmberBSD Ports](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/gtsam).
This directory owns the application and its CSV acceptance tests.

## Build and run

Install the GTSAM and Eigen packages from the Ports profile. Use GCC 16 and
its matching C++ runtime. This example requires the exact dependency versions.

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr/pkg
cmake --build build
ctest --test-dir build --output-on-failure
./build/ember-gtsam-graph --case linear --output linear.csv
./build/ember-gtsam-graph --case pose --output pose.csv
```

For a cross build, pass the GCC 16 EmberBSD toolchain file and the staged
`GTSAM_DIR` and `Eigen3_DIR` paths. Run the resulting executable and CSV
checker in EmberBSD. The CLI CTest script runs executables directly. Use it
with a native build; it does not add a cross emulator.

The default case is `pose`. The default output is standard output. Use
`--output -` explicitly to send CSV to another command. `--help` shows the
supported cases. A rejected graph does not create or truncate an output file.
An output failure can leave a partial file; remove it before a retry.

## Independent expected results

The `linear` graph minimizes this unit-variance residual cost:

```text
0.5 * (x0^2 + (x1 - x0 - 2)^2 + (x1 - 3)^2)
```

The normal equations are `2*x0-x1=-2` and `-x0+2*x1=5`. Thus `x0=1/3`,
`x1=8/3`, and the minimum cost is `1/6`. Initial states `[-1,4]` have cost
`11/2`. The tolerance is `1e-10` for both optimized states and cost.

The `pose` graph has a prior at `(0,0,0)`, two motions and a loop constraint:

| Factor | Measurement `(x,y,theta)` |
|---|---|
| Prior for pose 0 | `(0,0,0)` |
| Motion 0 to 1 | `(2,0,0)` |
| Motion 1 to 2 | `(0,2,pi/2)` |
| Loop 0 to 2 | `(2,2,pi/2)` |

All factors use isotropic sigma `0.1`. The independent solution is
`[(0,0,0),(2,0,0),(2,2,pi/2)]`. Initial poses are
`[(0.1,-0.2,0.03),(2.2,0.1,-0.04),(1.9,2.2,pi/2+0.05)]`.
Levenberg-Marquardt has a fixed limit of 100 iterations. Every optimized
coordinate must match within `1e-7`, and the final cost must be below
`1e-12`. Every marginal covariance coefficient must be finite. The anchor
covariance must equal `0.01 I` within `1e-8`. The other constraints are
relative, so they add no independent absolute pose information.

The `underconstrained` case removes both absolute scalar measurements.
GTSAM must raise `IndeterminateSystemException`. The `missing-state` case
omits pose 2 from the initial values. GTSAM must reject the incomplete graph.
These failures are explicit examples of invalid graph inputs. A numerical
solver cannot repair a missing reference or a missing state.

## CSV and status contract

CSV has one row per state before and after optimization. Rows use the C
numeric locale and 17 significant digits. `theta` is in radians. The
`cost` column repeats the whole graph's cost for that stage. Pose covariance
uses the local tangent coordinates `(x,y,theta)` at the optimized state.
Only the six upper-triangle entries are exported. The application checks
all nine entries for finiteness and symmetry. Empty fields mean that a
coordinate or covariance is not part of the selected case. Scalar covariance
is not computed by this example.

| Status | Meaning |
|---|---|
| `0` | Optimization and independent numerical acceptance passed |
| `64` | Invalid command line |
| `65` | Expected graph input rejection |
| `70` | Unexpected solver, numerical acceptance or internal failure |
| `74` | Output open, write, flush or close failure |

The application completes all numerical checks before opening the output.
It checks file and standard-output write failures, including a closed pipe.
It does not promise atomic replacement of an existing output file.

## Tests and validation limits

`check-gtsam-csv` reads exported results without linking GTSAM or Eigen.
It compares states and residuals with literal analytic expectations. It
checks pose covariance finiteness, positive definiteness and the anchor's
known covariance. Run it directly in EmberBSD:

```sh
./build/check-gtsam-csv linear linear.csv
./build/check-gtsam-csv pose pose.csv
```

The CTest CLI test covers both graphs, standard output, invalid arguments,
missing states, underconstrained input, existing-output preservation and
output open/write failures. It also corrupts accepted CSV values to verify
that the independent checker rejects wrong finite states, NaN, both infinities
and each nonfinite exported covariance coefficient. `/dev/full` checks run
only where that device exists.

Current target execution and memory-analysis results are recorded in the
[Ports validation](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/gtsam).
These synthetic graphs do not validate live IMU/GNSS fusion, visual SLAM,
real-time deadlines, a physical robot or sustained operation.

## License and origin

This example is MIT licensed. It was developed with AI assistance for the
EmberBSD Examples project. The graphs follow the independent mathematical
contracts in the Ports GTSAM profile. GTSAM remains a separate BSD-licensed
upstream dependency. Its contributors own the optimization algorithms.
