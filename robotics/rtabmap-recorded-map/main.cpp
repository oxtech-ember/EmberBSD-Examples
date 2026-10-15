// SPDX-License-Identifier: MIT
// Origin: EmberBSD (AI-assisted). Recorded-pose mapping with real RGB-D input.
#include "recording.h"
#include <rtabmap/core/CameraModel.h>
#include <rtabmap/core/Memory.h>
#include <rtabmap/core/Rtabmap.h>
#include <rtabmap/core/Signature.h>
#include <rtabmap/core/util3d.h>
#include <rtabmap/core/util3d_transforms.h>
#include <opencv2/imgcodecs.hpp>
#include <sqlite3.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
class OutputFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void write_file(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw OutputFailure("cannot open output file");
    output << text;
    output.flush();
    const bool written = static_cast<bool>(output);
    output.close();
    if (!written || output.fail()) throw OutputFailure("output write, flush or close failed");
}
rtabmap::Transform recorded_pose(const RecordedFrame& frame)
{
    const auto& p = frame.position;
    const auto& q = frame.quaternion;
    return rtabmap::Transform(p[0], p[1], p[2], q[0], q[1], q[2], q[3]);
}
int database_nodes(const std::filesystem::path& path)
{
    sqlite3* raw = nullptr;
    const int opened = sqlite3_open_v2(path.c_str(), &raw, SQLITE_OPEN_READONLY, nullptr);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(raw, sqlite3_close);
    require(opened == SQLITE_OK, "cannot open persisted SQLite map");
    sqlite3_stmt* query = nullptr;
    require(sqlite3_prepare_v2(raw, "SELECT count(*) FROM Node", -1, &query, nullptr) == SQLITE_OK,
        "missing map node table");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(query, sqlite3_finalize);
    require(sqlite3_step(query) == SQLITE_ROW, "cannot count map nodes");
    return sqlite3_column_int(query, 0);
}
void usage()
{
    std::cerr << "Usage: ember-rtabmap-recorded-map --dataset FIXTURE --output NEW_RESULTS [--validate-only]\n";
}
struct Images { cv::Mat color, depth; };
} // namespace

