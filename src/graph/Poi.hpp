#pragma once
// Points of interest for explicit curves: zeros, extrema, y-intercept, intersections.
#include "graph/Viewport.hpp"
#include "math/Vm.hpp"

#include <vector>

namespace calc {

struct Poi {
    enum Type : uint8_t { Zero, Extremum, Intercept, Intersection };
    double x, y;
    Type type;
};

// POIs of y = f(x) visible in `vp`. `others` are other explicit curves to intersect with.
void findPois(const Program& f, const std::vector<const Program*>& others, const Viewport& vp,
              std::vector<Poi>& out);

// Rounds a coordinate for display the way Desmos labels do: enough decimals for the zoom level.
double roundForDisplay(double v, double scale);

} // namespace calc
