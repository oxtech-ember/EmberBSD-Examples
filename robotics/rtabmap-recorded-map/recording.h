// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <vector>

struct RecordedFrame {
    int id;
    double rgb_stamp, depth_stamp, gt_stamp;
    std::string rgb, depth;
    std::array<double, 3> position;
    std::array<double, 4> quaternion; // x, y, z, w; camera optical frame to world.
};
std::vector<RecordedFrame> read_recording(const std::filesystem::path& dataset);
