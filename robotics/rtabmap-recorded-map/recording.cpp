// SPDX-License-Identifier: MIT
#include "recording.h"
#include <cmath>
#include <fstream>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

std::vector<RecordedFrame> read_recording(const std::filesystem::path& dataset)
{
    std::ifstream input(dataset / "frames.tsv");
    input.imbue(std::locale::classic());
    std::string line;
    if (!std::getline(input, line) || line != "id rgb_stamp depth_stamp gt_stamp rgb depth tx ty tz qx qy qz qw")
        throw std::runtime_error("invalid frames.tsv header");
    std::vector<RecordedFrame> result;
    std::set<std::string> paths;
    double previous_rgb = -1, previous_depth = -1;
    while (std::getline(input, line)) {
        RecordedFrame frame{};
        std::istringstream row(line);
        row.imbue(std::locale::classic());
        row >> frame.id >> frame.rgb_stamp >> frame.depth_stamp >> frame.gt_stamp >> frame.rgb >> frame.depth;
        for (auto& value : frame.position) row >> value;
        for (auto& value : frame.quaternion) row >> value;
        if (!row || (row >> std::ws, !row.eof()) || frame.id != static_cast<int>(result.size()) + 1)
            throw std::runtime_error("invalid recorded frame row");
        for (double stamp : {frame.rgb_stamp, frame.depth_stamp, frame.gt_stamp})
            if (!std::isfinite(stamp) || stamp <= 0) throw std::runtime_error("invalid frame timestamp");
        if (frame.rgb_stamp <= previous_rgb || frame.depth_stamp <= previous_depth ||
            std::abs(frame.rgb_stamp - frame.depth_stamp) > 0.020 ||
            std::abs(frame.rgb_stamp - frame.gt_stamp) > 0.020)
            throw std::runtime_error("unordered or unassociated frame timestamps");
        previous_rgb = frame.rgb_stamp;
        previous_depth = frame.depth_stamp;
        double norm = 0;
        for (double value : frame.position)
            if (!std::isfinite(value)) throw std::runtime_error("nonfinite recorded position");
        for (double value : frame.quaternion) {
            if (!std::isfinite(value)) throw std::runtime_error("nonfinite recorded quaternion");
            norm += value * value;
        }
        if (std::abs(norm - 1) > 0.001) throw std::runtime_error("recorded quaternion is not normalized");
        for (const auto& name : {frame.rgb, frame.depth}) {
            const std::filesystem::path path(name);
            if (path.is_absolute() || name.find("..") != std::string::npos ||
                path.extension() != ".png" || !paths.insert(name).second ||
                !std::filesystem::is_regular_file(dataset / path))
                throw std::runtime_error("missing, reused or invalid recorded image path");
        }
        result.push_back(frame);
    }
    if (input.bad() || result.size() != 12) throw std::runtime_error("expected exactly 12 recorded frames");
    return result;
}
