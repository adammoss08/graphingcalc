#include "graph/ImplicitCpu.hpp"

#include <dispatch/dispatch.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace calc {

namespace {

constexpr float kNaNf = std::numeric_limits<float>::quiet_NaN();

struct Ctx {
    const Program* f;
    const ImplicitGrid* g;
    std::vector<double> field;
    std::vector<float>* hEdge;
    std::vector<float>* vEdge;
};

double worldX(const ImplicitGrid& g, double gx) { return g.centerX + (gx - g.graphW * 0.5) / g.scale; }
double worldY(const ImplicitGrid& g, double gy) { return g.centerY + (g.graphH * 0.5 - gy) / g.scale; }

// Mirrors refineCrossing() in implicit.metal (Illinois, 3 steps, pole rejection) in double.
float refine(const Program& f, double ax, double ay, double fa, double bx, double by, double fb) {
    double ref = std::max(std::fabs(fa), std::fabs(fb));
    double ta = 0, tb = 1, t = fa / (fa - fb), ft = 0;
    int side = 0;
    for (int i = 0; i < 3; ++i) {
        t = (ta * fb - tb * fa) / (fb - fa);
        if (!std::isfinite(t)) t = 0.5 * (ta + tb);
        ft = evaluate(f, ax + (bx - ax) * t, ay + (by - ay) * t);
        if (std::isnan(ft)) return kNaNf;
        if ((ft > 0) == (fa > 0)) { ta = t; fa = ft; if (side == -1) fb *= 0.5; side = -1; }
        else { tb = t; fb = ft; if (side == 1) fa *= 0.5; side = 1; }
    }
    return std::fabs(ft) <= 0.1 * ref + 1e-12 ? float(t) : kNaNf;
}

void fieldRow(void* p, size_t y) {
    Ctx& c = *static_cast<Ctx*>(p);
    const ImplicitGrid& g = *c.g;
    double wy = worldY(g, double(y) * g.cellPts);
    for (int x = 0; x < g.gridW; ++x) c.field[y * g.gridW + x] = evaluate(*c.f, worldX(g, double(x) * g.cellPts), wy);
}

void edgeRow(void* p, size_t y) {
    Ctx& c = *static_cast<Ctx*>(p);
    const ImplicitGrid& g = *c.g;
    for (int x = 0; x < g.gridW; ++x) {
        size_t i = y * g.gridW + x;
        double f0 = c.field[i];
        double ax = worldX(g, x * g.cellPts), ay = worldY(g, double(y) * g.cellPts);
        float h = kNaNf, v = kNaNf;
        if (x + 1 < g.gridW) {
            double f1 = c.field[i + 1];
            if (std::isfinite(f0) && std::isfinite(f1) && (f0 > 0) != (f1 > 0))
                h = refine(*c.f, ax, ay, f0, worldX(g, (x + 1) * g.cellPts), ay, f1);
        }
        if (int(y) + 1 < g.gridH) {
            double f1 = c.field[i + g.gridW];
            if (std::isfinite(f0) && std::isfinite(f1) && (f0 > 0) != (f1 > 0))
                v = refine(*c.f, ax, ay, f0, ax, worldY(g, double(y + 1) * g.cellPts), f1);
        }
        (*c.hEdge)[i] = h;
        (*c.vEdge)[i] = v;
    }
}

} // namespace

bool implicitNeedsDouble(const ImplicitGrid& g) {
    double mag = std::max(std::fabs(g.centerX), std::fabs(g.centerY)) + 0.5 * std::max(g.graphW, g.graphH) / g.scale;
    double ulp = mag * std::numeric_limits<float>::epsilon();
    double cellWorld = g.cellPts / g.scale;
    return cellWorld < 512 * ulp;  // fewer than ~9 bits of sub-cell resolution in float
}

void implicitFieldCpu(const Program& f, const ImplicitGrid& g, std::vector<float>& field,
                      std::vector<float>& hEdge, std::vector<float>& vEdge) {
    size_t n = size_t(g.gridW) * g.gridH;
    Ctx c{&f, &g, std::vector<double>(n), &hEdge, &vEdge};
    hEdge.assign(n, kNaNf);
    vEdge.assign(n, kNaNf);
    dispatch_queue_t q = dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0);
    dispatch_apply_f(g.gridH, q, &c, fieldRow);
    dispatch_apply_f(g.gridH, q, &c, edgeRow);
    field.resize(n);
    for (size_t i = 0; i < n; ++i) field[i] = float(c.field[i]);
}

} // namespace calc
