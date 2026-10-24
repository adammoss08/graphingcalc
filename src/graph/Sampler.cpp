#include "graph/Sampler.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace calc {

namespace {

constexpr float kNaNf = std::numeric_limits<float>::quiet_NaN();
constexpr double kInitialStepPts = 3.0;   // first pass: one sample every 3 points
constexpr int kMaxDepth = 8;              // 3pt / 2^8 ≈ 0.012pt
constexpr double kFlatTolPts = 0.15;      // midpoint deviation that triggers subdivision
constexpr double kClampPts = 1e7;         // keep floats sane near asymptotes
constexpr size_t kMaxPoints = 400000;

float clampPts(double v) {
    if (v > kClampPts) return float(kClampPts);
    if (v < -kClampPts) return float(-kClampPts);
    return float(v);
}

constexpr float kSimplifyTolPts = 0.05f;  // drop points within 0.1 device px of the chord
constexpr int kMaxDropped = 64;

// Streams points into `out`, dropping points that lie on the chord between their neighbours
// (within kSimplifyTolPts). Fewer, longer segments mean far fewer overlapping capsule fragments
// on the GPU with no visible difference.
struct Emitter {
    CurvePoints& out;
    bool alongY;
    bool penDown = false;
    size_t runStart = 0;       // xy index of the first point of the current run
    float dropped[kMaxDropped * 2];
    int nDropped = 0;

    static float distToSegment(float px, float py, float ax, float ay, float bx, float by) {
        float dx = bx - ax, dy = by - ay;
        float len2 = dx * dx + dy * dy;
        float t = len2 > 0 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / len2, 0.0f, 1.0f) : 0.0f;
        float qx = ax + t * dx - px, qy = ay + t * dy - py;
        return std::sqrt(qx * qx + qy * qy);
    }

    void point(double u, double v) {
        double wx = alongY ? v : u, wy = alongY ? u : v;
        float cx = clampPts((wx - out.anchorX) * out.scale);
        float cy = clampPts(-(wy - out.anchorY) * out.scale);
        if (!penDown) {
            runStart = out.xy.size();
            nDropped = 0;
            out.xy.push_back(cx);
            out.xy.push_back(cy);
            penDown = true;
            return;
        }
        size_t n = out.xy.size();
        if (n - runStart >= 4 && nDropped < kMaxDropped) {
            // ..., A, B  + new C: can B be dropped (and everything dropped since A still fits)?
            float ax = out.xy[n - 4], ay = out.xy[n - 3], bx = out.xy[n - 2], by = out.xy[n - 1];
            bool ok = distToSegment(bx, by, ax, ay, cx, cy) <= kSimplifyTolPts;
            for (int k = 0; ok && k < nDropped; ++k)
                ok = distToSegment(dropped[2 * k], dropped[2 * k + 1], ax, ay, cx, cy) <= kSimplifyTolPts;
            if (ok) {
                dropped[2 * nDropped] = bx;
                dropped[2 * nDropped + 1] = by;
                ++nDropped;
                out.xy[n - 2] = cx;
                out.xy[n - 1] = cy;
                return;
            }
        }
        nDropped = 0;
        out.xy.push_back(cx);
        out.xy.push_back(cy);
    }
    void lift() {
        if (penDown) { out.xy.push_back(kNaNf); out.xy.push_back(kNaNf); }
        penDown = false;
        nDropped = 0;
    }
};

class ExplicitSampler {
public:
    ExplicitSampler(const Program& f, bool alongY, double scale, double cullLo, double cullHi, Emitter& em)
        : m_f(f), m_alongY(alongY), m_scale(scale), m_cullLo(cullLo), m_cullHi(cullHi), m_em(em) {}

    double eval(double u) const { return m_alongY ? evaluate(m_f, 0, u) : evaluate(m_f, u); }

