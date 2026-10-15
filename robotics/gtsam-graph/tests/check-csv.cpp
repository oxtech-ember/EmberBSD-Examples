// SPDX-License-Identifier: MIT
// Independent CSV acceptance. This executable does not link GTSAM or Eigen.
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

std::vector<std::string> split(const std::string& line)
{
    std::vector<std::string> result;
    std::string::size_type begin = 0;
    for (;;) {
        const auto comma = line.find(',', begin);
        if (comma == std::string::npos) {
            result.push_back(line.substr(begin));
            return result;
        }
        result.push_back(line.substr(begin, comma - begin));
        begin = comma + 1;
    }
}

double number(const std::string& text)
{
    require(!text.empty(), "missing number");
    std::size_t used = 0;
    const double value = std::stod(text, &used);
    require(used == text.size() && std::isfinite(value), "invalid or nonfinite number");
    return value;
}

void close(double value, double expected, double tolerance)
{
    require(std::isfinite(value) && std::abs(value - expected) <= tolerance,
            "value differs from independent expectation");
}

void check_covariance(const std::vector<std::string>& fields, std::size_t key)
{
    std::array<double, 6> covariance{};
    for (std::size_t i = 0; i < covariance.size(); ++i)
        covariance[i] = number(fields[7 + i]);
    const double xx = covariance[0];
    const double xy = covariance[1];
    const double xt = covariance[2];
    const double yy = covariance[3];
    const double yt = covariance[4];
    const double tt = covariance[5];
    // Sylvester's criterion for the symmetric 3x3 covariance in the CSV.
    const double determinant = xx * yy * tt + 2 * xy * xt * yt -
                               xx * yt * yt - yy * xt * xt - tt * xy * xy;
    require(xx > 0 && xx * yy - xy * xy > 0 && std::isfinite(determinant) && determinant > 0,
            "covariance is not positive definite");
    if (key == 0) {
        // The only absolute measurement is an isotropic sigma=0.1 prior.
        const std::array<double, 6> expected = {0.01, 0, 0, 0.01, 0, 0.01};
        for (std::size_t i = 0; i < covariance.size(); ++i)
            close(covariance[i], expected[i], 1e-8);
    }
}
} // namespace

int main(int argc, char** argv)
{
    try {
        require(argc == 3, "expected linear|pose and a CSV path");
        const std::string scenario = argv[1];
        require(scenario == "linear" || scenario == "pose", "unknown case");
        const std::size_t count = scenario == "linear" ? 2 : 3;
        std::ifstream input(argv[2]);
        require(input.is_open(), "cannot open CSV");
        std::string line;
        require(static_cast<bool>(std::getline(input, line)), "missing CSV header");
        require(line == "case,stage,key,x,y,theta,cost,cov_xx,cov_xy,cov_xt,cov_yy,cov_yt,cov_tt",
                "unexpected CSV header");
        double initial_cost = 0;
        double optimized_cost = 0;
        for (std::size_t row = 0; row < 2 * count; ++row) {
            require(static_cast<bool>(std::getline(input, line)) && line.size() < 4096,
                    "missing or oversized CSV row");
            const auto fields = split(line);
            require(fields.size() == 13, "unexpected CSV column count");
            const std::size_t key = row % count;
            const bool optimized = row >= count;
            require(fields[0] == scenario && fields[1] == (optimized ? "optimized" : "initial") &&
                    fields[2] == std::to_string(key), "unexpected case, stage or key");
            const double x = number(fields[3]);
            const double cost = number(fields[6]);
            if (row == 0) initial_cost = cost;
            if (row == count) optimized_cost = cost;
            close(cost, optimized ? optimized_cost : initial_cost, 1e-14);
            if (scenario == "linear") {
                // Solve 2x0-x1=-2 and -x0+2x1=5 without GTSAM.
                const std::array<double, 2> expected = optimized ?
                    std::array<double, 2>{1.0 / 3.0, 8.0 / 3.0} : std::array<double, 2>{-1, 4};
                close(x, expected[key], 1e-10);
                close(cost, optimized ? 1.0 / 6.0 : 11.0 / 2.0, 1e-10);
                require(fields[4].empty() && fields[5].empty(), "linear row has unused pose coordinates");
                for (std::size_t i = 7; i < fields.size(); ++i)
                    require(fields[i].empty(), "linear covariance was not part of this contract");
            } else {
                const double pi = std::acos(-1.0);
                const std::array<std::array<double, 3>, 3> expected = optimized ?
                    std::array<std::array<double, 3>, 3>{{{0, 0, 0}, {2, 0, 0}, {2, 2, pi / 2}}} :
                    std::array<std::array<double, 3>, 3>{{{0.1, -0.2, 0.03}, {2.2, 0.1, -0.04}, {1.9, 2.2, pi / 2 + 0.05}}};
                close(x, expected[key][0], optimized ? 1e-7 : 1e-14);
                close(number(fields[4]), expected[key][1], optimized ? 1e-7 : 1e-14);
                close(number(fields[5]), expected[key][2], optimized ? 1e-7 : 1e-14);
                if (optimized) {
                    require(cost >= 0 && cost < 1e-12, "nonzero optimized pose residual");
                    check_covariance(fields, key);
                } else {
                    require(cost > 1, "invalid initial pose residual");
                    for (std::size_t i = 7; i < fields.size(); ++i)
                        require(fields[i].empty(), "initial covariance is not defined");
                }
            }
        }
        require(!std::getline(input, line), "unexpected extra CSV row");
        require(input.eof() && !input.bad(), "CSV read error");
        require(initial_cost > optimized_cost, "cost did not decrease");
        std::cout << "PASS " << scenario << " CSV\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
