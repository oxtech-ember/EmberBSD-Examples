// SPDX-License-Identifier: MIT
// Independent oracle: libpng, SQLite and direct arithmetic; no RTAB-Map/PCL/Eigen.
#include "recording.h"
#include "png-reader.h"
#include <sqlite3.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Matrix = std::array<std::array<double, 4>, 3>;
using Point = std::array<double, 3>;
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
double number(const std::string& text)
{
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    double value = 0;
    input >> std::noskipws >> value;
    require(input && input.peek() == std::char_traits<char>::eof() && std::isfinite(value),
        "invalid or nonfinite number");
    return value;
}
std::vector<std::string> fields(const std::string& text, std::size_t count)
{
    std::vector<std::string> result;
    std::size_t start = 0;
    for (;;) {
        const auto comma = text.find(',', start);
        result.push_back(text.substr(start, comma == std::string::npos ? comma : comma - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    require(result.size() == count, "wrong CSV field count");
    return result;
}
std::string line(std::istream& input)
{
    std::string text;
    require(static_cast<bool>(std::getline(input, text)), "missing result line");
    return text;
}
void finished(std::istream& input)
{
    std::string extra;
    require(!std::getline(input, extra) && input.eof() && !input.bad(), "extra result data or read failure");
}
void near(double actual, double expected, double tolerance)
{
    require(std::isfinite(actual) && std::isfinite(expected) && std::abs(actual - expected) <= tolerance,
        "result differs from independent numeric expectation");
}
Matrix absolute_pose(const RecordedFrame& frame)
{
    double norm = 0;
    for (double value : frame.quaternion) norm += value * value;
    norm = std::sqrt(norm);
    const double x = frame.quaternion[0] / norm, y = frame.quaternion[1] / norm;
    const double z = frame.quaternion[2] / norm, w = frame.quaternion[3] / norm;
    return {{{1 - 2 * (y*y + z*z), 2 * (x*y - z*w), 2 * (x*z + y*w), frame.position[0]},
        {2 * (x*y + z*w), 1 - 2 * (x*x + z*z), 2 * (y*z - x*w), frame.position[1]},
        {2 * (x*z - y*w), 2 * (y*z + x*w), 1 - 2 * (x*x + y*y), frame.position[2]}}};
}
Matrix relative_pose(const Matrix& origin, const Matrix& current)
{
    Matrix result{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column)
            for (int k = 0; k < 3; ++k) result[row][column] += origin[k][row] * current[k][column];
        for (int k = 0; k < 3; ++k) result[row][3] += origin[k][row] * (current[k][3] - origin[k][3]);
    }
    return result;
}
Point transform(const Matrix& pose, const Point& local)
{
    Point result{};
    for (int row = 0; row < 3; ++row) {
        result[row] = pose[row][3];
        for (int column = 0; column < 3; ++column) result[row] += pose[row][column] * local[column];
    }
    return result;
}
double distance(const Point& first, const Point& second)
{
    double squared = 0;
    for (int coordinate = 0; coordinate < 3; ++coordinate)
        squared += std::pow(first[coordinate] - second[coordinate], 2);
    return std::sqrt(squared);
}
void check_pose(const Matrix& actual, const Matrix& expected)
{
    Point actual_position{}, expected_position{};
    double trace = 0;
    for (int row = 0; row < 3; ++row) {
        actual_position[row] = actual[row][3];
        expected_position[row] = expected[row][3];
        for (int column = 0; column < 3; ++column) {
            double product = 0;
            for (int k = 0; k < 3; ++k) product += actual[k][row] * actual[k][column];
            near(product, row == column ? 1 : 0, 1e-4);
            trace += actual[row][column] * expected[row][column];
        }
    }
    require(distance(actual_position, expected_position) <= 0.03, "map position differs from recorded pose");
    const double determinant = actual[0][0] * (actual[1][1] * actual[2][2] - actual[1][2] * actual[2][1]) -
        actual[0][1] * (actual[1][0] * actual[2][2] - actual[1][2] * actual[2][0]) +
        actual[0][2] * (actual[1][0] * actual[2][1] - actual[1][1] * actual[2][0]);
    near(determinant, 1, 1e-4);
    require((trace - 1) / 2 >= std::cos(0.02) - 2e-6, "map rotation differs from recorded pose");
}
void check_database(const std::filesystem::path& file)
{
    sqlite3* raw = nullptr;
    const int opened = sqlite3_open_v2(file.c_str(), &raw, SQLITE_OPEN_READONLY, nullptr);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(raw, sqlite3_close);
    require(opened == SQLITE_OK, "cannot open recorded SQLite map");
    for (const auto sql : {"PRAGMA quick_check", "SELECT count(*) FROM Node"}) {
        sqlite3_stmt* query = nullptr;
        require(sqlite3_prepare_v2(raw, sql, -1, &query, nullptr) == SQLITE_OK, "invalid SQLite map schema");
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(query, sqlite3_finalize);
        require(sqlite3_step(query) == SQLITE_ROW, "missing SQLite result");
        if (std::string(sql) == "PRAGMA quick_check") {
            const auto* text = sqlite3_column_text(query, 0);
            require(text && std::string(reinterpret_cast<const char*>(text)) == "ok", "SQLite integrity check failed");
        } else require(sqlite3_column_int(query, 0) == 12, "SQLite map lost recorded nodes");
        require(sqlite3_step(query) == SQLITE_DONE, "unexpected SQLite rows");
    }
}
void check(const std::filesystem::path& dataset, const std::filesystem::path& results)
{
    const auto frames = read_recording(dataset);
    std::ifstream poses(results / "poses.csv"), cloud(results / "cloud.csv"), ply(results / "map.ply");
    require(poses && cloud && ply, "missing map exports");
    require(line(poses) == "id,stamp,words,m00,m01,m02,m03,m10,m11,m12,m13,m20,m21,m22,m23", "wrong pose header");
    require(line(cloud) == "id,u,v,x,y,z,r,g,b", "wrong point-cloud header");
    require(line(ply) == "ply" && line(ply) == "format ascii 1.0", "wrong PLY format");
    const auto vertex = line(ply);
    const std::string vertex_prefix = "element vertex ";
    require(vertex.rfind(vertex_prefix, 0) == 0, "missing PLY vertex count");
    const double declared_vertices = number(vertex.substr(vertex_prefix.size()));
    require(declared_vertices >= 1000 && std::floor(declared_vertices) == declared_vertices, "invalid PLY vertex count");
    for (const auto property : {"property float x", "property float y", "property float z", "property uchar red", "property uchar green", "property uchar blue", "end_header"})
        require(line(ply) == property, "unexpected PLY property");
    const auto origin = absolute_pose(frames.front());
    std::size_t point_count = 0;
    for (const auto& frame : frames) {
        const auto values = fields(line(poses), 15);
        require(values[0] == std::to_string(frame.id), "wrong pose id");
        near(number(values[1]), frame.rgb_stamp, 1e-6);
        const double words = number(values[2]);
        require(words > 0 && words <= 200 && std::floor(words) == words, "missing or invalid visual-word count");
        Matrix pose{};
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 4; ++column) pose[row][column] = number(values[3 + row * 4 + column]);
        const auto reference = relative_pose(origin, absolute_pose(frame));
        check_pose(pose, reference);
        void *raw_color = nullptr, *raw_depth = nullptr;
        char error[256]{};
        require(read_tum_png((dataset / frame.rgb).c_str(), 0, &raw_color, error, sizeof(error)), error);
        std::unique_ptr<void, decltype(&std::free)> color_storage(raw_color, std::free);
        require(read_tum_png((dataset / frame.depth).c_str(), 1, &raw_depth, error, sizeof(error)), error);
        std::unique_ptr<void, decltype(&std::free)> depth_storage(raw_depth, std::free);
        const auto* rgb = static_cast<const unsigned char*>(raw_color);
        const auto* depth = static_cast<const std::uint16_t*>(raw_depth);
        for (int v = 0; v < 480; v += 16) {
            for (int u = 0; u < 640; u += 16) {
                const double z = depth[v * 640 + u] / 5000.0;
                if (z < 0.1 || z > 4.0) continue;
                const auto point = fields(line(cloud), 9);
                require(point[0] == std::to_string(frame.id) && point[1] == std::to_string(u) && point[2] == std::to_string(v),
                    "missing, reordered or duplicated cloud sample");
                const Point local{(u - 319.5) * z / 525.0, (v - 239.5) * z / 525.0, z};
                const auto expected = transform(pose, local);
                const auto from_ground_truth = transform(reference, local);
                Point actual{};
                for (int coordinate = 0; coordinate < 3; ++coordinate) {
                    actual[coordinate] = number(point[3 + coordinate]);
                    near(actual[coordinate], expected[coordinate], 2e-5);
                }
                require(distance(actual, from_ground_truth) <= 0.12, "cloud geometry differs from recorded reference");
                std::istringstream ply_row(line(ply));
                ply_row.imbue(std::locale::classic());
                std::array<std::string, 6> ply_values;
                for (auto& value : ply_values) ply_row >> value;
                require(ply_row && (ply_row >> std::ws, ply_row.eof()), "invalid PLY point row");
                for (int coordinate = 0; coordinate < 3; ++coordinate)
                    near(number(ply_values[coordinate]), actual[coordinate], 0);
                for (int channel = 0; channel < 3; ++channel) {
                    const auto expected_color = std::to_string(rgb[(v * 640 + u) * 3 + channel]);
                    require(point[6 + channel] == expected_color && ply_values[3 + channel] == expected_color,
                        "cloud color differs from recorded RGB pixel");
                }
                ++point_count;
            }
        }
    }
    finished(poses); finished(cloud); finished(ply);
    require(point_count == declared_vertices, "PLY point count differs from all valid recorded depth samples");
    check_database(results / "map.db");
    std::cout << "PASS independent recorded map: nodes=12 points=" << point_count << '\n';
}
} // namespace

int main(int argc, char** argv)
{
    std::string dataset, results;
    bool dataset_seen = false, results_seen = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--dataset" && !dataset_seen && i + 1 < argc) { dataset = argv[++i]; dataset_seen = true; }
        else if (argument == "--results" && !results_seen && i + 1 < argc) { results = argv[++i]; results_seen = true; }
        else { std::cerr << "Usage: check-recorded-map --dataset FIXTURE --results RESULTS\n"; return 64; }
    }
    if (dataset.empty() || results.empty()) return 64;
    try {
        check(dataset, results);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