int main(int argc, char** argv)
{
    std::string dataset_name, output_name;
    bool validate_only = false, dataset_seen = false, output_seen = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--dataset" && !dataset_seen && i + 1 < argc) { dataset_name = argv[++i]; dataset_seen = true; }
        else if (argument == "--output" && !output_seen && i + 1 < argc) { output_name = argv[++i]; output_seen = true; }
        else if (argument == "--validate-only" && !validate_only) validate_only = true;
        else { usage(); return 64; }
    }
    if (dataset_name.empty() || output_name.empty()) { usage(); return 64; }
    const std::filesystem::path dataset(dataset_name), output(output_name);
    std::error_code filesystem_error;
    if (std::filesystem::exists(output, filesystem_error) || std::filesystem::is_symlink(output, filesystem_error)) {
        std::cerr << "Output path already exists\n";
        return 74;
    }
    cv::setNumThreads(1);
    cv::setRNGSeed(0);
    std::vector<RecordedFrame> frames;
    std::vector<Images> images;
    try {
        frames = read_recording(dataset);
        for (const auto& frame : frames) {
            Images image;
            image.color = cv::imread((dataset / frame.rgb).string(), cv::IMREAD_UNCHANGED);
            const cv::Mat raw_depth = cv::imread((dataset / frame.depth).string(), cv::IMREAD_UNCHANGED);
            require(image.color.size() == cv::Size(640, 480) && image.color.type() == CV_8UC3 &&
                raw_depth.size() == cv::Size(640, 480) && raw_depth.type() == CV_16UC1,
                "expected 640x480 RGB8 and uint16 depth PNG input");
            require(cv::countNonZero(raw_depth) > 1000, "recorded depth has insufficient valid data");
            raw_depth.convertTo(image.depth, CV_32FC1, 1.0 / 5000.0);
            images.push_back(std::move(image));
        }
    } catch (const std::exception& error) {
        std::cerr << "Invalid input: " << error.what() << '\n';
        return 65;
    }
    if (validate_only) { std::cout << "PASS 12 recorded RGB-D inputs\n"; return 0; }
    try {
        if (!std::filesystem::create_directory(output, filesystem_error))
            throw OutputFailure("cannot create new results directory");
        const auto database = output / "map.db";
        rtabmap::ParametersMap parameters = {
            {"Mem/IncrementalMemory", "true"}, {"Mem/GenerateIds", "false"},
            {"Mem/STMSize", "12"}, {"Mem/RehearsalSimilarity", "1.0"},
            {"Mem/NotLinkedNodesKept", "true"}, {"Mem/ImageCompressionFormat", ".png"},
            {"Mem/DepthCompressionFormat", ".png"},
            {"RGBD/Enabled", "true"}, {"RGBD/ProximityBySpace", "false"},
            {"RGBD/LinearUpdate", "0"}, {"RGBD/AngularUpdate", "0"},
            {"Kp/DetectorStrategy", "2"}, {"Kp/MaxFeatures", "200"},
            {"Vis/FeatureType", "2"}, {"Vis/MaxFeatures", "200"},
            {"Reg/Strategy", "0"}, {"Optimizer/Strategy", "2"},
            {"Grid/FromDepth", "false"}
        };
        const rtabmap::CameraModel camera(525, 525, 319.5, 239.5,
            rtabmap::Transform::getIdentity(), 0, cv::Size(640, 480));
        const auto origin_inverse = recorded_pose(frames.front()).inverse();
        std::map<int, rtabmap::Transform> recorded;
        {
            rtabmap::Rtabmap mapper;
            mapper.init(parameters, database.string(), false);
            for (std::size_t i = 0; i < frames.size(); ++i) {
                const auto& frame = frames[i];
                const auto pose = origin_inverse * recorded_pose(frame);
                recorded.emplace(frame.id, pose);
                rtabmap::SensorData data(images[i].color, images[i].depth, camera, frame.id, frame.rgb_stamp);
                const cv::Mat covariance = cv::Mat::eye(6, 6, CV_64FC1) * 1e-6;
                require(mapper.process(data, pose, covariance), "recorded frame was not added to the map");
            }
            mapper.close(true);
        }
        require(database_nodes(database) == 12, "persisted map must contain all 12 nodes");
        parameters["Mem/IncrementalMemory"] = "false";
        parameters["Mem/InitWMWithAllNodes"] = "true";
        parameters["Mem/LocalizationReadOnly"] = "true";
        rtabmap::Rtabmap reopened;
        reopened.init(parameters, database.string(), false);
        std::map<int, rtabmap::Transform> poses;
        std::multimap<int, rtabmap::Link> constraints;
        std::map<int, rtabmap::Signature> signatures;
        reopened.getGraph(poses, constraints, true, true, &signatures, true, false, false, false, true);
        require(poses.size() == 12 && signatures.size() == 12 && constraints.size() >= 11,
            "reopened map lost graph nodes or constraints");
        std::ostringstream pose_csv, cloud_csv, ply_points, metrics;
        for (auto* stream : {&pose_csv, &cloud_csv, &ply_points, &metrics}) {
            stream->imbue(std::locale::classic());
            *stream << std::setprecision(17);
        }
        pose_csv << "id,stamp,words,m00,m01,m02,m03,m10,m11,m12,m13,m20,m21,m22,m23\n";
        cloud_csv << "id,u,v,x,y,z,r,g,b\n";
        std::size_t count = 0, words = 0;
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const auto& frame = frames[i];
            const auto pose = poses.at(frame.id);
            require(!pose.isNull(), "null persisted map pose");
            for (int coefficient = 0; coefficient < 12; ++coefficient)
                require(std::isfinite(pose[coefficient]), "nonfinite map pose");
            const auto error = recorded.at(frame.id).inverse() * pose;
            require(error.getNorm() <= 0.03f && error.getAngle(rtabmap::Transform::getIdentity()) <= 0.02f,
                "map pose differs from its recorded reference");
            auto& signature = signatures.at(frame.id);
            signature.sensorData().uncompressData();
            const auto& data = signature.sensorData();
            require(data.imageRaw().type() == CV_8UC3 && data.imageRaw().size() == images[i].color.size() &&
                cv::norm(data.imageRaw(), images[i].color, cv::NORM_INF) == 0,
                "persisted PNG RGB differs from recorded input");
            require(data.depthRaw().type() == CV_32FC1 && data.depthRaw().size() == images[i].depth.size() &&
                cv::norm(data.depthRaw(), images[i].depth, cv::NORM_INF) == 0,
                "persisted PNG depth differs from metric recorded input");
            require(!signature.getWords().empty(), "recorded image produced no stored visual words");
            words += signature.getWords().size();
            pose_csv << frame.id << ',' << frame.rgb_stamp << ',' << signature.getWords().size();
            for (int coefficient = 0; coefficient < 12; ++coefficient) pose_csv << ',' << pose[coefficient];
            pose_csv << '\n';
            std::vector<int> valid;
            const auto local = rtabmap::util3d::cloudRGBFromSensorData(data, 16, 4.0f, 0.1f, &valid);
            require(local->width == 40 && local->height == 30 && !valid.empty(), "unexpected projected map cloud");
            const auto world = rtabmap::util3d::transformPointCloud(local, pose);
            for (int index : valid) {
                const auto& point = world->at(static_cast<std::size_t>(index));
                require(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z),
                    "nonfinite exported map point");
                const int u = (index % 40) * 16, v = (index / 40) * 16;
                cloud_csv << frame.id << ',' << u << ',' << v << ',' << point.x << ',' << point.y << ','
                    << point.z << ',' << unsigned(point.r) << ',' << unsigned(point.g) << ',' << unsigned(point.b) << '\n';
                ply_points << point.x << ' ' << point.y << ' ' << point.z << ' ' << unsigned(point.r) << ' '
                    << unsigned(point.g) << ' ' << unsigned(point.b) << '\n';
                ++count;
            }
        }
        reopened.close(false);
        require(database_nodes(database) == 12, "read-only reopening changed map node count");
        require(count >= 1000 && words > 0, "map has insufficient saved geometry or visual words");
        const std::string ply_header = "ply\nformat ascii 1.0\nelement vertex " + std::to_string(count) +
            "\nproperty float x\nproperty float y\nproperty float z\nproperty uchar red\nproperty uchar green\nproperty uchar blue\nend_header\n";
        write_file(output / "poses.csv", pose_csv.str());
        write_file(output / "cloud.csv", cloud_csv.str());
        write_file(output / "map.ply", ply_header + ply_points.str());
        metrics << "frames=12\nmap_nodes=12\nconstraints=" << constraints.size() << "\nvisual_words=" << words <<
            "\nmap_points=" << count << "\ndecimation=16\ndepth_units_per_metre=5000\n";
        write_file(output / "metrics.txt", metrics.str());
        std::cout << "PASS recorded RGB-D map: nodes=12 points=" << count << " words=" << words << '\n';
        return 0;
    } catch (const OutputFailure& error) {
        std::cerr << "Output failure: " << error.what() << '\n';
        return 74;
    } catch (const std::exception& error) {
        std::cerr << "Mapping failure: " << error.what() << '\n';
        return 70;
    }
}
