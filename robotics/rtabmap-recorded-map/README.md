# Map recorded RGB-D data with RTAB-Map

This C++17 example builds a colored 3-D map from 12 saved TUM RGB-D frames
and their recorded reference poses. Shared RTAB-Map 0.23.8 extracts image
features, stores a graph and images in SQLite, closes the map and reopens
it through a new mapping object. It exports map geometry from the reopened
data. No camera, GUI, ROS, network service or Python interpreter is required.

This is recorded-pose mapping. Recorded camera poses are supplied to
RTAB-Map as external odometry. The example does not estimate visual odometry
or claim an independent SLAM trajectory score.

The GCC 16 toolchain, shared dependencies, cross package and target acceptance
belong to [EmberBSD Ports](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/rtabmap).
This directory owns the application, fixture selection and independent map
checker. Measured target and memory results are recorded in Ports.

## Prepare the recorded data

[dataset.tsv](dataset.tsv) pins the official TUM `freiburg1_desk` archive.
Its size is 344,011,403 bytes. The complete original archive stays outside
Git. Ruby, tar and SHA256 support are needed to prepare the fixture.

```sh
curl -fL https://webshare.cvg.cit.tum.de/g/rgbd/dataset/freiburg1/rgbd_dataset_freiburg1_desk.tgz \
  -o /absolute/tum-rgbd-fr1-desk.tgz
ruby robotics/rtabmap-recorded-map/extract-fixture.rb \
  /absolute/tum-rgbd-fr1-desk.tgz /absolute/new-recorded-fixture
```

The extractor verifies the complete archive before writing output. It pairs
each depth image with the nearest unused RGB image within 20 ms. It then
selects the nearest recorded ground-truth pose within 20 ms of RGB. Ties
select the earlier timestamp. It takes the first 12 eligible pairs with
stride 10. Original PNG files remain unchanged. Selection covers RGB times
`1305031453.359684` through `1305031457.127632`, approximately 3.77 seconds.

The fixture contains 24 PNG files, original timestamp lists and ground truth,
`frames.tsv`, per-file SHA256 values and `fixture.json`. Verify the extracted
files from the fixture directory with `sha256sum -c inputs.sha256` before use.
The fixture requires approximately 7.2 MiB. No images or databases enter Git.

