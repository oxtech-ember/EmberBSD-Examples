# ncnn CPU inference through installed C and C++ APIs

This example loads a small convolutional neural network with installed ncnn
**1.0.20260526**. It runs the same FP32 workload through the C and C++ APIs.
An independent scalar implementation checks every output. The fixture uses
original synthetic weights. It needs no downloaded model or Python package.

The runtime belongs to [EmberBSD Ports](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/ncnn).
This example does not build ncnn or include runtime source directories.

## Requirements

- GCC 16 or a compatible C11 and C++17 compiler.
- Installed ncnn 1.0.20260526 headers, shared library and CMake configuration.
  Its CMake package version is `20260526`; its runtime and pkg-config version is `1.0.20260526`.
- The CPU profile with `NCNN_THREADS=ON`, `NCNN_OPENMP=OFF` and `NCNN_VULKAN=OFF`.
- Ruby for the small model generator. Ruby is not required to run the applications.
- CMake 3.22 or newer. Ninja is optional.

The fixture uses little-endian float32 weights. The tested architectures are
AArch64 and x86-64. Each inference uses one thread. The separate concurrency
control uses two application threads with independent `ncnn::Net` instances.
`NCNN_THREADS=ON` does not enable operator parallelism with OpenMP disabled.

## Build and run

Point CMake at the installed SDK. Do not point it at an ncnn build tree.

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=/usr/pkg -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
build/ncnn-cpu-c build/models/tiny-cnn.param build/models/tiny-cnn.bin c.csv 41 7 9
build/ncnn-cpu-cxx build/models/tiny-cnn.param build/models/tiny-cnn.bin cxx.csv 41 7 9
build/check-ncnn-csv c.csv 41 7 9
build/check-ncnn-csv cxx.csv 41 7 9
cmp c.csv cxx.csv
```

For a cross build, supply the EmberBSD toolchain file and the staged installed
SDK. Set `BUILD_TESTING=OFF` if the cross toolchain cannot run target executables.
Run the tests in EmberBSD after deployment. Install with `cmake --install build`.
The applications use an install RPATH of `$ORIGIN/../lib`.

The command line is:

```text
ncnn-cpu-{c,cxx} MODEL.param MODEL.bin OUTPUT.csv SEED COUNT REPEAT [INPUT_BLOB OUTPUT_BLOB]
check-ncnn-csv OUTPUT.csv SEED COUNT REPEAT
```

`SEED` is 0 through 100000. `COUNT` is 1 through 64. `REPEAT` is 1 through 1000.
The default blob names are `input` and `prob`. Optional names exercise the API
error paths. Use `--help` to print the application syntax.

The applications check the exact runtime version. They request CPU FP32,
nonpacked tensors and one inference thread. They reject unexpected output
shapes and nonfinite results. Each row contains the seed, sample index,
repeat index, three raw logits and three probabilities. Rows use sample order,
then repeat order. Repeated runs receive identical input for each sample.
The applications replace the output atomically after all inference calls pass.
Failures preserve an existing output file. Output must not alias a model input.

## Model and independent reference

`generate-model.rb` writes a six-layer graph:

```text
Input 5x5x2 -> Convolution 3x3, valid, 3 channels -> ReLU
             -> global average pool -> InnerProduct 3 -> Softmax
```

The plain text graph is `tiny-cnn.param`. `tiny-cnn.bin` contains 284 bytes:
54 convolution weights, three convolution biases, nine dense weights and three
dense biases. Each weight block starts with the ncnn FP32 tag `0x00000000`.
The graph and weights use the MIT license in [LICENSE](LICENSE).
They are a numerical fixture, without a training dataset or an accuracy claim.

The input uses channel-major order. For sample `s`, channel `c` and position
`j=y*5+x`, the input is `(((seed+s)*17+(c*25+j)*13)%41-20)/16`.
For flattened convolution index `i=((out*2+in)*3+ky)*3+kx`, the weight is
`((i*7+3)%17-8)/32`. The convolution biases are `[-1/8,1/16,3/16]`.
For dense index `i=out*3+in`, the weight is `((i*5+1)%11-5)/16`.
The dense biases are `[1/16,-1/8,1/4]`.

[tests/check-results.cpp](tests/check-results.cpp) implements scalar convolution,
ReLU, averaging, dense multiplication and stable softmax with double precision.
It does not include ncnn headers, link ncnn or read the generated weights.
It independently implements the documented mathematical contract. It checks
row count and order, all six outputs, finite values, probability range and sum.
The absolute tolerance is `1e-5`, scaled by the expected magnitude above one.

## Verification and limits

The CTest suite checks 192 rows per API, with seeds 0, 41 and 100000. It requires
identical C and C++ CSV output. It checks 38 failed CLI calls and five deliberately
corrupt CSV files. Failure cases cover absent files, a malformed graph,
truncated and nonfinite weights, unknown blob names, numeric bounds, output
aliasing and an unwritable output path.

The wrapper accepts only the exact six-layer graph and bounded float32 weights.
Its malformed-file checks are application validation. They do not prove that
ncnn safely parses arbitrary untrusted models. `ncnn-api-errors` separately calls
the installed C API with a bad graph magic, absent graph, absent weights and
truncated weights. It requires a nonzero return for each case.

`ncnn-thread-lifecycle` compares 128 concurrent inference runs against sequential
results. Two application threads create and destroy 16 independent networks.
This verifies the scenario's object lifetimes and numerical consistency. It is
not a thread sanitizer or a test of parallel operators.

The [port validation record](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/ncnn/VALIDATION.md) identifies the compiler, runtime build profile,
EmberBSD QEMU results, native Memcheck checks and source-analysis limits.
This example does not measure performance or validate a physical board.
GPU, Vulkan, quantized inference and model conversion are outside this workload.
