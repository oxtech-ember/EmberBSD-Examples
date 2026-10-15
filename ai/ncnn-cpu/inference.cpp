// SPDX-License-Identifier: MIT
#include "app-common.h"
#include <net.h>
#include <c_api.h>
#include <exception>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>

int main(int argc, char** argv)
{
    configuration c{};
    const int parsed = parse_configuration(argc, argv, &c);
    if (parsed != 0)
        return parsed == -1 ? 0 : parsed;
    if (validate_model(&c) != 0)
        return 65;
    if (ncnn_version_number() != 20260526 || std::strcmp(ncnn_version(), "1.0.20260526") != 0) {
        std::fprintf(stderr, "This example requires ncnn 1.0.20260526.\n");
        return 65;
    }
    std::fprintf(stderr, "ncnn %s; C++ API; CPU FP32; one inference thread\n", ncnn_version());
    try {
        ncnn::Net net;
        net.opt.num_threads = 1;
        net.opt.use_vulkan_compute = false;
        net.opt.use_fp16_packed = false;
        net.opt.use_fp16_storage = false;
        net.opt.use_fp16_arithmetic = false;
        net.opt.use_bf16_packed = false;
        net.opt.use_bf16_storage = false;
        net.opt.use_int8_inference = false;
        net.opt.use_int8_packed = false;
        net.opt.use_int8_storage = false;
        net.opt.use_int8_arithmetic = false;
        net.opt.use_packing_layout = false;
        if (net.load_param(c.graph) != 0 || net.load_model(c.weights) != 0) {
            std::fprintf(stderr, "ncnn cannot load the model.\n");
            return 65;
        }
        std::vector<float> values(static_cast<size_t>(c.count) * c.repeats * 6);
        size_t row = 0;
        for (unsigned sample = 0; sample < c.count; ++sample) {
            for (unsigned repeat = 0; repeat < c.repeats; ++repeat, ++row) {
                ncnn::Mat input(5, 5, 2);
                if (input.empty())
                    return 70;
                for (unsigned channel = 0; channel < 2; ++channel)
                    make_input(c.seed, sample, channel, input.channel(static_cast<int>(channel)));
                auto ex = net.create_extractor();
                ncnn::Mat logits, prob;
                if (ex.input(c.input_name, input) != 0 || ex.extract("logits", logits) != 0 ||
                    logits.dims != 1 || logits.w != 3 || logits.elemsize != sizeof(float) || logits.elempack != 1) {
                    std::fprintf(stderr, "ncnn logits inference failed or returned an unexpected tensor.\n");
                    return 65;
                }
                for (unsigned k = 0; k < 3; ++k)
                    values[row * 6 + k] = logits[k];
                if (ex.extract(c.output_name, prob) != 0 || prob.dims != 1 || prob.w != 3 ||
                    prob.elemsize != sizeof(float) || prob.elempack != 1) {
                    std::fprintf(stderr, "ncnn probability inference failed or returned an unexpected tensor.\n");
                    return 65;
                }
                for (unsigned k = 0; k < 3; ++k)
                    values[row * 6 + 3 + k] = prob[k];
                for (unsigned k = 0; k < 6; ++k)
                    if (!std::isfinite(values[row * 6 + k])) {
                        std::fprintf(stderr, "Nonfinite inference result.\n");
                        return 65;
                    }
            }
        }
        return write_results(&c, values.data());
    } catch (const std::exception& error) {
        std::fprintf(stderr, "ncnn example: %s\n", error.what());
        return 70;
    }
}
