#include "graph/Grid.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace calc {

namespace {

void stepFor(double target, double& major, double& minor) {
    static const double kMajor[] = {1, 2, 5};
    static const double kMinorDiv[] = {5, 4, 5};
    major = INFINITY;
    minor = 0;
    for (int i = 0; i < 3; ++i) {
        double cand = kMajor[i] * std::pow(10.0, std::ceil(std::log10(target / kMajor[i])));
        if (cand < major) {
            major = cand;
            minor = cand / kMinorDiv[i];
        }
    }
}

} // namespace

GridSteps computeGridSteps(const Viewport& vp, double pixelsPerLabel) {
    GridSteps s;
    double xr = vp.xmax() - vp.xmin(), yr = vp.ymax() - vp.ymin();
    stepFor(pixelsPerLabel / vp.width * xr, s.majorX, s.minorX);
    stepFor(pixelsPerLabel / vp.height * yr, s.majorY, s.minorY);
    // Square grid (x and y pixel scales within 1%): both axes use the step chosen from the
    // dimension whose ratio rule Desmos applies; with equal scales the steps are simply unified.
    double ratio = (vp.width / xr) / (vp.height / yr);
    if (ratio >= 0.99 && ratio <= 1.01) {
        double m = std::min(s.majorX, s.majorY);
        double n = s.majorX <= s.majorY ? s.minorX : s.minorY;
        s.majorX = s.majorY = m;
        s.minorX = s.minorY = n;
    }
    return s;
}

std::vector<double> gridValues(double lo, double hi, double step) {
    std::vector<double> out;
    if (!(step > 0) || !std::isfinite(lo) || !std::isfinite(hi)) return out;
    double first = std::ceil(lo / step);
    double last = std::floor(hi / step);
    if (last - first > 2000) return out;
    for (double k = first; k <= last; ++k) out.push_back(k * step);
    return out;
}

AxisLabel formatAxisLabel(double value, double step) {
    AxisLabel l;
    l.value = value;
    double a = std::fabs(value);
    if (a < step * 1e-9) {
        l.mantissa = "0";
        return l;
    }
    int stepExp = int(std::floor(std::log10(step) + 1e-9));
    int valExp = int(std::floor(std::log10(a) + 1e-9));
    char buf[64];
    // Scientific only for big values on a big step (3×10^7) or tiny ones; a fine step at a large
    // offset reads better in full (1000000.01).
    bool scientific = (valExp >= 6 && stepExp >= 1) || valExp <= -5 || valExp - stepExp > 15;
    if (scientific) {
        // Scientific: digits needed = difference between value and step exponents.
        double m = value / std::pow(10.0, valExp);
        int decimals = std::clamp(valExp - stepExp, 0, 10);
        std::snprintf(buf, sizeof buf, "%.*f", decimals, m);
        std::string s = buf;
        if (s.find('.') != std::string::npos) {
            while (s.back() == '0') s.pop_back();
            if (s.back() == '.') s.pop_back();
        }
        l.mantissa = s;
        l.exponent = valExp;
        l.scientific = true;
        return l;
    }
    int decimals = std::max(0, -stepExp);
    std::snprintf(buf, sizeof buf, "%.*f", decimals, value);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {  // 0.5 steps label "1", not "1.0"
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    l.mantissa = s;
    return l;
}

} // namespace calc
