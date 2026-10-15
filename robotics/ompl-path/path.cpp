// SPDX-License-Identifier: MIT
#include "geometry.h"

#include <ompl/base/ScopedState.h>
#include <ompl/base/spaces/RealVectorStateSpace.h>
#include <ompl/base/terminationconditions/IterationTerminationCondition.h>
#include <ompl/geometric/SimpleSetup.h>
#include <ompl/geometric/planners/rrt/RRTConnect.h>
#include <ompl/util/Console.h>
#include <ompl/util/RandomNumbers.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace ob = ompl::base;
namespace og = ompl::geometric;
using geometry::Point;
using geometry::Rectangle;

enum class Case { path, blocked, invalid_goal };
struct Options {
    Case scenario = Case::path;
    std::string output = "-";
    bool help = false;
};

static Options parse_options(int argc, char **argv)
{
    Options options;
    bool have_case = false, have_output = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" && argc == 2) {
            options.help = true;
        } else if (argument == "--case" && !have_case && index + 1 < argc) {
            have_case = true;
            const std::string value = argv[++index];
            if (value == "path")
                options.scenario = Case::path;
            else if (value == "blocked")
                options.scenario = Case::blocked;
            else if (value == "invalid-goal")
                options.scenario = Case::invalid_goal;
            else
                throw std::invalid_argument("Unknown planning case: " + value);
        } else if (argument == "--output" && !have_output && index + 1 < argc) {
            have_output = true;
            options.output = argv[++index];
            if (options.output.empty())
                throw std::invalid_argument("Output path is empty");
        } else {
            throw std::invalid_argument("Unknown, repeated or incomplete option: " + argument);
        }
    }
    return options;
}

class StderrHandler final : public ompl::msg::OutputHandler {
public:
    void log(const std::string &text, ompl::msg::LogLevel,
             const char *, int) override
    {
        std::fprintf(stderr, "OMPL: %s\n", text.c_str());
    }
};

// Keep the output handler alive until all OMPL objects have been destroyed.
class ScopedHandler {
public:
    ScopedHandler() : previous_(ompl::msg::getOutputHandler())
    {
        ompl::msg::useOutputHandler(&handler_);
    }
    ~ScopedHandler() { ompl::msg::useOutputHandler(previous_); }
    ScopedHandler(const ScopedHandler &) = delete;
    ScopedHandler &operator=(const ScopedHandler &) = delete;
private:
    StderrHandler handler_;
    ompl::msg::OutputHandler *previous_;
};

static Point point(const ob::State *state)
{
    const auto *value = state->as<ob::RealVectorStateSpace::StateType>();
    return {value->values[0], value->values[1]};
}

static double validate_path(const og::PathGeometric &path, Rectangle obstacle)
{
    const auto require = [](bool valid, const char *message) {
        if (!valid)
            throw std::runtime_error(message);
    };
    require(path.getStateCount() >= 2, "Path has fewer than two states");
    const Point first = point(path.getState(0));
    const Point last = point(path.getState(path.getStateCount() - 1));
    require(std::hypot(first.x - .1, first.y - .5) < 1e-8, "Path missed the start");
    require(std::hypot(last.x - .9, last.y - .5) < 1e-8, "Path missed the goal");
    double length = 0;
    for (std::size_t index = 0; index < path.getStateCount(); ++index) {
        const Point current = point(path.getState(index));
        require(geometry::bounded(current) && !geometry::inside(current, obstacle),
                "Path contains an invalid state");
        if (index != 0) {
            const Point previous = point(path.getState(index - 1));
            require(!geometry::segment_hits_rectangle(previous, current, obstacle),
                    "Continuous path segment intersects the closed obstacle");
            length += std::hypot(current.x - previous.x, current.y - previous.y);
        }
    }
    require(std::isfinite(length) && std::abs(length - path.length()) < 1e-8,
            "Path length differs from the independent calculation");
    require(length > .8 && length < 5, "Unexpected detour length");
    return length;
}

