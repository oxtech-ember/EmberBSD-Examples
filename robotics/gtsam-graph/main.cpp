// SPDX-License-Identifier: MIT
// AI-assisted implementation. See README.md for provenance and validation limits.
#include <gtsam/geometry/Pose2.h>
#include <gtsam/linear/GaussianFactorGraph.h>
#include <gtsam/linear/JacobianFactor.h>
#include <gtsam/linear/linearExceptions.h>
#include <gtsam/nonlinear/LevenbergMarquardtOptimizer.h>
#include <gtsam/nonlinear/Marginals.h>
#include <gtsam/slam/BetweenFactor.h>
#include <gtsam/slam/PriorFactor.h>

#include <array>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
class GraphError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
class OutputError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
struct Options {
    std::string scenario = "pose";
    std::string output = "-";
    bool help = false;
};

Options parse(int argc, char** argv)
{
    Options result;
    bool has_case = false;
    bool has_output = false;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--help" && argc == 2) {
            result.help = true;
            return result;
        }
        if (option != "--case" && option != "--output")
            throw UsageError("unknown option: " + option);
        if (i + 1 >= argc)
            throw UsageError("missing value for " + option);
        const std::string value = argv[++i];
        if (option == "--case") {
            if (has_case) throw UsageError("duplicate --case");
            has_case = true;
            result.scenario = value;
        } else {
            if (has_output) throw UsageError("duplicate --output");
            has_output = true;
            result.output = value;
        }
    }
    if (result.scenario != "linear" && result.scenario != "pose" &&
        result.scenario != "underconstrained" && result.scenario != "missing-state")
        throw UsageError("unknown graph case: " + result.scenario);
    if (result.output.empty()) throw UsageError("empty output path");
    return result;
}

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

bool near(double actual, double expected, double tolerance)
{
    return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}

void header(std::ostream& output)
{
    output << "case,stage,key,x,y,theta,cost,cov_xx,cov_xy,cov_xt,cov_yy,cov_yt,cov_tt\n";
}

void linear(std::ostream& output, bool underconstrained)
{
    using namespace gtsam;
    GaussianFactorGraph graph;
    const Matrix identity = Matrix::Identity(1, 1);
    const auto noise = noiseModel::Isotropic::Sigma(1, 1.0);
    graph.emplace_shared<JacobianFactor>(0, -identity, 1, identity, Vector1(2.0), noise);
    if (underconstrained) {
        try {
            (void)graph.optimize();
        } catch (const IndeterminateSystemException&) {
            throw GraphError("underconstrained graph has no absolute position anchor");
        }
        throw std::runtime_error("underconstrained graph unexpectedly produced a solution");
    }
    graph.emplace_shared<JacobianFactor>(0, identity, Vector1(0.0), noise);
    graph.emplace_shared<JacobianFactor>(1, identity, Vector1(3.0), noise);
    VectorValues initial;
    initial.insert(0, Vector1(-1.0));
    initial.insert(1, Vector1(4.0));
    const VectorValues result = graph.optimize();
    const double before = graph.error(initial);
    const double after = graph.error(result);
    // Solve the normal equations by hand: [[2,-1],[-1,2]] x = [-2,5].
    // This is independent of the GTSAM optimization result.
    const std::array<double, 2> expected = {1.0 / 3.0, 8.0 / 3.0};
    require(near(before, 11.0 / 2.0, 1e-10), "unexpected initial linear cost");
    require(near(after, 1.0 / 6.0, 1e-10), "unexpected optimized linear cost");
    for (Key key = 0; key < expected.size(); ++key)
        require(near(result.at(key)(0), expected[key], 1e-10), "linear solution differs from analytic result");
    header(output);
    for (Key key = 0; key < expected.size(); ++key)
        output << "linear,initial," << key << ',' << initial.at(key)(0)
               << ",,," << before << ",,,,,,\n";
    for (Key key = 0; key < expected.size(); ++key)
        output << "linear,optimized," << key << ',' << result.at(key)(0)
               << ",,," << after << ",,,,,,\n";
}

