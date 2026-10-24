#include "graph/Poi.hpp"

#include <cmath>

namespace calc {

namespace {

double bisectZero(const Program& f, const Program* g, double a, double fa, double b) {
    auto h = [&](double x) { return evaluate(f, x) - (g ? evaluate(*g, x) : 0.0); };
    for (int i = 0; i < 60; ++i) {
        double m = 0.5 * (a + b), fm = h(m);
        if (fm == 0) return m;
        if ((fm < 0) == (fa < 0)) { a = m; fa = fm; } else b = m;
    }
    return 0.5 * (a + b);
}

double goldenExtremum(const Program& f, double a, double b, bool maximum) {
    const double r = 0.6180339887498949;
    double c = b - r * (b - a), d = a + r * (b - a);
    auto v = [&](double x) { double y = evaluate(f, x); return maximum ? -y : y; };
    double fc = v(c), fd = v(d);
    for (int i = 0; i < 80; ++i) {
        if (fc < fd) { b = d; d = c; fd = fc; c = b - r * (b - a); fc = v(c); }
        else { a = c; c = d; fc = fd; d = a + r * (b - a); fd = v(d); }
    }
    return 0.5 * (a + b);
}

void zerosOf(const Program& f, const Program* g, const Viewport& vp, Poi::Type type, std::vector<Poi>& out) {
    double lo = vp.xmin(), hi = vp.xmax();
    int n = std::max(64, int(vp.width / 2));
    auto h = [&](double x) { return evaluate(f, x) - (g ? evaluate(*g, x) : 0.0); };
    double xa = lo, ha = h(xa);
    double tol = 2.0 / vp.scale;  // reject sign changes through a jump of more than ~2pt
    for (int i = 1; i <= n; ++i) {
        double xb = lo + (hi - lo) * i / n, hb = h(xb);
        if (std::isfinite(ha) && std::isfinite(hb)) {
            if (ha == 0) {
                out.push_back({xa, evaluate(f, xa), type});
            } else if ((ha < 0) != (hb < 0) && hb != 0) {
                double z = bisectZero(f, g, xa, ha, xb);
                double hz = h(z);
                if (std::isfinite(hz) && std::fabs(hz) < tol) out.push_back({z, evaluate(f, z), type});
            }
        }
        xa = xb;
        ha = hb;
    }
}

} // namespace

void findPois(const Program& f, const std::vector<const Program*>& others, const Viewport& vp,
              std::vector<Poi>& out) {
    out.clear();
    zerosOf(f, nullptr, vp, Poi::Zero, out);
    for (auto& p : out) p.y = 0;

    // Extrema: slope sign changes on a fine grid, refined by golden-section search.
    double lo = vp.xmin(), hi = vp.xmax();
    int n = std::max(64, int(vp.width / 2));
    double h = (hi - lo) / n;
    double y0 = evaluate(f, lo), y1 = evaluate(f, lo + h);
    for (int i = 2; i <= n; ++i) {
        double x2 = lo + h * i, y2 = evaluate(f, x2);
        if (std::isfinite(y0) && std::isfinite(y1) && std::isfinite(y2)) {
            double d1 = y1 - y0, d2 = y2 - y1;
            bool max = d1 > 0 && d2 < 0, min = d1 < 0 && d2 > 0;
            // Ignore cusps through asymptotes: both slopes must be modest in screen space.
            if ((max || min) && std::fabs(d1) * vp.scale < 200 && std::fabs(d2) * vp.scale < 200) {
                double xe = goldenExtremum(f, x2 - 2 * h, x2, max);
                double ye = evaluate(f, xe);
                if (std::isfinite(ye)) out.push_back({xe, ye, Poi::Extremum});
            }
        }
        y0 = y1;
        y1 = y2;
    }

    if (lo <= 0 && hi >= 0) {
        double yi = evaluate(f, 0);
        if (std::isfinite(yi)) out.push_back({0, yi, Poi::Intercept});
    }
    for (const Program* g : others) zerosOf(f, g, vp, Poi::Intersection, out);
}

double roundForDisplay(double v, double scale) {
    // Show about 4 significant decimals relative to one screen point.
    double unit = 1.0 / scale;
    int decimals = std::max(0, int(std::ceil(-std::log10(unit))) + 1);
    decimals = std::min(decimals, 12);
    double p = std::pow(10.0, decimals);
    double r = std::round(v * p) / p;
    return r == 0 ? 0 : r;  // no "-0"
}

} // namespace calc
