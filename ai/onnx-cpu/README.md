# ONNX Runtime CPU inference through installed C and C++ APIs

Run the same small neural network with the installed ONNX Runtime C API and
its C++ RAII API. Each consumer opens an ONNX model and executes CPU inference.
Neither consumer includes Runtime sources or calls internal interfaces.

## Requirements

- ONNX Runtime **1.31.0**, with shared libraries and the standard CPU provider.
- A C11 compiler, a C++17 compiler, CMake 3.22 or later, and Ruby on the build host.
- The installed `onnxruntimeConfig.cmake`, public headers, and target libraries.

[EmberBSD Ports](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/pkgsrc/local-ai/onnxruntime)
owns the Runtime recipe and platform patches. Use its GCC16 cross-build
instructions to create an EmberBSD prefix and toolchain file. The example does
not replace the compiler, OS headers, or sysroot. Ruby runs only on the build
host. The installed programs do not need Ruby, Python, protobuf, or a model
download.

## Build and run

Run these commands in `ai/onnx-cpu` with an installed native Runtime:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/usr/pkg
cmake --build build -j2
ctest --test-dir build --output-on-failure
build/onnx-cpu-c build/models/tiny-mlp.onnx 3 100 c.csv
build/onnx-cpu-cxx build/models/tiny-mlp.onnx 3 100 cxx.csv
build/check-onnx-csv 3 100 c.csv
build/check-onnx-csv 3 100 cxx.csv
```

For a cross-build, add `-DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake` to
configuration. Also set
`-Donnxruntime_DIR=/path/to/staged/usr/pkg/lib/cmake/onnxruntime`.
This selects the installed target package when its staging prefix is outside
the sysroot. Set `CMAKE_INSTALL_PREFIX=/usr/pkg` and stage installation with
`DESTDIR=/path/to/application-stage cmake --install build`.
Run the programs on EmberBSD. The `models` directory is generated on the build
host. Copy `tiny-mlp.onnx` with the executables, or use the staged installation.
The installed model path is `/usr/pkg/share/examples/onnx-cpu/tiny-mlp.onnx`.
The installation also preserves the original MIT license under
`/usr/pkg/share/licenses/onnx-cpu`.

The command format is the same for both APIs:

```text
onnx-cpu-c MODEL BATCH REPEATS OUTPUT.csv [ERROR_SWITCH]
onnx-cpu-cxx MODEL BATCH REPEATS OUTPUT.csv [ERROR_SWITCH]
```

`BATCH` accepts 1 to 32 samples. `REPEATS` accepts 1 to 1000 runs. Inputs change
between runs. Each CSV row contains the run, sample index, three input values,
and two inference results. All inference must succeed before the output file
opens. A model or inference error preserves an existing output file.

Exit codes are 0 for success, 64 for invalid arguments, 65 for model or Runtime
errors, 70 for memory allocation failure, and 74 for output errors.

## Model and independent numerical check

`generate-model.rb` writes a deterministic ONNX IR8, opset13 model. It encodes
the public [ONNX 1.22.0 protobuf format](https://github.com/onnx/onnx/blob/v1.22.0/onnx/onnx.proto3)
with a small Ruby encoder. It creates no learned model and uses no exporter.
The generated binary remains in the build directory. Its length is 421 bytes.
Its SHA256 is `b5df932d38244f965e037edc2df55957ec1bb342c07a800898a974f4ddc34188`.

The graph is `Gemm -> Relu -> Gemm`. It has three input columns, four hidden
units, two output columns, and a symbolic batch dimension. All coefficients
are exact binary fractions:

```text
W1 = [[ 1,   -2,  0.5,  0  ],
      [ 0.5,  1, -1,    2  ],
      [-1,    0,  2,   -0.5]]
b1 = [0.25, -0.5, 1, 0]
W2 = [[ 1,   -1   ],
      [ 0.5,  2   ],
      [-2,    0.25],
      [ 1.5, -0.5 ]]
b2 = [0.75, -1.25]
y = relu(x W1 + b1) W2 + b2
```

`check-onnx-csv` evaluates separate scalar equations with double precision.
It does not parse the ONNX model or share application code. It checks every
input, output, row index, and row count. The tolerance is `1e-5` per output.
The CLI tests also corrupt numeric results and verify checker rejection.

## Error contracts and validation limits

CTest runs both APIs with batches 1, 3, and 32. It compares complete CSV output.
It checks malformed models, missing model files, wrong tensor dimensions,
unknown input and output names, argument bounds, and failed output writes.
It checks the actual `OrtErrorCode`, including `ORT_INVALID_PROTOBUF`,
`ORT_NO_SUCHFILE`, and `ORT_INVALID_ARGUMENT`.
The switches `--wrong-shape`, `--missing-input`, and `--missing-output` send
real invalid requests to the Runtime. They are explicit acceptance inputs.

Both consumers select one intra-op and one inter-op thread. They require
exactly `CPUExecutionProvider`. The C consumer releases each API object.
The C++ consumer uses the installed RAII wrappers.

`onnx-thread-lifecycle` separately creates and destroys eight sessions with
two intra-op threads: the caller and one Runtime worker.
It checks 64 runs and 832 samples against scalar
answers. It changes batches 1, 3, 16, and 32 within each live session.
It uses the Runtime's own worker creation and teardown. The public
1.31.0 API has no native worker stack-size setting. The test uses its default
stack size and the documented `session.intra_op.allow_spinning` setting.

The example proves deterministic CPU inference through the installed APIs.
It does not measure throughput, model accuracy, GPU/NPU execution, training,
Python bindings, or physical-board behavior. Native memory analysis and
EmberBSD AArch64 VM acceptance are separate checks of the same sources.

Native validation used GCC16.2 on Linux x86_64 with ONNX Runtime 1.31.0.
Both CTest contracts passed. All 58 strict Memcheck processes reported zero
memory errors, lost bytes, reachable bytes, and suppressions. They covered both
consumers, the independent checker, real error paths, and session lifecycles.

The same C/C++ sources passed in AArch64 EmberBSD under QEMU with GCC16.2.
The installed Runtime package was removed and reinstalled. Both APIs passed
all three batch sizes, independent CSV checks, exact Runtime error codes,
and the worker-session lifecycle before and after reinstallation.
The Ports [validation record](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/onnxruntime/VALIDATION.md)
also covers Silero compatibility, 76 selected upstream tests, source analysis
limits, and actual-source platform regressions. This is CPU VM acceptance.
