#pragma once
// Grid lines and axis labels, using Desmos's step selection (see docs/DESMOS_SPEC.md).
#include "graph/Viewport.hpp"

#include <string>
#include <vector>

namespace calc {

struct GridSteps {
    double majorX, minorX, majorY, minorY;
};

// pixelsPerLabel = 80 in Desmos. Square views share one step for both axes.
GridSteps computeGridSteps(const Viewport& vp, double pixelsPerLabel = 80);

struct AxisLabel {
    double value;
    std::string mantissa;   // e.g. "-2.5" or "1" (with exponent)
    int exponent = 0;       // nonzero => "mantissa × 10^exponent"
    bool scientific = false;
};

// Formats an axis value given the major step (decides decimals and scientific notation).
AxisLabel formatAxisLabel(double value, double step);

// Multiples of `step` within [lo, hi].
std::vector<double> gridValues(double lo, double hi, double step);

} // namespace calc