    void interval(double a, double fa, double b, double fb, int depth) {
        if (m_em.out.xy.size() > kMaxPoints * 2) return;
        bool okA = std::isfinite(fa), okB = std::isfinite(fb);
        if (okA && okB) {
            if ((fa > m_cullHi && fb > m_cullHi) || (fa < m_cullLo && fb < m_cullLo)) {
                // Entirely off-screen on one side: nothing visible to refine.
                if (!m_em.penDown) m_em.point(a, fa);
                m_em.point(b, fb);
                return;
            }
            if (depth >= kMaxDepth) {
                if (std::fabs(fb - fa) * m_scale > 1.0 && isJump(a, fa, b, fb)) m_em.lift();
                else if (!m_em.penDown) m_em.point(a, fa);
                m_em.point(b, fb);
                return;
            }
            double m = 0.5 * (a + b), fm = eval(m);
            if (std::isfinite(fm)) {
                double dev = std::fabs(fm - 0.5 * (fa + fb)) * m_scale;
                if (dev < kFlatTolPts && depth >= 1) {
                    if (!m_em.penDown) m_em.point(a, fa);
                    m_em.point(b, fb);
                    return;
                }
            }
            interval(a, fa, m, fm, depth + 1);
            interval(m, fm, b, fb, depth + 1);
            return;
        }
        if (okA != okB) {
            // Find the edge of the defined region by bisection, then lift the pen.
            double lo = a, hi = b, flo = fa;
            if (!okA) { lo = b; hi = a; flo = fb; }
            for (int i = 0; i < 40; ++i) {
                double m = 0.5 * (lo + hi), fm = eval(m);
                if (std::isfinite(fm)) { lo = m; flo = fm; } else hi = m;
            }
            if (okA) {
                if (!m_em.penDown) m_em.point(a, fa);
                m_em.point(lo, flo);
                m_em.lift();
            } else {
                m_em.lift();
                m_em.point(lo, flo);
                m_em.point(b, fb);
            }
            return;
        }
        // Neither end defined: look for an island in the middle a few levels down.
        m_em.lift();
        if (depth < 3) {
            double m = 0.5 * (a + b), fm = eval(m);
            if (std::isfinite(fm)) {
                interval(a, fa, m, fm, depth + 1);
                interval(m, fm, b, fb, depth + 1);
            }
        }
    }

private:
    const Program& m_f;
    bool m_alongY;
    double m_scale;
    double m_cullLo, m_cullHi;
    Emitter& m_em;

    // Bisect towards the larger change; a continuous function's change vanishes, a jump doesn't.
    bool isJump(double a, double fa, double b, double fb) const {
        for (int i = 0; i < 48; ++i) {
            double m = 0.5 * (a + b), fm = eval(m);
            if (!std::isfinite(fm)) return true;
            if (std::fabs(fm - fa) > std::fabs(fb - fm)) { b = m; fb = fm; }
            else { a = m; fa = fm; }
            if (std::fabs(fb - fa) * m_scale < 0.5) return false;
        }
        return true;
    }
};

} // namespace

bool CurvePoints::covers(const Viewport& vp, bool alongY) const {
    if (scale != vp.scale || scale == 0) return false;
    if (viewIndependentDomain) return true;
    double lo = alongY ? vp.ymin() : vp.xmin(), hi = alongY ? vp.ymax() : vp.xmax();
    double dlo = alongY ? vp.xmin() : vp.ymin(), dhi = alongY ? vp.xmax() : vp.ymax();
    return domainLo <= lo && domainHi >= hi && cullLo <= dlo && cullHi >= dhi;
}

namespace {

// Samples [lo, hi] into `xy` relative to the anchor already set on `out`.
void sampleRange(const Program& f, bool alongY, double lo, double hi, CurvePoints& out,
                 std::vector<float>& xy) {
    double spanPts = (hi - lo) * out.scale;
    int n = std::max(2, int(std::ceil(spanPts / kInitialStepPts)));
    std::vector<double> us(n + 1), fs(n + 1);
    for (int i = 0; i <= n; ++i) us[i] = lo + (hi - lo) * i / n;
    if (alongY) evaluateBatch(f, n + 1, nullptr, us.data(), nullptr, fs.data());
    else evaluateBatch(f, n + 1, us.data(), nullptr, nullptr, fs.data());
    std::swap(out.xy, xy);
    out.xy.clear();
    out.xy.reserve(size_t(n) * 4);
    Emitter em{out, alongY};
    ExplicitSampler s(f, alongY, out.scale, out.cullLo, out.cullHi, em);
    for (int i = 0; i < n; ++i) s.interval(us[i], fs[i], us[i + 1], fs[i + 1], 0);
    std::swap(out.xy, xy);
}

void setCull(const Viewport& vp, bool alongY, CurvePoints& out) {
    double lo = alongY ? vp.xmin() : vp.ymin(), hi = alongY ? vp.xmax() : vp.ymax();
    double h = hi - lo;
    out.cullLo = lo - h;
    out.cullHi = hi + h;
}

} // namespace

void sampleExplicit(const Program& f, const Viewport& vp, bool alongY, CurvePoints& out) {
    out.anchorX = vp.cx;
    out.anchorY = vp.cy;
    out.scale = vp.scale;
    out.hash = f.hash();
    out.viewIndependentDomain = false;
    setCull(vp, alongY, out);
    double lo = alongY ? vp.ymin() : vp.xmin(), hi = alongY ? vp.ymax() : vp.xmax();
    double margin = 0.5 * (hi - lo);
    out.domainLo = lo - margin;
    out.domainHi = hi + margin;
    sampleRange(f, alongY, out.domainLo, out.domainHi, out, out.xy);
}

