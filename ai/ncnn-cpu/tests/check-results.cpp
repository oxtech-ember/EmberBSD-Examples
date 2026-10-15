// SPDX-License-Identifier: MIT
// This independent scalar implementation has no ncnn dependency.
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

static unsigned number(const std::string& text, unsigned low, unsigned high)
{
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("invalid integer");
    size_t end = 0;
    const auto n = std::stoul(text, &end);
    if (end != text.size() || n < low || n > high)
        throw std::runtime_error("integer outside bounds");
    return static_cast<unsigned>(n);
}

static std::array<double, 6> reference(unsigned seed, unsigned sample)
{
    double image[2][5][5]{};
    for (int c = 0; c < 2; ++c)
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x) {
                const unsigned position = static_cast<unsigned>(c * 25 + y * 5 + x);
                const int centered = static_cast<int>(((seed + sample) * 17u + position * 13u) % 41u) - 20;
                image[c][y][x] = centered * 0.0625;
            }
    std::array<double, 3> average{};
    const double offsets[3] = {-0.125, 0.0625, 0.1875};
    for (int out = 0; out < 3; ++out) {
        double pooled = 0;
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x) {
                double activation = offsets[out];
                for (int in = 0; in < 2; ++in)
                    for (int ky = 0; ky < 3; ++ky)
                        for (int kx = 0; kx < 3; ++kx) {
                            const int flattened = ((out * 2 + in) * 3 + ky) * 3 + kx;
                            const double coefficient = static_cast<double>((flattened * 7 + 3) % 17 - 8) * 0.03125;
                            activation += image[in][y + ky][x + kx] * coefficient;
                        }
                pooled += std::max(activation, 0.0);
            }
        average[static_cast<size_t>(out)] = pooled / 9.0;
    }
    std::array<double, 6> result{};
    const double dense_bias[3] = {0.0625, -0.125, 0.25};
    for (int out = 0; out < 3; ++out) {
        double value = dense_bias[out];
        for (int in = 0; in < 3; ++in) {
            const int index = out * 3 + in;
            const double weight = static_cast<double>((index * 5 + 1) % 11 - 5) / 16.0;
            value += average[static_cast<size_t>(in)] * weight;
        }
        result[static_cast<size_t>(out)] = value;
    }
    const double largest = std::max({result[0], result[1], result[2]});
    double total = 0;
    for (size_t i = 0; i < 3; ++i) {
        result[i + 3] = std::exp(result[i] - largest);
        total += result[i + 3];
    }
    for (size_t i = 3; i < 6; ++i)
        result[i] /= total;
    return result;
}

int main(int argc, char** argv)
{
    try {
        if (argc != 5)
            throw std::runtime_error("Usage: check-ncnn-csv CSV SEED COUNT REPEAT");
        const unsigned seed = number(argv[2], 0, 100000);
        const unsigned count = number(argv[3], 1, 64);
        const unsigned repeats = number(argv[4], 1, 1000);
        std::ifstream in(argv[1]);
        std::string line;
        if (!in || !std::getline(in, line) ||
            line != "seed,sample,repeat,logit0,logit1,logit2,prob0,prob1,prob2")
            throw std::runtime_error("CSV header mismatch");
        unsigned rows = 0;
        for (unsigned sample = 0; sample < count; ++sample) {
            const auto expected = reference(seed, sample);
            for (unsigned repeat = 0; repeat < repeats; ++repeat) {
                if (!std::getline(in, line) || line.empty() || line.back() == ',')
                    throw std::runtime_error("missing or malformed row");
                std::istringstream row(line);
                std::vector<std::string> fields;
                for (std::string field; std::getline(row, field, ',');)
                    fields.push_back(field);
                if (fields.size() != 9 || number(fields[0], 0, 100000) != seed ||
                    number(fields[1], 0, 63) != sample || number(fields[2], 0, 999) != repeat)
                    throw std::runtime_error("row order or identity mismatch");
                double sum = 0;
                for (size_t k = 0; k < 6; ++k) {
                    size_t end = 0;
                    const auto& text = fields[k + 3];
                    const double value = std::stod(text, &end);
                    if (end != text.size() || !std::isfinite(value) ||
                        std::abs(value - expected[k]) > 1e-5 * std::max(1.0, std::abs(expected[k])))
                        throw std::runtime_error("scalar reference mismatch at row " + std::to_string(rows));
                    if (k >= 3) {
                        if (value < 0 || value > 1)
                            throw std::runtime_error("probability outside [0,1]");
                        sum += value;
                    }
                }
                if (std::abs(sum - 1.0) > 1e-5)
                    throw std::runtime_error("probabilities do not sum to one");
                ++rows;
            }
        }
        if (std::getline(in, line) || !in.eof())
            throw std::runtime_error("extra row or CSV read error");
        std::cout << "PASS: " << rows << " rows, six outputs each, independent scalar CNN\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CSV check: " << error.what() << '\n';
        return 1;
    }
}
