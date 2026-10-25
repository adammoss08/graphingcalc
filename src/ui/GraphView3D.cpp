// GraphView in 3D mode: builds the Scene3D (surfaces, space curves, points, box, floor grid, axes)
// and draws the projected axis letters and tick numbers.
#include "ui/GraphView.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_set>

namespace calc {

using namespace theme;

namespace {

uint64_t mix3(uint64_t h, double a, double b, double c) {
    uint64_t u[3];
    std::memcpy(&u[0], &a, 8);
    std::memcpy(&u[1], &b, 8);
    std::memcpy(&u[2], &c, 8);
    return h ^ (u[0] * 0x9e3779b97f4a7c15ull) ^ (u[1] * 0xc2b2ae3d27d4eb4full) ^ (u[2] * 0x165667b19e3779f9ull);
}

simd_float4 rgba(Color c, float a) { return {c.r, c.g, c.b, a}; }

Line3D line3(double ax, double ay, double az, double bx, double by, double bz, float width, simd_float4 color) {
    Line3D l{};
    l.a = {float(ax), float(ay), float(az), width};
    l.b = {float(bx), float(by), float(bz), 0};
    l.color = color;
    return l;
}

// Samples a curve into world points (independent of the camera). Explicit curves cover [-L, L].
void sampleCurve3D(const GraphPiece& pc, GraphKind kind, double tMin, double tMax, double L, std::vector<simd_float3>& out) {
    int n = int(Curve3DSamples);
    std::vector<double> u(n + 1), a(n + 1), b(n + 1), c(n + 1);
    bool explicitKind = kind == GraphKind::ExplicitY || kind == GraphKind::ExplicitX;
    double u0 = explicitKind ? -L : tMin, u1 = explicitKind ? L : tMax;
    for (int i = 0; i <= n; ++i) u[i] = u0 + (u1 - u0) * i / n;
    out.resize(n + 1);
    switch (kind) {
    case GraphKind::ExplicitY:
        evaluateBatch(pc.f, n + 1, u.data(), nullptr, nullptr, a.data());
        for (int i = 0; i <= n; ++i) out[i] = simd_make_float3(float(u[i]), float(a[i]), 0);
        break;
    case GraphKind::ExplicitX:
        evaluateBatch(pc.f, n + 1, nullptr, u.data(), nullptr, a.data());
        for (int i = 0; i <= n; ++i) out[i] = simd_make_float3(float(a[i]), float(u[i]), 0);
        break;
    case GraphKind::Polar:
        evaluateBatch(pc.f, n + 1, nullptr, nullptr, u.data(), a.data());
        for (int i = 0; i <= n; ++i)
            out[i] = simd_make_float3(float(a[i] * std::cos(u[i])), float(a[i] * std::sin(u[i])), 0);
        break;
    case GraphKind::Parametric:
        evaluateBatch(pc.f, n + 1, nullptr, nullptr, u.data(), a.data());
        evaluateBatch(pc.g, n + 1, nullptr, nullptr, u.data(), b.data());
        for (int i = 0; i <= n; ++i) out[i] = simd_make_float3(float(a[i]), float(b[i]), 0);
        break;
    case GraphKind::Parametric3D:
        evaluateBatch(pc.f, n + 1, nullptr, nullptr, u.data(), a.data());
        evaluateBatch(pc.g, n + 1, nullptr, nullptr, u.data(), b.data());
        evaluateBatch(pc.h, n + 1, nullptr, nullptr, u.data(), c.data());
        for (int i = 0; i <= n; ++i) out[i] = simd_make_float3(float(a[i]), float(b[i]), float(c[i]));
        break;
    default:
        out.clear();
        break;
    }
    for (auto& p : out)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) p.x = NAN;
}

bool inBox(simd_float3 p, float L) {
    float k = L * 1.0001f;
    return std::fabs(p.x) <= k && std::fabs(p.y) <= k && std::fabs(p.z) <= k;
}

// Where to grid an implicit surface F(x, y, z) = 0 inside the box [-L, L]^3: a coarse probe in
// double finds where F is defined (restrictions, sqrt of negatives...) and, within that, where it
// changes sign. The GPU grid then spends its resolution there. Thin features smaller than a probe
// cell are still found by the fine grid when the bounds fall back to the defined region.
void implicitBounds(const Program& f, double L, bool& any, simd_float3& lo, simd_float3& hi) {
    const int n = theme::Implicit3DProbeN;
    const size_t total = size_t(n) * n * n;
    std::vector<double> xs(total), ys(total), zs(total), out(total);
    double step = 2 * L / (n - 1);
    for (int k = 0, i = 0; k < n; ++k)
        for (int j = 0; j < n; ++j)
            for (int q = 0; q < n; ++q, ++i) {
                xs[i] = -L + q * step;
                ys[i] = -L + j * step;
                zs[i] = -L + k * step;
            }
    evaluateBatch(f, int(total), xs.data(), ys.data(), zs.data(), out.data());
    auto at = [&](int q, int j, int k) { return out[(size_t(k) * n + j) * n + q]; };
    int dLo[3] = {n, n, n}, dHi[3] = {-1, -1, -1}, sLo[3] = {n, n, n}, sHi[3] = {-1, -1, -1};
    for (int k = 0; k < n; ++k)
        for (int j = 0; j < n; ++j)
            for (int q = 0; q < n; ++q) {
                double v = at(q, j, k);
                if (!std::isfinite(v)) continue;
                int c[3] = {q, j, k};
                for (int a = 0; a < 3; ++a) { dLo[a] = std::min(dLo[a], c[a]); dHi[a] = std::max(dHi[a], c[a]); }
                // sign change towards the +x/+y/+z neighbours
                bool change = false;
                if (q + 1 < n) { double w = at(q + 1, j, k); change |= std::isfinite(w) && (v >= 0) != (w >= 0); }
                if (j + 1 < n) { double w = at(q, j + 1, k); change |= std::isfinite(w) && (v >= 0) != (w >= 0); }
                if (k + 1 < n) { double w = at(q, j, k + 1); change |= std::isfinite(w) && (v >= 0) != (w >= 0); }
                if (change)
                    for (int a = 0; a < 3; ++a) { sLo[a] = std::min(sLo[a], c[a]); sHi[a] = std::max(sHi[a], c[a] + 1); }
            }
    any = dHi[0] >= 0;
    if (!any) return;
    bool useSign = sHi[0] >= 0;
    // Where the surface reaches the box, grid two fine cells past it: the renderer clips at the box
    // faces exactly (clip planes), so edges there are straight, not a cell staircase.
    double over = 2 * 2 * L / (theme::Implicit3DGridN - 1);
    for (int a = 0; a < 3; ++a) {
        int l = useSign ? sLo[a] : dLo[a], h = useSign ? sHi[a] : dHi[a];
        // One probe cell of margin: features between probe samples near the edge still fit.
        double lw = -L + (l - 1) * step, hw = -L + (h + 1) * step;
        lo[a] = float(lw <= -L ? -L - over : lw);
        hi[a] = float(hw >= L ? L + over : hw);
    }
}

std::string tickText(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    std::string s = buf;
    if (!s.empty() && s[0] == '-') s.replace(0, 1, "−");
    return s;
}

} // namespace