TUM releases these data under [CC BY 4.0](https://cvg.cit.tum.de/data/datasets/rgbd-dataset#license).
Attribute J. Sturm, N. Engelhard, F. Endres, W. Burgard and D. Cremers,
*A Benchmark for the Evaluation of RGB-D SLAM Systems*, IROS 2012.

## Build and run

Install the current Ports profile with GCC 16, RTAB-Map 0.23.8, PCL 1.15.1,
GTSAM 4.3.0, Eigen 5.0.1, OpenCV 5.0.0, SQLite 3.54.0 and libpng 1.6.59.
Use the same shared dependency installation for the library and application.
The headless OpenCV profile must support PNG encoding and decoding.

```sh
cmake -S robotics/rtabmap-recorded-map -B /absolute/example-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr/pkg \
  -DRECORDED_FIXTURE=/absolute/new-recorded-fixture
cmake --build /absolute/example-build
ctest --test-dir /absolute/example-build --output-on-failure
/absolute/example-build/ember-rtabmap-recorded-map \
  --dataset /absolute/new-recorded-fixture --output /absolute/new-map-results
/absolute/example-build/check-recorded-map \
  --dataset /absolute/new-recorded-fixture --results /absolute/new-map-results
```

For a cross build, pass the Ports GCC 16 EmberBSD toolchain file and staged
dependency paths. Run the resulting programs in EmberBSD. CTest starts
programs directly; use it with a native build. It adds no cross emulator.

Append `--validate-only` to decode and validate all selected images without
starting mapping or creating output. Mapping requires a new output path.
Invalid arguments return `64`; invalid recorded input returns `65` before
output creation. Unexpected mapping or numerical failure returns `70`.
Output creation, write, flush or close failure returns `74`. A failed map
can retain partial diagnostic output; use a new path for a retry.

## Map and numerical contract

The source images are 640×480 RGB8 and uint16 depth. Zero depth is invalid.
TUM depth uses 5000 units per metre. The application converts it to float
metres before creating `SensorData`. RTAB-Map stores this float depth
losslessly through its PNG representation. Reopened RGB and depth must match
the decoded inputs exactly.

The declared pinhole model uses `fx=fy=525`, `cx=319.5`, `cy=239.5`, without
undistortion. These are the [TUM recommended ROS defaults](https://cvg.cit.tum.de/data/datasets/rgbd-dataset/file_formats).
This checks the declared model; it does not measure physical calibration
accuracy. The camera local transform is explicitly identity. Coordinates
therefore remain optical: X right, Y down and Z forward. The map frame is
the first recorded camera frame. Later poses are `T_first^-1 * T_current`.

RTAB-Map extracts up to 200 ORB features per image. No precomputed features
or descriptors are supplied. Each reopened node must retain visual words.
All 12 frames remain in short-term memory for this bounded run. The graph
must retain all nodes and at least 11 constraints. This example does not
require or report a visual loop closure. GTSAM is the graph optimizer.
External odometry covariance is `1e-6 I`, in the library's pose coordinates.

The output contains:

- `map.db`: the actual RTAB-Map SQLite map.
- `poses.csv`: node IDs, image timestamps, word counts and 3×4 map poses.
- `cloud.csv`: source node, pixel coordinates, map XYZ and original RGB.
- `map.ply`: the same colored geometry in ASCII PLY format.
- `metrics.txt`: node, constraint, word and point counts.

Geometry uses `util3d::cloudRGBFromSensorData` on the reopened map, with
decimation 16 and inclusive depth limits 0.1 through 4 metres. Each valid
grid pixel contributes one map point. CSV uses the C numeric locale and
17 significant digits. The application checks finite poses and points.

`check-recorded-map` links only libpng, SQLite and the C++ runtime. It uses
no RTAB-Map, PCL, OpenCV or Eigen APIs. It decodes the original PNG files,
computes pinhole XYZ and quaternion transformations directly, and checks
every exported point and color. It also checks every pose, exact row order,
complete coverage, PLY agreement, SQLite integrity and the 12-node count.

Bounds are declared before measured runs: translation error at most 0.03 m,
rotation error at most 0.02 radians, and each point coordinate within `2e-5`
of the declared projection and exported pose. A point must also be within
0.12 m of the independent recorded-pose projection. These tolerances allow
float arithmetic and graph adjustment. They are not a sensor accuracy score.
The checker returns `0` for acceptance, `1` for invalid input or results,
and `64` for invalid arguments.

## Tests and limits

The CLI CTest creates a fresh result directory on each run. It checks the
complete mapping workflow, independent geometry acceptance, validation-only
mode, invalid arguments, missing/corrupt images and existing-output safety.
Counterfactual checks replace pose and cloud numbers with wrong finite
values, NaN and both infinities. Truncated or extra map exports must fail.

GCC16 cross-built executables pass the installed EmberBSD AArch64 QEMU
workflow with fourteen real packages, including removal/reinstallation and
byte-identical repeated exports. Both target maps contain twelve nodes, eleven
constraints and 10,859 colored points. Linux GCC16 controls pass the full CLI
CTest and 74 Memcheck processes with zero errors or lost blocks.
Detailed source/runtime boundaries belong to the
[Ports validation](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/rtabmap/VALIDATION.md).
A short recorded RGB-D map does not establish camera capture, visual
odometry, loop closure accuracy, full-sequence SLAM, a physical board,
large maps, real-time deadlines or sustained operation.

## License and origin

This application and its tools are MIT licensed and developed with AI
assistance for EmberBSD Examples. The independent PNG reader is reused from
this repository's MIT-licensed ORB-SLAM3 RGB-D example. TUM owns the recorded
data under CC BY 4.0. RTAB-Map and all libraries retain their upstream
authorship and separate licenses; bundled utilite is LGPL-3.0-or-later.
