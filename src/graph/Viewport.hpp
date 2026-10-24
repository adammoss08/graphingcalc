#pragma once
// World <-> screen mapping for the graph paper. Screen coordinates are points (not pixels),
// relative to the graph area's top-left, y down. World is y up.
#include <algorithm>
#include <cmath>

namespace calc {

struct Viewport {
    double cx = 0, cy = 0;     // world coordinate at the centre of the graph area
    double scale = 50;         // points per world unit (x and y: Desmos keeps a square grid by default)
    double width = 800, height = 600;

    double xmin() const { return cx - width * 0.5 / scale; }
    double xmax() const { return cx + width * 0.5 / scale; }
    double ymin() const { return cy - height * 0.5 / scale; }
    double ymax() const { return cy + height * 0.5 / scale; }

    double sx(double x) const { return (x - cx) * scale + width * 0.5; }
    double sy(double y) const { return height * 0.5 - (y - cy) * scale; }
    double wx(double sxp) const { return cx + (sxp - width * 0.5) / scale; }
    double wy(double syp) const { return cy + (height * 0.5 - syp) / scale; }

    // Desmos default: x spans [-10, 10] across the wider dimension, origin centred.
    static Viewport defaultFor(double w, double h) {
        Viewport v;
        v.width = w;
        v.height = h;
        v.scale = std::max(w, 1.0) / 20.0;
        if (h > w) v.scale = std::max(h, 1.0) / 20.0;
        return v;
    }

    void resize(double w, double h) { width = w; height = h; }

    void pan(double dxPoints, double dyPoints) {
        cx -= dxPoints / scale;
        cy += dyPoints / scale;
    }

    // Zoom by `factor` (>1 zooms in) keeping the world point under (sxp, syp) fixed.
    void zoomAbout(double sxp, double syp, double factor) {
        double wxp = wx(sxp), wyp = wy(syp);
        scale = std::clamp(scale * factor, 1e-12, 1e14);
        cx = wxp - (sxp - width * 0.5) / scale;
        cy = wyp - (height * 0.5 - syp) / scale;
    }

    bool sameView(const Viewport& o) const {
        return cx == o.cx && cy == o.cy && scale == o.scale && width == o.width && height == o.height;
    }
};

} // namespace calc
