#pragma once
// Adaptive CPU sampling (double precision) of explicit, parametric and polar curves.
// Output is in screen points relative to an anchor so a cached curve can be re-used while panning:
//   screen = (sx(anchorX), sy(anchorY)) + p
#include "graph/Viewport.hpp"
#include "math/Vm.hpp"

#include <cstdint>
#include <vector>

namespace calc {

struct CurvePoints {
    std::vector<float> xy;          // x0,y0,x1,y1,... ; NaN pair = pen up
    double anchorX = 0, anchorY = 0;
    double scale = 0;               // viewport scale at sampling time
    double domainLo = 0, domainHi = 0;
    double cullLo = 0, cullHi = 0;  // dependent-variable range refined at full detail (explicit)
    uint64_t hash = 0;              // GraphPiece program hash at sampling time
    bool viewIndependentDomain = false;  // parametric/polar: domain doesn't depend on the view
    double costMs = 0;              // how long the last sampling took (decides sync vs background)

    size_t count() const { return xy.size() / 2; }
    // Still valid for `vp`? (same zoom; explicit curves must also cover the visible range)
    bool covers(const Viewport& vp, bool alongY) const;
    // Explicit curves: visible range within a quarter view of the sampled domain's edge.
    bool nearEdge(const Viewport& vp, bool alongY) const {
        double lo = alongY ? vp.ymin() : vp.xmin(), hi = alongY ? vp.ymax() : vp.xmax();
        double q = 0.25 * (hi - lo);
        return lo - q < domainLo || hi + q > domainHi;
    }
};

// y = f(x) (alongY=false) or x = f(y) (alongY=true). Samples the visible range plus half a view of
// margin on each side, so small pans reuse the result. Segments entirely beyond the cull range
// (one view height above/below) are not refined: they are invisible.
void sampleExplicit(const Program& f, const Viewport& vp, bool alongY, CurvePoints& out);

// Pan without resampling: if `c` was sampled at this zoom, samples only the newly exposed domain
// and splices it on. Returns false if a full resample is needed instead.
bool extendExplicit(const Program& f, const Viewport& vp, bool alongY, CurvePoints& c);

// (fx(t), fy(t)) for t in [t0, t1]; polar: r = fx(θ), θ in [t0, t1].
void sampleParametric(const Program& fx, const Program* fy, double t0, double t1, bool polar,
                      const Viewport& vp, CurvePoints& out);

} // namespace calc
