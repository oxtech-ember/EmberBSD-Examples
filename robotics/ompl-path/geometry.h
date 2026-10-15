// SPDX-License-Identifier: MIT
#ifndef EMBER_OMPL_PATH_GEOMETRY_H
#define EMBER_OMPL_PATH_GEOMETRY_H

#include <algorithm>
#include <cmath>
#include <limits>

namespace geometry {
struct Point { double x, y; };
struct Rectangle { double left, right, bottom, top; };

inline bool bounded(Point p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) &&
        p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y <= 1;
}

inline bool inside(Point p, Rectangle rectangle)
{
    return p.x >= rectangle.left && p.x <= rectangle.right &&
        p.y >= rectangle.bottom && p.y <= rectangle.top;
}

// Clip the complete closed segment against both rectangle slabs. This oracle
// does not use OMPL's sampled motion checker. Tangency counts as collision.
// Expand by a roundoff guard in the unit square. A nearly tangent segment
// must be rejected conservatively, rather than accepted after division error.
inline bool segment_hits_rectangle(Point a, Point b, Rectangle rectangle)
{
    double enter = 0, leave = 1;
    constexpr double guard = 32 * std::numeric_limits<double>::epsilon();
    const double origin[] = {a.x, a.y}, end[] = {b.x, b.y};
    const double low[] = {rectangle.left - guard, rectangle.bottom - guard};
    const double high[] = {rectangle.right + guard, rectangle.top + guard};
    for (int axis = 0; axis < 2; ++axis) {
        const double direction = end[axis] - origin[axis];
        if (direction == 0) {
            if (origin[axis] < low[axis] || origin[axis] > high[axis])
                return false;
        } else {
            double near = (low[axis] - origin[axis]) / direction;
            double far = (high[axis] - origin[axis]) / direction;
            if (near > far)
                std::swap(near, far);
            enter = std::max(enter, near);
            leave = std::min(leave, far);
            if (enter > leave)
                return false;
        }
    }
    return true;
}
} // namespace geometry

#endif
