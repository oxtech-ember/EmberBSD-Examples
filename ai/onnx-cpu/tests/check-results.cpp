// SPDX-License-Identifier: MIT
// Independent scalar acceptance. Do not include model-generator or app code.
// This executable has no ONNX Runtime, protobuf, or matrix-library dependency.
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::size_t count(const std::string& text, std::size_t maximum)
{
    require(!text.empty() && text.find_first_not_of("0123456789") == std::string::npos,
            "invalid count");
    std::size_t used = 0;
    const auto value = std::stoul(text, &used);
    require(used == text.size() && value >= 1 && value <= maximum, "count out of range");
    return value;
}

double number(const std::string& text)
{
    require(!text.empty(), "missing numeric value");
    std::size_t used = 0;
    const double value = std::stod(text, &used);
    require(used == text.size() && std::isfinite(value), "invalid or nonfinite numeric value");
    return value;
}

std::vector<std::string> split(const std::string& line)
{
    std::vector<std::string> fields;
    std::size_t begin = 0;
    for (;;) {
        const auto comma = line.find(',', begin);
        if (comma == std::string::npos) {
            fields.push_back(line.substr(begin));
            return fields;
        }
        fields.push_back(line.substr(begin, comma - begin));
        begin = comma + 1;
    }
}

std::array<double, 2> scalar_model(const std::array<double, 3>& input)
{
    // These coefficients are stated independently of the protobuf encoder.
    // h = relu(x W1 + b1); y = h W2 + b2.
    std::array<double, 4> hidden{
        input[0] + 0.5 * input[1] - input[2] + 0.25,
        -2.0 * input[0] + input[1] - 0.5,
        0.5 * input[0] - input[1] + 2.0 * input[2] + 1.0,
        2.0 * input[1] - 0.5 * input[2]
    };
    for (double& value : hidden) if (value < 0) value = 0;
    return {hidden[0] + 0.5 * hidden[1] - 2.0 * hidden[2] + 1.5 * hidden[3] + 0.75,
            -hidden[0] + 2.0 * hidden[1] + 0.25 * hidden[2] - 0.5 * hidden[3] - 1.25};
}
} // namespace

int main(int argc, char** argv)
{
    try {
        require(argc == 4, "expected BATCH REPEATS CSV");
        const auto batch = count(argv[1], 32);
        const auto repeats = count(argv[2], 1000);
        std::ifstream input(argv[3]);
        require(input.is_open(), "cannot open CSV");
        std::string line;
        require(static_cast<bool>(std::getline(input, line)) && line == "run,row,x0,x1,x2,y0,y1",
                "missing or incorrect CSV header");
        for (std::size_t run = 0; run < repeats; ++run) {
            for (std::size_t row = 0; row < batch; ++row) {
                require(static_cast<bool>(std::getline(input, line)) && line.size() < 512,
                        "missing or oversized CSV row");
                const auto fields = split(line);
                require(fields.size() == 7 && fields[0] == std::to_string(run) &&
                        fields[1] == std::to_string(row), "wrong row order or column count");
                const std::array<double, 3> expected_input{
                    static_cast<double>(row) - 2.0 + 0.25 * static_cast<double>(run % 3),
                    static_cast<double>((3 * row + run) % 7) - 3.0,
                    ((row + run) % 2 == 0) ? -1.5 : 0.5
                };
                for (std::size_t i = 0; i < 3; ++i)
                    require(number(fields[2 + i]) == expected_input[i], "incorrect input sample");
                const auto expected = scalar_model(expected_input);
                for (std::size_t i = 0; i < 2; ++i)
                    require(std::abs(number(fields[5 + i]) - expected[i]) <= 1e-5,
                            "inference differs from independent scalar answer");
            }
        }
        require(!std::getline(input, line) && input.eof(), "extra CSV rows or input read error");
        std::cout << "PASS " << batch * repeats << " rows; 3-4-2 MLP scalar answers\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