void pose(std::ostream& output, bool missing_state)
{
    using namespace gtsam;
    const double pi = std::acos(-1.0);
    const auto noise = noiseModel::Isotropic::Sigma(3, 0.1);
    NonlinearFactorGraph graph;
    graph.emplace_shared<PriorFactor<Pose2>>(0, Pose2(0, 0, 0), noise);
    graph.emplace_shared<BetweenFactor<Pose2>>(0, 1, Pose2(2, 0, 0), noise);
    graph.emplace_shared<BetweenFactor<Pose2>>(1, 2, Pose2(0, 2, pi / 2), noise);
    graph.emplace_shared<BetweenFactor<Pose2>>(0, 2, Pose2(2, 2, pi / 2), noise);
    Values initial;
    initial.insert(0, Pose2(0.1, -0.2, 0.03));
    initial.insert(1, Pose2(2.2, 0.1, -0.04));
    if (missing_state) {
        try {
            (void)graph.error(initial);
        } catch (const ValuesKeyDoesNotExist&) {
            throw GraphError("factor references pose 2, which has no initial state");
        }
        throw std::runtime_error("missing pose unexpectedly passed graph evaluation");
    }
    initial.insert(2, Pose2(1.9, 2.2, pi / 2 + 0.05));
    const double before = graph.error(initial);
    LevenbergMarquardtParams params;
    params.setMaxIterations(100);
    params.setRelativeErrorTol(1e-12);
    params.setAbsoluteErrorTol(1e-12);
    const Values result = LevenbergMarquardtOptimizer(graph, initial, params).optimize();
    const double after = graph.error(result);
    // Independent poses satisfy the prior and all three relative motions.
    const std::array<std::array<double, 3>, 3> expected = {{{0, 0, 0}, {2, 0, 0}, {2, 2, pi / 2}}};
    require(std::isfinite(before) && before > 1.0, "invalid initial pose cost");
    require(std::isfinite(after) && after >= 0.0 && after < 1e-12, "pose graph did not reach the known solution");
    const Marginals marginals(graph, result);
    std::array<Matrix, 3> covariances;
    for (Key key = 0; key < expected.size(); ++key) {
        const auto actual = result.at<Pose2>(key);
        require(near(actual.x(), expected[key][0], 1e-7), "pose x differs from known solution");
        require(near(actual.y(), expected[key][1], 1e-7), "pose y differs from known solution");
        require(near(actual.theta(), expected[key][2], 1e-7), "pose angle differs from known solution");
        Matrix& covariance = covariances[key];
        covariance = marginals.marginalCovariance(key);
        require(covariance.rows() == 3 && covariance.cols() == 3, "unexpected covariance dimensions");
        for (int row = 0; row < 3; ++row) {
            require(covariance(row, row) > 0.0, "nonpositive covariance diagonal");
            for (int column = 0; column < 3; ++column) {
                require(std::isfinite(covariance(row, column)), "nonfinite pose covariance");
                require(near(covariance(row, column), covariance(column, row), 1e-10), "asymmetric pose covariance");
                if (key == 0)
                    require(near(covariance(row, column), row == column ? 0.01 : 0.0, 1e-8),
                            "anchor covariance differs from the prior");
            }
        }
    }
    header(output);
    for (Key key = 0; key < expected.size(); ++key) {
        const auto value = initial.at<Pose2>(key);
        output << "pose,initial," << key << ',' << value.x() << ',' << value.y() << ',' << value.theta()
               << ',' << before << ",,,,,,\n";
    }
    for (Key key = 0; key < expected.size(); ++key) {
        const auto value = result.at<Pose2>(key);
        const Matrix& covariance = covariances[key];
        output << "pose,optimized," << key << ',' << value.x() << ',' << value.y() << ',' << value.theta()
               << ',' << after << ',' << covariance(0, 0) << ',' << covariance(0, 1)
               << ',' << covariance(0, 2) << ',' << covariance(1, 1) << ',' << covariance(1, 2)
               << ',' << covariance(2, 2) << '\n';
    }
}

void write_output(const std::string& path, const std::string& data)
{
    FILE* const file = path == "-" ? stdout : std::fopen(path.c_str(), "wb");
    if (!file) throw OutputError("cannot open output: " + std::string(std::strerror(errno)));
    bool failed = std::fwrite(data.data(), 1, data.size(), file) != data.size();
    if (std::fflush(file) != 0 || std::ferror(file)) failed = true;
    if (file != stdout && std::fclose(file) != 0) failed = true;
    if (failed) throw OutputError("cannot write, flush or close output");
}
} // namespace

int main(int argc, char** argv)
{
    try {
        const Options options = parse(argc, argv);
        // A closed output pipe must report an output error, rather than SIGPIPE.
        if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR)
            throw std::runtime_error("cannot configure output-pipe error handling");
        if (options.help) {
            write_output("-", "Usage: ember-gtsam-graph [--case linear|pose|underconstrained|missing-state] [--output FILE|-]\n");
            return 0;
        }
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << std::setprecision(17);
        if (options.scenario == "linear" || options.scenario == "underconstrained")
            linear(output, options.scenario == "underconstrained");
        else
            pose(output, options.scenario == "missing-state");
        // A rejected graph must not create or truncate a file.
        write_output(options.output, output.str());
        return 0;
    } catch (const UsageError& error) {
        std::cerr << "Usage error: " << error.what() << '\n';
        return 64;
    } catch (const GraphError& error) {
        std::cerr << "Graph error: " << error.what() << '\n';
        return 65;
    } catch (const OutputError& error) {
        std::cerr << "Output error: " << error.what() << '\n';
        return 74;
    } catch (const std::exception& error) {
        std::cerr << "Optimization error: " << error.what() << '\n';
        return 70;
    }
}
