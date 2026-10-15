// SPDX-License-Identifier: MIT
#include <net.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <exception>
#include <thread>

static void configure(ncnn::Net& net)
{
    net.opt.num_threads = 1;
    net.opt.use_vulkan_compute = false;
    net.opt.use_fp16_packed = false;
    net.opt.use_fp16_storage = false;
    net.opt.use_fp16_arithmetic = false;
    net.opt.use_bf16_storage = false;
    net.opt.use_bf16_packed = false;
    net.opt.use_int8_inference = false;
    net.opt.use_packing_layout = false;
}

static bool infer(ncnn::Net& net, unsigned variant, std::array<float, 6>& out)
{
    ncnn::Mat input(5, 5, 2);
    if (input.empty())
        return false;
    for (int channel = 0; channel < 2; ++channel) {
        float* data = input.channel(channel);
        for (int j = 0; j < 25; ++j)
            data[j] = static_cast<float>((static_cast<unsigned>(channel * 25 + j) + variant * 7) % 23) / 16.f - 0.5f;
    }
    auto ex = net.create_extractor();
    ncnn::Mat logits, prob;
    if (ex.input("input", input) != 0 || ex.extract("logits", logits) != 0 || logits.dims != 1 || logits.w != 3 || logits.elemsize != sizeof(float) || logits.elempack != 1)
        return false;
    for (size_t k = 0; k < 3; ++k)
        out[k] = logits[k];
    if (ex.extract("prob", prob) != 0 || prob.dims != 1 || prob.w != 3 || prob.elemsize != sizeof(float) || prob.elempack != 1)
        return false;
    for (size_t k = 0; k < 3; ++k)
        out[k + 3] = prob[k];
    return true;
}

int main(int argc, char** argv)
{
    if (argc != 3)
        return 64;
    try {
        std::array<std::array<float, 6>, 2> expected{};
        {
            ncnn::Net warm;
            configure(warm);
            if (warm.load_param(argv[1]) != 0 || warm.load_model(argv[2]) != 0)
                return 1;
            for (unsigned variant = 0; variant < 2; ++variant)
                if (!infer(warm, variant, expected[variant]))
                    return 1;
        }
        std::atomic<unsigned> checked{0};
        std::atomic<bool> failed{false};
        auto worker = [&](unsigned variant) {
            try {
                for (unsigned cycle = 0; cycle < 8; ++cycle) {
                    ncnn::Net net;
                    configure(net);
                    if (net.load_param(argv[1]) != 0 || net.load_model(argv[2]) != 0) {
                        failed = true;
                        return;
                    }
                    for (unsigned repeat = 0; repeat < 8; ++repeat) {
                        std::array<float, 6> actual{};
                        if (!infer(net, variant, actual)) {
                            failed = true;
                            return;
                        }
                        for (size_t k = 0; k < actual.size(); ++k)
                            if (!std::isfinite(actual[k]) || std::abs(actual[k] - expected[variant][k]) > 1e-6f) {
                                failed = true;
                                return;
                            }
                        ++checked;
                    }
                }
            } catch (const std::exception&) {
                failed = true;
            }
        };
        std::thread first(worker, 0);
        try {
            std::thread second(worker, 1);
            second.join();
        } catch (...) {
            first.join();
            throw;
        }
        first.join();
        if (failed || checked != 128)
            return 1;
        std::puts("PASS: two application threads, 16 independent Net lifecycles, 128 inference comparisons (768 scalar values)");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Thread lifecycle: %s\n", error.what());
        return 1;
    }
}
