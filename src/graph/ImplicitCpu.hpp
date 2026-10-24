#pragma once
// Double-precision implicit-curve field for deep zoom. Same grid and outputs as the GPU kernels
// implicitField + implicitEdges (shaders/implicit.metal), so the GPU contour kernel runs unchanged.
#include "math/Vm.hpp"

#include <vector>

namespace calc {

struct ImplicitGrid {
    double centerX, centerY, scale;   // world centre, points per unit
    double graphW, graphH;            // points
    double cellPts;
    int gridW, gridH;
};

// True when float coordinates can't resolve a grid cell (|centre| too large for the zoom).
bool implicitNeedsDouble(const ImplicitGrid& g);

// Fills field[gridW*gridH], hEdge[gridW*gridH] (crossing t on the edge to the right, NaN if none)
// and vEdge (edge below), row-major. Runs rows in parallel.
void implicitFieldCpu(const Program& f, const ImplicitGrid& g, std::vector<float>& field,
                      std::vector<float>& hEdge, std::vector<float>& vEdge);

} // namespace calc
