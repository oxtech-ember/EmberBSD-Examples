// SPDX-License-Identifier: MIT
// Independent oracle. This program does not include or link PCL or Eigen.
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
class InputFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::string> fields(const std::string& line)
{
    std::vector<std::string> result;
    std::size_t start = 0;
    for (;;) {
        const auto comma = line.find(',', start);
        result.push_back(line.substr(start, comma == std::string::npos ? comma : comma - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    require(result.size() == 7, "expected seven CSV fields");
    return result;
}

double number(const std::string& text)
{
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    double result = 0;
    stream >> std::noskipws >> result;
    require(stream && stream.peek() == std::char_traits<char>::eof() &&
        std::isfinite(result), "invalid or nonfinite CSV number");
    return result;
}

void near(double actual, double expected, double tolerance)
{
    require(std::isfinite(actual) && std::isfinite(expected) &&
        std::abs(actual - expected) <= tolerance, "CSV differs from the independent expected value");
}

class Reader {
public:
    Reader(std::istream& input, const std::string& test) : input_(input), test_(test)
    {
        std::string line;
        const bool read = static_cast<bool>(std::getline(input_, line));
        if (input_.bad()) throw InputFailure("CSV header read failed");
        require(read &&
            line == "case,kind,index,a,b,c,d", "invalid CSV header");
    }

    std::array<double, 4> row(const char* kind, int index)
    {
        std::string line;
        const bool read = static_cast<bool>(std::getline(input_, line));
        if (input_.bad()) throw InputFailure("CSV row read failed");
        require(read, "missing CSV row");
        const auto values = fields(line);
        require(values[0] == test_ && values[1] == kind && values[2] == std::to_string(index),
            "unexpected CSV case, kind or index");
        return {number(values[3]), number(values[4]), number(values[5]), number(values[6])};
    }

    void finish()
    {
        std::string extra;
        const bool read = static_cast<bool>(std::getline(input_, extra));
        if (input_.bad()) throw InputFailure("CSV final read failed");
        require(!read && input_.eof(), "extra CSV data");
    }
private:
    std::istream& input_;
    std::string test_;
};

std::array<double, 3> source(int index)
{
    return {(index % 5) * 0.8 + (index % 3) * 0.03,
        ((index / 5) % 3) * 0.9 + (index % 2) * 0.07,
        (index / 15) * 1.1 + (index % 7) * 0.04};
}

std::array<double, 3> target(int index)
{
    const auto point = source(index);
    return {std::cos(0.012) * point[0] - std::sin(0.012) * point[1] + 0.04,
        std::sin(0.012) * point[0] + std::cos(0.012) * point[1] - 0.03,
        point[2] + 0.02};
}

void validate(std::istream& input, const std::string& test)
{
    Reader csv(input, test);
    if (test == "filter") {
        for (int i = 0; i < 2; ++i) {
            const auto row = csv.row("centroid", i);
            near(row[0], 0.2 + i, 1e-6);
            near(row[1], 0.2, 1e-6);
            near(row[2], 0.2, 1e-6);
            near(row[3], 0, 0);
        }
    } else if (test == "registration") {
        for (const auto kind : {"source", "target", "aligned"}) {
            for (int i = 0; i < 30; ++i) {
                const auto row = csv.row(kind, i);
                const auto expected = std::string(kind) == "source" ? source(i) : target(i);
                const double tolerance = std::string(kind) == "aligned" ? 1e-4 : 2e-6;
                double squared_error = 0;
                for (int coordinate = 0; coordinate < 3; ++coordinate) {
                    near(row[coordinate], expected[coordinate], tolerance);
                    squared_error += std::pow(row[coordinate] - expected[coordinate], 2);
                }
                require(squared_error < tolerance * tolerance, "point error exceeds the norm tolerance");
                near(row[3], 0, 0);
            }
        }
        const double c = std::cos(0.012), s = std::sin(0.012);
        const std::array<std::array<double, 4>, 4> expected = {{{c, -s, 0, 0.04},
            {s, c, 0, -0.03}, {0, 0, 1, 0.02}, {0, 0, 0, 1}}};
        for (int i = 0; i < 4; ++i) {
            const auto row = csv.row("matrix", i);
            for (int j = 0; j < 4; ++j) near(row[j], expected[i][j], 1e-4);
        }
        const auto fitness = csv.row("fitness", 0);
        require(fitness[0] >= 0 && fitness[0] < 1e-8, "invalid ICP fitness");
        for (int j = 1; j < 4; ++j) near(fitness[j], 0, 0);
    }
    csv.finish();
}

void usage()
{
    std::cerr << "Usage: check-pcl-csv --case filter|registration|empty-filter --input FILE|-\n";
}
} // namespace

int main(int argc, char** argv)
{
    std::string test, path;
    bool case_seen = false, input_seen = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--case" && !case_seen && i + 1 < argc) {
            test = argv[++i];
            case_seen = true;
        } else if (argument == "--input" && !input_seen && i + 1 < argc) {
            path = argv[++i];
            input_seen = true;
        }
        else { usage(); return 64; }
    }
    if (path.empty() || (test != "filter" && test != "registration" && test != "empty-filter")) {
        usage();
        return 64;
    }
    try {
        if (path == "-") validate(std::cin, test);
        else {
            std::ifstream input(path, std::ios::binary);
            if (!input) return 74;
            validate(input, test);
            if (input.bad()) return 74;
        }
        std::cout << "PASS " << test << '\n';
        std::cout.flush();
        if (!std::cout) return 74;
        return 0;
    } catch (const InputFailure& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 74;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