double GraphView::gridStep3D() const {
    // Desmos 1-2-5 steps, about ten intervals across the box.
    double raw = 2 * cam.halfSize / 10;
    double p = std::pow(10.0, std::floor(std::log10(raw)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (m * p >= raw * 0.999) return m * p;
    return 10 * p;
}

void GraphView::buildFrame3D(const std::vector<GraphItem>& items, int selected, GraphFrame& out) {
    out.threeD = true;
    Scene3D& sc = out.scene;
    double L = cam.halfSize;
    float Lf = float(L);
    sc.viewProj = cam.matrix(area.w / std::max(area.h, 1.0f));
    sc.eye = cam.eye() * Lf;
    sc.halfSize = Lf;
    sc.gridStep = float(gridStep3D());
    sc.surfaces.clear();
    sc.implicits.clear();
    sc.lines.clear();

    // Box edges, floor grid (z = 0), axes.
    simd_float4 box = rgba(Ink, Box3DOpacity);
    for (int k = 0; k < 4; ++k) {
        double sx = (k & 1) ? L : -L, sy = (k & 2) ? L : -L;
        sc.lines.push_back(line3(sx, sy, -L, sx, sy, L, Box3DWidth, box));
        sc.lines.push_back(line3(-L, sx, sy, L, sx, sy, Box3DWidth, box));
        sc.lines.push_back(line3(sx, -L, sy, sx, L, sy, Box3DWidth, box));
    }
    if (settings.grid) {
        double step = sc.gridStep;
        simd_float4 grid = rgba(Ink, Floor3DGridOpacity);
        for (double v = std::ceil(-L / step) * step; v <= L * 1.0001; v += step) {
            if (std::fabs(v) < step * 1e-6) continue;  // the axes
            sc.lines.push_back(line3(v, -L, 0, v, L, 0, Floor3DGridWidth, grid));
            sc.lines.push_back(line3(-L, v, 0, L, v, 0, Floor3DGridWidth, grid));
        }
    }
    simd_float4 axis = rgba(Ink, Axis3DOpacity);
    if (settings.xAxis) sc.lines.push_back(line3(-L, 0, 0, L, 0, 0, Axis3DWidth, axis));
    if (settings.yAxis) sc.lines.push_back(line3(0, -L, 0, 0, L, 0, Axis3DWidth, axis));
    sc.lines.push_back(line3(0, 0, -L, 0, 0, L, Axis3DWidth, axis));

    std::unordered_set<uint64_t> used;
    for (const GraphItem& it : items) {
        const RowAnalysis& an = *it.an;
        const Style& st = it.style;
        if (st.hidden) continue;
        bool hl = it.row == selected || it.row == m_hoverRow;
        float width = st.lineWidth + (hl ? CurveWidthHighlight - CurveWidth : 0);
        simd_float4 color = rgba(st.color, hl ? CurveOpacityHighlight : st.lineOpacity);
        for (size_t p = 0; p < an.pieces.size(); ++p) {
            const GraphPiece& pc = an.pieces[p];
            switch (an.graph) {
            case GraphKind::Surface:
                sc.surfaces.push_back({&pc.f, st.color, an.rel == Cmp::Eq ? Surface3DOpacity : Inequality3DOpacity});
                break;
            case GraphKind::ParametricSurface: {
                SurfaceDraw3D sd{&pc.f, st.color, Surface3DOpacity};
                sd.g = &pc.g;
                sd.h = &pc.h;
                sc.surfaces.push_back(sd);
                break;
            }
            case GraphKind::Implicit3D: {
                uint64_t k = it.key + p;
                used.insert(k);
                ImplicitBox3D& bx = m_box3d[k];
                uint64_t h = mix3(pc.f.hash(), L, 0, 0);
                if (bx.hash != h) {
                    implicitBounds(pc.f, L, bx.any, bx.lo, bx.hi);
                    bx.hash = h;
                }
                if (!bx.any) break;
                float opacity = an.rel == Cmp::Eq ? Surface3DOpacity : Inequality3DOpacity;
                sc.implicits.push_back({&pc.f, st.color, opacity, bx.lo, bx.hi});
                break;
            }
            case GraphKind::Point3D: case GraphKind::Point: {
                if (!st.showPoints) break;
                simd_float3 q = simd_make_float3(float(pc.px), float(pc.py), float(pc.pz));
                if (!std::isfinite(q.x + q.y + q.z) || !inBox(q, Lf)) break;
                float d = (an.graph == GraphKind::Point ? st.pointSize : Point3DDiameter) * (hl ? 1.25f : 1.0f);
                sc.lines.push_back(line3(q.x, q.y, q.z, q.x, q.y, q.z, d, rgba(st.color, 1)));
                break;
            }
            case GraphKind::ExplicitY: case GraphKind::ExplicitX: case GraphKind::Polar:
            case GraphKind::Parametric: case GraphKind::Parametric3D: {
                if (an.rel != Cmp::Eq) break;
                uint64_t k = it.key + p;
                used.insert(k);
                bool explicitKind = an.graph == GraphKind::ExplicitY || an.graph == GraphKind::ExplicitX;
                uint64_t h = pc.f.hash() ^ (pc.g.hash() * 31) ^ (pc.h.hash() * 131) ^ uint64_t(an.graph);
                h = mix3(h, explicitKind ? L : an.tMin, explicitKind ? 0 : an.tMax, 0);
                Curve3D& c = m_cache3d[k];
                if (c.hash != h || c.pts.empty()) {
                    sampleCurve3D(pc, an.graph, an.tMin, an.tMax, L, c.pts);
                    c.hash = h;
                }
                for (size_t i = 1; i < c.pts.size(); ++i) {
                    simd_float3 a = c.pts[i - 1], b = c.pts[i];
                    if (std::isnan(a.x) || std::isnan(b.x) || !inBox(a, Lf) || !inBox(b, Lf)) continue;
                    sc.lines.push_back(line3(a.x, a.y, a.z, b.x, b.y, b.z, width, color));
                }
                break;
            }
            default:
                break;
            }
        }
    }
    for (auto it = m_cache3d.begin(); it != m_cache3d.end();) {
        if (!used.count(it->first)) it = m_cache3d.erase(it);
        else ++it;
    }
    for (auto it = m_box3d.begin(); it != m_box3d.end();) {
        if (!used.count(it->first)) it = m_box3d.erase(it);
        else ++it;
    }
}

void GraphView::drawOver3D(Batch2D& b, const std::vector<GraphItem>& items) const {
    b.pushClip(area);
    double L = cam.halfSize;
    TextAtlas& atlas = b.atlas();
    auto put = [&](double x, double y, double z, const std::string& s, float size, Color c) {
        float sx, sy, depth;
        if (!cam.project(x, y, z, area.w, area.h, sx, sy, depth)) return;
        float w = atlas.measure(Font::Ui, s, size);
        b.text(Font::Ui, size, area.x + sx - w * 0.5f, area.y + sy + size * 0.36f, s, c, LabelHalo);
    };
    double k = 1.08;
    if (settings.xAxis) put(L * k, 0, 0, "x", Axis3DLabelSize, Text);
    if (settings.yAxis) put(0, L * k, 0, "y", Axis3DLabelSize, Text);
    put(0, 0, L * k, "z", Axis3DLabelSize, Text);
    // Point labels: coordinates (or the label text) beside the projected point.
    for (const GraphItem& it : items) {
        if (it.style.hidden || !it.style.showLabel || it.an->graph != GraphKind::Point3D) continue;
        for (const GraphPiece& pc : it.an->pieces) {
            float sx, sy, depth;
            if (!std::isfinite(pc.px + pc.py + pc.pz) || !cam.project(pc.px, pc.py, pc.pz, area.w, area.h, sx, sy, depth))
                continue;
            std::string t = it.style.label.empty()
                                ? "(" + tickText(pc.px) + ", " + tickText(pc.py) + ", " + tickText(pc.pz) + ")"
                                : labelText(it.style, pc.px, pc.py);
            b.text(Font::Ui, CoordLabelSize, area.x + sx + 10, area.y + sy - 10, t, Text, LabelHalo);
        }
    }
    if (settings.axisNumbers) {
        double step = gridStep3D() * 2;
        for (double v = std::ceil(-L / step) * step; v <= L * 1.0001; v += step) {
            if (std::fabs(v) < step * 1e-6) continue;
            std::string t = tickText(v);
            if (settings.xAxis) put(v, 0, 0, t, Tick3DLabelSize, LabelHanging);
            if (settings.yAxis) put(0, v, 0, t, Tick3DLabelSize, LabelHanging);
            put(0, 0, v, t, Tick3DLabelSize, LabelHanging);
        }
    }
    b.popClip();
}

} // namespace calc
