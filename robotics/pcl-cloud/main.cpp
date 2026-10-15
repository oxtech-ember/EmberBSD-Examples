// SPDX-License-Identifier: MIT
// Developed with AI assistance for EmberBSD Examples.
#include <pcl/filters/passthrough.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/registration/icp.h>

#include <algorithm>
#include <cmath>
#include <csignal>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using Point = pcl::PointXYZ;
using Cloud = pcl::PointCloud<Point>;

class RejectedInput : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

bool finite_point(const Point& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

void point_row(std::ostream& output, const std::string& test, const char* kind,
    std::size_t index, const Point& point)
{
    require(finite_point(point), "nonfinite exported point");
    output << test << ',' << kind << ',' << index << ',' << point.x << ','
           << point.y << ',' << point.z << ",0\n";
}

void filtering(std::ostream& output, bool empty)
{
    auto input = pcl::make_shared<Cloud>();
    if (!empty) {
        // Two occupied one-metre voxels, a distant point and invalid input.
        input->push_back(Point(0.1f, 0.1f, 0.1f));
        input->push_back(Point(0.3f, 0.3f, 0.3f));
        input->push_back(Point(1.1f, 0.1f, 0.1f));
        input->push_back(Point(1.3f, 0.3f, 0.3f));
        input->push_back(Point(0, 0, 20));
        input->push_back(Point(std::numeric_limits<float>::quiet_NaN(), 0, 0));
        input->is_dense = false;
    }
    pcl::PassThrough<Point> pass;
    pass.setInputCloud(input);
    pass.setFilterFieldName("z");
    pass.setFilterLimits(0.0f, 2.0f);
    auto bounded = pcl::make_shared<Cloud>();
    pass.filter(*bounded);
    require(bounded->size() == (empty ? 0u : 4u), "unexpected pass-through count");
    pcl::VoxelGrid<Point> voxel;
    voxel.setInputCloud(bounded);
    voxel.setLeafSize(1, 1, 1);
    Cloud result;
    voxel.filter(result);
    require(result.size() == (empty ? 0u : 2u), "unexpected voxel count");
    std::sort(result.begin(), result.end(), [](const Point& first, const Point& second) {
        return first.x < second.x;
    });
    for (std::size_t i = 0; i < result.size(); ++i) {
        require(finite_point(result[i]), "nonfinite centroid");
        require(std::abs(result[i].x - (0.2f + static_cast<float>(i))) < 1e-6f &&
            std::abs(result[i].y - 0.2f) < 1e-6f &&
            std::abs(result[i].z - 0.2f) < 1e-6f, "incorrect voxel centroid");
        point_row(output, "filter", "centroid", i, result[i]);
    }
}

void registration(std::ostream& output, bool no_correspondence)
{
    auto source = pcl::make_shared<Cloud>();
    auto target = pcl::make_shared<Cloud>();
    constexpr float angle = 0.012f;
    const float cosine = std::cos(angle), sine = std::sin(angle);
    const float tx = no_correspondence ? 100.0f : 0.04f;
    for (int i = 0; i < 30; ++i) {
        const float x = (i % 5) * 0.8f + (i % 3) * 0.03f;
        const float y = ((i / 5) % 3) * 0.9f + (i % 2) * 0.07f;
        const float z = (i / 15) * 1.1f + (i % 7) * 0.04f;
        source->push_back(Point(x, y, z));
        // The expected target does not use PCL's transformation operation.
        target->push_back(Point(cosine * x - sine * y + tx,
            sine * x + cosine * y - 0.03f, z + 0.02f));
    }
    pcl::IterativeClosestPoint<Point, Point> icp;
    icp.setInputSource(source);
    icp.setInputTarget(target);
    icp.setMaxCorrespondenceDistance(0.2);
    icp.setMaximumIterations(80);
    icp.setTransformationEpsilon(1e-12);
    icp.setEuclideanFitnessEpsilon(1e-12);
    Cloud aligned;
    icp.align(aligned);
    if (no_correspondence) {
        require(!icp.hasConverged(), "unmatched clouds reported convergence");
        throw RejectedInput("no correspondences within 0.2 metres");
    }
    require(icp.hasConverged(), "ICP did not converge");
    const Eigen::Matrix4f transform = icp.getFinalTransformation();
    Eigen::Matrix4f expected = Eigen::Matrix4f::Identity();
    expected(0, 0) = cosine;
    expected(0, 1) = -sine;
    expected(1, 0) = sine;
    expected(1, 1) = cosine;
    expected(0, 3) = tx;
    expected(1, 3) = -0.03f;
    expected(2, 3) = 0.02f;
    require(transform.allFinite() &&
        (transform - expected).cwiseAbs().maxCoeff() < 1e-4f,
        "ICP transform differs from the known rigid motion");
    const double fitness = icp.getFitnessScore();
    require(std::isfinite(fitness) && fitness >= 0 && fitness < 1e-8,
        "ICP fitness is invalid");
    require(aligned.size() == target->size(), "unexpected ICP output count");
    for (std::size_t i = 0; i < aligned.size(); ++i) {
        require(finite_point(aligned[i]) &&
            (aligned[i].getVector3fMap() - (*target)[i].getVector3fMap()).norm() < 1e-4f,
            "ICP output differs from the known target point");
    }
    for (std::size_t i = 0; i < source->size(); ++i)
        point_row(output, "registration", "source", i, (*source)[i]);
    for (std::size_t i = 0; i < target->size(); ++i)
        point_row(output, "registration", "target", i, (*target)[i]);
    for (std::size_t i = 0; i < aligned.size(); ++i)
        point_row(output, "registration", "aligned", i, aligned[i]);
    for (int row = 0; row < 4; ++row) {
        output << "registration,matrix," << row;
        for (int column = 0; column < 4; ++column) output << ',' << transform(row, column);
        output << '\n';
    }
    output << "registration,fitness,0," << fitness << ",0,0,0\n";
}

void usage(std::ostream& output)
{
    output << "Usage: ember-pcl-cloud [--case filter|registration|empty-filter|no-correspondence] "
              "[--output FILE|-]\n";
}
} // namespace

