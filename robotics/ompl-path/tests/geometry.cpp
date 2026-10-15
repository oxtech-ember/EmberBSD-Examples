// SPDX-License-Identifier: MIT
#include "../geometry.h"

#include <cstdio>
#include <limits>

int main()
{
    using namespace geometry;
    const Rectangle rectangle{.4, .6, .2, .8};
    struct Case { Point a, b; bool collision; };
    const Case cases[] = {
        {{0, .5}, {1, .5}, true}, {{.5, 0}, {.5, 1}, true},
        {{0, 0}, {1, 1}, true}, {{0, .8}, {1, .8}, true},
        {{.4, 0}, {.4, 1}, true}, {{.5, .5}, {.5, .5}, true},
        {{.4, .2}, {.4, .2}, true}, {{0, 0}, {0, 0}, false},
        {{0, .8001}, {1, .8001}, false}, {{.1, 0}, {.1, 1}, false},
        {{0, 0}, {.3, .3}, false}, {{0, 1}, {.4, .8}, true},
        // Exact decimal corner tangency previously escaped slab clipping
        // because the two division results rounded to opposite sides.
        {{.399, .204}, {.401, .196}, true}
    };
    for (const auto &test : cases) {
        if (segment_hits_rectangle(test.a, test.b, rectangle) != test.collision ||
            segment_hits_rectangle(test.b, test.a, rectangle) != test.collision) {
            std::fputs("Continuous rectangle geometry failed\n", stderr);
            return 1;
        }
    }
    if (bounded({std::numeric_limits<double>::quiet_NaN(), .5}) ||
        bounded({.5, std::numeric_limits<double>::infinity()}) ||
        bounded({.5, 1.001}) || !bounded({0, 1}))
        return 1;
    std::puts("Continuous geometry: crossing, tangent, reverse and degenerate cases passed");
    return 0;
}
