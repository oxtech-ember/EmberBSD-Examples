// SPDX-License-Identifier: MIT
// Exercise the installed Runtime's own two-thread pool and session teardown.
#include <onnxruntime_cxx_api.h>
#include <onnxruntime_session_options_config_keys.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::array<float, 2> scalar(const float* x)
{
    const auto relu = [](float value) { return value > 0 ? value : 0; };
    const float a = relu(x[0] + 0.5f * x[1] - x[2] + 0.25f);
    const float b = relu(-2 * x[0] + x[1] - 0.5f);
    const float c = relu(0.5f * x[0] - x[1] + 2 * x[2] + 1);
    const float d = relu(2 * x[1] - 0.5f * x[2]);
    return {a + 0.5f * b - 2 * c + 1.5f * d + 0.75f,
            -a + 2 * b + 0.25f * c - 0.5f * d - 1.25f};
}
} // namespace

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "expected MODEL.onnx");
        require(std::string(OrtGetApiBase()->GetVersionString()) == "1.31.0",
                "expected ONNX Runtime 1.31.0");
        require(Ort::GetAvailableProviders() == std::vector<std::string>{"CPUExecutionProvider"},
                "expected only CPUExecutionProvider");
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "ember-onnx-threads");
        const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        constexpr std::array<std::size_t, 8> batches{1, 3, 16, 32, 32, 16, 3, 1};
        const char* inputs[] = {"x"};
        const char* outputs[] = {"y"};
        for (std::size_t cycle = 0; cycle < 8; ++cycle) {
            Ort::SessionOptions options;
            options.SetIntraOpNumThreads(2).SetInterOpNumThreads(1);
            options.AddConfigEntry(kOrtSessionOptionsConfigAllowIntraOpSpinning, "0");
            options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            Ort::Session session(env, argv[1], options);
            for (std::size_t run = 0; run < batches.size(); ++run) {
                // Change the symbolic batch within the same live session.
                const auto batch = batches[run];
                const std::array<int64_t, 2> shape{static_cast<int64_t>(batch), 3};
                std::vector<float> data(batch * 3);
                auto input = Ort::Value::CreateTensor<float>(memory, data.data(), data.size(),
                                                             shape.data(), shape.size());
                for (std::size_t row = 0; row < batch; ++row) {
                    data[row * 3] = static_cast<float>(row) - 2.0f + 0.25f * static_cast<float>(cycle);
                    data[row * 3 + 1] = static_cast<float>((row + run) % 7) - 3.0f;
                    data[row * 3 + 2] = ((row + cycle + run) % 2 == 0) ? -1.5f : 0.5f;
                }
                auto results = session.Run(Ort::RunOptions{nullptr}, inputs, &input, 1, outputs, 1);
                require(results.size() == 1 && results[0].IsTensor(), "missing result tensor");
                const auto info = results[0].GetTensorTypeAndShapeInfo();
                require(info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
                        info.GetShape() == std::vector<int64_t>{static_cast<int64_t>(batch), 2},
                        "incorrect output shape");
                const auto* values = results[0].GetTensorData<float>();
                for (std::size_t row = 0; row < batch; ++row) {
                    const auto expected = scalar(data.data() + row * 3);
                    for (std::size_t column = 0; column < 2; ++column)
                        require(std::isfinite(values[row * 2 + column]) &&
                                std::abs(values[row * 2 + column] - expected[column]) <= 1e-5f,
                                "threaded inference differs from scalar answer");
                }
            }
            // Destroy the session and its own Runtime worker before restart.
        }
        std::cout << "PASS installed CPU API: 8 two-thread session lifecycles, 64 dynamic-batch runs, 832 scalar-checked samples\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
