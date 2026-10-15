// SPDX-License-Identifier: MIT
#include <onnxruntime_cxx_api.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#include "app-common.h"

int main(int argc, char** argv)
{
    configuration config{};
    const int parsed = parse_configuration(argc, argv, &config);
    if (parsed != 0) return parsed == -1 ? 0 : parsed;
    try {
        if (std::string(OrtGetApiBase()->GetVersionString()) != "1.31.0")
            throw std::runtime_error("This example requires ONNX Runtime 1.31.0.");
        const auto providers = Ort::GetAvailableProviders();
        if (providers != std::vector<std::string>{"CPUExecutionProvider"})
            throw std::runtime_error("This example requires the CPU-only Runtime profile.");
        std::cerr << "ONNX Runtime " << OrtGetApiBase()->GetVersionString()
                  << "; provider " << providers[0] << "; C++ API\n";
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "ember-onnx-cxx");
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(1).SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        Ort::Session session(env, config.model, options);
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::size_t columns = config.wrong_shape ? 4 : 3;
        std::vector<float> data(config.batch * columns);
        std::vector<float> results(config.repeats * config.batch * 2);
        const std::array<int64_t, 2> shape{static_cast<int64_t>(config.batch),
                                           static_cast<int64_t>(columns)};
        auto input = Ort::Value::CreateTensor<float>(memory, data.data(), data.size(),
                                                     shape.data(), shape.size());
        const char* inputs[] = {config.missing_input ? "absent_input" : "x"};
        const char* outputs[] = {config.missing_output ? "absent_output" : "y"};
        for (std::size_t run = 0; run < config.repeats; ++run) {
            for (std::size_t row = 0; row < config.batch; ++row)
                make_input(row, run, data.data() + row * columns);
            auto tensors = session.Run(Ort::RunOptions{nullptr}, inputs, &input, 1, outputs, 1);
            if (tensors.size() != 1 || !tensors[0].IsTensor())
                throw std::runtime_error("Unexpected output tensor count or type.");
            const auto info = tensors[0].GetTensorTypeAndShapeInfo();
            if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                info.GetShape() != std::vector<int64_t>{static_cast<int64_t>(config.batch), 2})
                throw std::runtime_error("Unexpected output tensor shape or element type.");
            const float* values = tensors[0].GetTensorData<float>();
            for (std::size_t i = 0; i < config.batch * 2; ++i) {
                if (!std::isfinite(values[i]))
                    throw std::runtime_error("Nonfinite inference result.");
                results[run * config.batch * 2 + i] = values[i];
            }
        }
        return write_csv(&config, results.data());
    } catch (const std::bad_alloc&) {
        std::cerr << "Cannot allocate input or result buffer.\n";
        return 70;
    } catch (const Ort::Exception& error) {
        std::cerr << "ONNX Runtime [" << error.GetOrtErrorCode() << "]: " << error.what() << '\n';
        return 65;
    } catch (const std::exception& error) {
        std::cerr << "ONNX Runtime: " << error.what() << '\n';
        return 65;
    }
}