int main(int argc, char** argv)
{
    std::signal(SIGPIPE, SIG_IGN);
    std::string test = "registration", path = "-";
    bool case_seen = false, output_seen = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" && argc == 2) {
            usage(std::cout);
            std::cout.flush();
            return std::cout ? 0 : 74;
        }
        if (argument == "--case" && !case_seen && i + 1 < argc) {
            test = argv[++i];
            case_seen = true;
        } else if (argument == "--output" && !output_seen && i + 1 < argc) {
            path = argv[++i];
            output_seen = true;
        } else {
            usage(std::cerr);
            return 64;
        }
    }
    if (path.empty() || (test != "filter" && test != "registration" &&
        test != "empty-filter" && test != "no-correspondence")) {
        usage(std::cerr);
        return 64;
    }
    try {
        require(EIGEN_MAJOR_VERSION == 5 && EIGEN_MINOR_VERSION == 0 &&
            EIGEN_PATCH_VERSION == 1, "wrong Eigen headers");
        std::ostringstream csv;
        csv.imbue(std::locale::classic());
        csv << std::setprecision(17) << "case,kind,index,a,b,c,d\n";
        if (test == "filter" || test == "empty-filter") filtering(csv, test == "empty-filter");
        else registration(csv, test == "no-correspondence");
        require(static_cast<bool>(csv), "CSV formatting failed");
        // Numerical acceptance completes before an output file can be changed.
        if (path == "-") {
            std::cout << csv.str();
            std::cout.flush();
            if (!std::cout) return 74;
        } else {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output) return 74;
            output << csv.str();
            output.flush();
            const bool written = static_cast<bool>(output);
            output.close();
            if (!written || output.fail()) return 74;
        }
        return 0;
    } catch (const RejectedInput& error) {
        std::cerr << "Rejected input: " << error.what() << '\n';
        return 65;
    } catch (const std::exception& error) {
        std::cerr << "Failure: " << error.what() << '\n';
        return 70;
    }
}