static bool write_path(const og::PathGeometric &path, const std::string &name)
{
    FILE *output = name == "-" ? stdout : std::fopen(name.c_str(), "w");
    if (output == nullptr) {
        std::fprintf(stderr, "Cannot open output %s: %s\n", name.c_str(), std::strerror(errno));
        return false;
    }
    int failure = 0;
    if (std::fprintf(output, "x,y\n") < 0)
        failure = errno == 0 ? EIO : errno;
    for (std::size_t index = 0; failure == 0 && index < path.getStateCount(); ++index) {
        const Point current = point(path.getState(index));
        if (std::fprintf(output, "%.17g,%.17g\n", current.x, current.y) < 0)
            failure = errno == 0 ? EIO : errno;
    }
    if (std::fflush(output) != 0 && failure == 0)
        failure = errno == 0 ? EIO : errno;
    if (std::ferror(output) != 0 && failure == 0)
        failure = EIO;
    if (std::fclose(output) != 0 && failure == 0)
        failure = errno == 0 ? EIO : errno;
    if (failure != 0)
        std::fprintf(stderr, "Output failed for %s: %s\n", name.c_str(), std::strerror(failure));
    return failure == 0;
}

int main(int argc, char **argv)
{
    try {
        const Options options = parse_options(argc, argv);
        if (options.help) {
            std::puts("Usage: ember-ompl-path [--case path|blocked|invalid-goal] [--output FILE|-]");
            return std::fflush(stdout) == 0 ? 0 : 4;
        }
        ScopedHandler handler;
        ompl::RNG::setSeed(104729);
        const bool blocked = options.scenario == Case::blocked;
        const Rectangle obstacle = blocked ? Rectangle{.45, .55, 0, 1} :
                                              Rectangle{.4, .6, .15, .85};
        auto space = std::make_shared<ob::RealVectorStateSpace>(2);
        ob::RealVectorBounds bounds(2);
        bounds.setLow(0);
        bounds.setHigh(1);
        space->setBounds(bounds);
        og::SimpleSetup setup(space);
        setup.setStateValidityChecker([obstacle](const ob::State *state) {
            const Point value = point(state);
            return geometry::bounded(value) && !geometry::inside(value, obstacle);
        });
        setup.getSpaceInformation()->setStateValidityCheckingResolution(.0005);
        ob::ScopedState<> start(space), goal(space);
        start[0] = .1;
        start[1] = .5;
        goal[0] = options.scenario == Case::invalid_goal ? .5 : .9;
        goal[1] = .5;
        setup.setStartAndGoalStates(start, goal, 1e-9);
        auto planner = std::make_shared<og::RRTConnect>(setup.getSpaceInformation());
        planner->setRange(.07);
        setup.setPlanner(planner);
        const unsigned int budget = blocked ? 4000 :
            options.scenario == Case::invalid_goal ? 256 : 20000;
        ob::IterationTerminationCondition iterations(budget);
        const auto status = setup.solve(iterations);
        if (options.scenario != Case::path) {
            if (setup.haveExactSolutionPath() ||
                (options.scenario == Case::invalid_goal && status != ob::PlannerStatus::INVALID_GOAL) ||
                (blocked && status != ob::PlannerStatus::TIMEOUT &&
                 status != ob::PlannerStatus::APPROXIMATE_SOLUTION))
                throw std::runtime_error("Planner accepted an invalid or blocked goal");
            std::fprintf(stderr, "No exact path: %s (seed=104729, budget=%u)\n",
                         status.asString().c_str(), budget);
            return 3;
        }
        if (status != ob::PlannerStatus::EXACT_SOLUTION || !setup.haveExactSolutionPath()) {
            std::fprintf(stderr, "No exact path: %s (seed=104729, budget=%u)\n",
                         status.asString().c_str(), budget);
            return 3;
        }
        const auto &path = setup.getSolutionPath();
        const double length = validate_path(path, obstacle);
        std::fprintf(stderr, "Exact path: states=%zu length=%.12f seed=104729 budget=%u\n",
                     path.getStateCount(), length, budget);
        return write_path(path, options.output) ? 0 : 4;
    } catch (const std::invalid_argument &error) {
        std::fprintf(stderr, "Invalid arguments: %s\n", error.what());
        return 2;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Planning failed: %s\n", error.what());
        return 1;
    }
}