bool extendExplicit(const Program& f, const Viewport& vp, bool alongY, CurvePoints& c) {
    if (c.scale != vp.scale || c.hash != f.hash() || c.viewIndependentDomain || c.xy.empty()) return false;
    double lo = alongY ? vp.ymin() : vp.xmin(), hi = alongY ? vp.ymax() : vp.xmax();
    double dlo = alongY ? vp.xmin() : vp.ymin(), dhi = alongY ? vp.xmax() : vp.ymax();
    if (c.cullLo > dlo || c.cullHi < dhi) return false;  // panned across: culling no longer valid
    double w = hi - lo, margin = 0.5 * w;
    if (lo - c.domainHi > w || c.domainLo - hi > w) return false;  // jumped too far
    // Keep the cache bounded: if it would exceed 4 views wide, start over.
    double newLo = lo - 0.5 * margin < c.domainLo ? lo - margin : c.domainLo;
    double newHi = hi + 0.5 * margin > c.domainHi ? hi + margin : c.domainHi;
    if (newHi - newLo > 4 * w) return false;
    const float nanf = std::numeric_limits<float>::quiet_NaN();
    // Extend ahead of the visible edge (half a margin of lookahead) so panning never shows a gap.
    if (lo - 0.5 * margin < c.domainLo) {
        std::vector<float> left;
        sampleRange(f, alongY, newLo, c.domainLo, c, left);
        left.push_back(nanf); left.push_back(nanf);
        c.xy.insert(c.xy.begin(), left.begin(), left.end());
        c.domainLo = newLo;
    }
    if (hi + 0.5 * margin > c.domainHi) {
        std::vector<float> right;
        sampleRange(f, alongY, c.domainHi, newHi, c, right);
        c.xy.push_back(nanf); c.xy.push_back(nanf);
        c.xy.insert(c.xy.end(), right.begin(), right.end());
        c.domainHi = newHi;
    }
    return true;
}

void sampleParametric(const Program& fx, const Program* fy, double t0, double t1, bool polar,
                      const Viewport& vp, CurvePoints& out) {
    out.xy.clear();
    out.anchorX = vp.cx;
    out.anchorY = vp.cy;
    out.scale = vp.scale;
    out.hash = fx.hash() ^ (fy ? fy->hash() * 31 : 0);
    out.viewIndependentDomain = true;
    out.domainLo = t0;
    out.domainHi = t1;

    auto pointAt = [&](double t, double& x, double& y) {
        if (polar) {
            double r = evaluate(fx, 0, 0, t);
            x = r * std::cos(t);
            y = r * std::sin(t);
        } else {
            x = evaluate(fx, 0, 0, t);
            y = evaluate(*fy, 0, 0, t);
        }
        return std::isfinite(x) && std::isfinite(y);
    };

    Emitter em{out, false};
    const double s = vp.scale;
    std::function<void(double, double, double, double, double, double, int)> refine;
    refine = [&](double ta, double xa, double ya, double tb, double xb, double yb, int depth) {
        if (out.xy.size() > kMaxPoints * 2) return;
        double tm = 0.5 * (ta + tb), xm, ym;
        bool okM = pointAt(tm, xm, ym);
        double chord = std::hypot(xb - xa, yb - ya) * s;
        if (depth < 12 && okM) {
            double dev = std::hypot(xm - 0.5 * (xa + xb), ym - 0.5 * (ya + yb)) * s;
            if (dev > kFlatTolPts || chord > 8.0) {
                refine(ta, xa, ya, tm, xm, ym, depth + 1);
                refine(tm, xm, ym, tb, xb, yb, depth + 1);
                return;
            }
        }
        if (!okM || (depth >= 12 && chord > 50.0)) { em.lift(); return; }
        if (!em.penDown) em.point(xa, ya);
        em.point(xb, yb);
    };

    int n = polar ? 4000 : 800;
    double tPrev = t0, xPrev = 0, yPrev = 0;
    bool okPrev = pointAt(t0, xPrev, yPrev);
    for (int i = 1; i <= n; ++i) {
        double t = t0 + (t1 - t0) * i / n, x, y;
        bool ok = pointAt(t, x, y);
        if (ok && okPrev) refine(tPrev, xPrev, yPrev, t, x, y, 0);
        else em.lift();
        tPrev = t; xPrev = x; yPrev = y; okPrev = ok;
    }
}

} // namespace calc
