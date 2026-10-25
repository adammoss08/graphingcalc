#include "ui/GraphView.hpp"
#include "graph/Grid.hpp"
#include "math/MathTree.hpp"
#include "ui/Widgets.hpp"

#include <dispatch/dispatch.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_set>

namespace calc {

using namespace theme;

namespace {

constexpr float kSettingsWidth = 230;
constexpr float kSettingsRowH = 34;
constexpr int kSettingsRows = 4;           // grid, minor grid, axis numbers, radians/degrees
constexpr float kCheckboxSize = 16;

// Dotted lines: Desmos getDottedPattern gives ~0-length dashes with round caps every 1.62·width.
constexpr float kDotPeriodPerWidth = 1.62f;

std::u32string withMinus(const std::string& s) {
    std::u32string u = fromUtf8(s);
    for (auto& c : u) if (c == '-') c = sym::Minus;
    return u;
}

struct SampleJob {
    const GraphPiece* piece;
    GraphKind kind;
    double t0, t1;
    CurvePoints* out;
    Viewport vp;
    uint64_t hash;   // what the cache entry is valid for (programs + domain)
};

constexpr double kAsyncCostMs = 4;   // curves slower than this resample in the background

double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void runSampleJob(void* ctx, size_t i) {
    SampleJob& j = static_cast<SampleJob*>(ctx)[i];
    auto t0 = std::chrono::steady_clock::now();
    switch (j.kind) {
    case GraphKind::ExplicitY: case GraphKind::ExplicitX: {
        bool alongY = j.kind == GraphKind::ExplicitX;
        if (!extendExplicit(j.piece->f, j.vp, alongY, *j.out)) sampleExplicit(j.piece->f, j.vp, alongY, *j.out);
        break;
    }
    case GraphKind::Polar: sampleParametric(j.piece->f, nullptr, j.t0, j.t1, true, j.vp, *j.out); break;
    case GraphKind::Parametric: sampleParametric(j.piece->f, &j.piece->g, j.t0, j.t1, false, j.vp, *j.out); break;
    default: break;
    }
    j.out->hash = j.hash;
    j.out->costMs = msSince(t0);
}

uint64_t mixDomain(uint64_t h, double a, double b) {
    uint64_t ua, ub;
    std::memcpy(&ua, &a, 8);
    std::memcpy(&ub, &b, 8);
    return h ^ (ua * 0x9e3779b97f4a7c15ull) ^ (ub * 0xc2b2ae3d27d4eb4full);
}

int dashMode(const Style& st, const RowAnalysis& an) {
    if (an.rel != Cmp::Eq && an.strict) return 1;
    return st.line == LineStyle::Dashed ? 1 : st.line == LineStyle::Dotted ? 2 : 0;
}

// Draws a point marker in the item's point style.
void drawPoint(Batch2D& b, float x, float y, const Style& st, bool highlight) {
    float r = st.pointSize * 0.5f * (highlight ? 1.25f : 1.0f);
    Color c = st.color;
    switch (st.point) {
    case PointStyle::Open:
        b.circle(x, y, r, White, std::max(2.0f, r * 0.45f), c);
        break;
    case PointStyle::Cross: {
        float k = r * 0.85f;
        b.line(x - k, y - k, x + k, y + k, std::max(2.0f, r * 0.45f), c);
        b.line(x - k, y + k, x + k, y - k, std::max(2.0f, r * 0.45f), c);
        break;
    }
    default:
        b.circle(x, y, r, c);
        break;
    }
}

// Nearest point with F(x, y) = 0 within `radius` (world units) of (wx, wy), by rays + bisection.
bool nearestImplicitRoot(const Program& f, double wx, double wy, double radius, double& ox, double& oy) {
    double f0 = evaluate(f, wx, wy);
    if (std::isfinite(f0) && f0 == 0) { ox = wx; oy = wy; return true; }
    double best = INFINITY;
    const int rays = 24, steps = 10;
    for (int k = 0; k < rays; ++k) {
        double ang = 2 * M_PI * k / rays, dx = std::cos(ang), dy = std::sin(ang);
        double pa = 0, fa = f0;
        for (int s = 1; s <= steps; ++s) {
            double pb = radius * s / steps;
            double fb = evaluate(f, wx + dx * pb, wy + dy * pb);
            if (std::isfinite(fa) && std::isfinite(fb) && (fa < 0) != (fb < 0)) {
                double lo = pa, hi = pb, flo = fa;
                for (int it = 0; it < 40; ++it) {
                    double m = 0.5 * (lo + hi), fm = evaluate(f, wx + dx * m, wy + dy * m);
                    if (!std::isfinite(fm)) break;
                    if ((fm < 0) == (flo < 0)) { lo = m; flo = fm; } else hi = m;
                }
                double d = 0.5 * (lo + hi);
                // reject poles: |F| must be small at the root compared to the bracket
                double fr = evaluate(f, wx + dx * d, wy + dy * d);
                if (std::isfinite(fr) && std::fabs(fr) < 0.1 * std::max(std::fabs(fa), std::fabs(fb)) + 1e-12 && d < best) {
                    best = d;
                    ox = wx + dx * d;
                    oy = wy + dy * d;
                }
                break;
            }
            pa = pb;
            fa = fb;
        }
    }
    return std::isfinite(best);
}

uint64_t paramHash(const GraphPiece& p, GraphKind k) {
    return k == GraphKind::Parametric ? p.f.hash() ^ (p.g.hash() * 31) : p.f.hash();
}

// Axis label: mantissa, optionally "×10^exp".
float axisLabelWidth(TextAtlas& a, const AxisLabel& l) {
    float w = a.measure(Font::Ui, withMinus(l.mantissa), LabelSize);
    if (l.scientific) {
        w += a.measure(Font::Ui, std::u32string(U"×10"), LabelSize);
        w += a.measure(Font::Ui, withMinus(std::to_string(l.exponent)), LabelSize * 0.7f);
    }
    return w;
}

void drawAxisLabel(Batch2D& b, const AxisLabel& l, float x, float baseline, Color c) {
    float pen = x + b.text(Font::Ui, LabelSize, x, baseline, withMinus(l.mantissa), c, LabelHalo);
    if (l.scientific) {
        pen += b.text(Font::Ui, LabelSize, pen, baseline, std::u32string(U"×10"), c, LabelHalo);
        b.text(Font::Ui, LabelSize * 0.7f, pen, baseline - LabelSize * 0.45f, withMinus(std::to_string(l.exponent)), c, LabelHalo);
    }
}

std::string coordText(double x, double y, double scale) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "(%.12g, %.12g)", roundForDisplay(x, scale), roundForDisplay(y, scale));
    std::string s = buf;
    for (size_t p = s.find('-'); p != std::string::npos; p = s.find('-', p + 3)) s.replace(p, 1, "−");
    return s;
}

} // namespace

void GraphView::setArea(const Rect& r) {
    area = r;
    if (!m_initialised) {
        vp = Viewport::defaultFor(r.w, r.h);
        m_initialised = true;
    } else {
        vp.resize(r.w, r.h);
    }
}

void GraphView::resetView() { vp = Viewport::defaultFor(area.w, area.h); }

bool GraphView::viewIsDefault() const {
    Viewport d = Viewport::defaultFor(area.w, area.h);
    return std::fabs(vp.cx - d.cx) * vp.scale < 0.5 && std::fabs(vp.cy - d.cy) * vp.scale < 0.5 &&
           std::fabs(vp.scale / d.scale - 1) < 1e-6;
}

void GraphView::zoomBy(double factor) { vp.zoomAbout(area.w * 0.5, area.h * 0.5, factor); }

// ---- frame building ----------------------------------------------------------------------------

void GraphView::buildFrame(const std::vector<GraphItem>& items, int selected, GraphFrame& out) {
    auto t0 = std::chrono::steady_clock::now();
    out.area = area;
    out.vp = vp;
    out.curves.clear();
    out.implicits.clear();
    out.images.clear();
    std::vector<SampleJob> jobs;
    std::unordered_set<uint64_t> used;

    for (const GraphItem& it : items) {
        const RowAnalysis& an = *it.an;
        const Style& st = it.style;
        if (st.hidden) continue;
        bool hl = it.row == selected || it.row == m_hoverRow;
        float width = st.lineWidth + (hl ? CurveWidthHighlight - CurveWidth : 0);
        float opacity = hl ? CurveOpacityHighlight : st.lineOpacity;
        int dash = dashMode(st, an);
        float dashLen = dash == 1 ? DashLength * width / CurveWidth : dash == 2 ? -kDotPeriodPerWidth * width : 0;
        for (size_t p = 0; p < an.pieces.size(); ++p) {
            const GraphPiece& pc = an.pieces[p];
            switch (an.graph) {
            case GraphKind::ExplicitY: case GraphKind::ExplicitX:
            case GraphKind::Polar: case GraphKind::Parametric: {
                uint64_t k = it.key + p;
                used.insert(k);
                CurvePoints& c = m_cache[k];
                bool alongY = an.graph == GraphKind::ExplicitX;
                bool explicitKind = an.graph == GraphKind::ExplicitY || alongY;
                uint64_t h = explicitKind ? pc.f.hash() : mixDomain(paramHash(pc, an.graph), an.tMin, an.tMax);
                bool stale = c.hash != h || c.xy.empty() || !c.covers(vp, alongY) ||
                             (explicitKind && c.nearEdge(vp, alongY));
                if (stale) {
                    if (asyncSampling && !c.xy.empty() && c.costMs > kAsyncCostMs) {
                        // Heavy: keep drawing the old samples (rescaled) and refine in the background.
                        scheduleAsync(k, pc, an.graph, an.tMin, an.tMax, h);
                    } else {
                        if (explicitKind && c.hash != h) c.xy.clear();  // full resample, not an extension
                        c.hash = explicitKind ? h : c.hash;
                        jobs.push_back({&pc, an.graph, an.tMin, an.tMax, &c, vp, h});
                    }
                }
                out.curves.push_back({&c, st.color, width, opacity, dashLen, 1.0f});
                if (explicitKind && an.rel != Cmp::Eq)
                    out.implicits.push_back({&pc.region, st.color, width, opacity, 0, false, true, st.fillOpacity});
                break;
            }
            case GraphKind::Implicit:
                out.implicits.push_back({&pc.f, st.color, width, opacity, dashLen, true, an.rel != Cmp::Eq,
                                         st.fillOpacity});
                break;
            default:
                break;
            }
        }
    }
    if (jobs.size() == 1) {
        runSampleJob(jobs.data(), 0);
    } else if (!jobs.empty()) {
        dispatch_apply_f(jobs.size(), dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), jobs.data(),
                         runSampleJob);
    }
    for (auto& cd : out.curves)
        if (cd.points->scale > 0) cd.zoom = float(vp.scale / cd.points->scale);
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (!used.count(it->first)) it = m_cache.erase(it);
        else ++it;
    }
    if (!jobs.empty())
        m_lastSampleMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    for (const ImageItem& im : images) {
        if (im.texture < 0 || !(im.w > 0) || !(im.h > 0)) continue;
        float x0 = area.x + float(vp.sx(im.cx - im.w * 0.5)), x1 = area.x + float(vp.sx(im.cx + im.w * 0.5));
        float y0 = area.y + float(vp.sy(im.cy + im.h * 0.5)), y1 = area.y + float(vp.sy(im.cy - im.h * 0.5));
        out.images.push_back({im.texture, Rect{x0, y0, x1 - x0, y1 - y0}, im.opacity});
    }
    updatePois(items, selected);
}

// ---- grid --------------------------------------------------------------------------------------

void GraphView::drawUnder(Batch2D& b) const {
    b.pushClip(area);
    // The render pass clears to white, which is the graph background: no full-area rect needed.
    static_assert(GraphBg.r == 1 && GraphBg.g == 1 && GraphBg.b == 1, "graph bg must match the clear colour");
    TextAtlas& atlas = b.atlas();
    float s = atlas.scale();
    float ox = area.x, oy = area.y;
    GridSteps st = computeGridSteps(vp, PixelsPerLabel);
    // Snap a line of width w (points) so its edges land on device pixels.
    auto snap = [&](float p, float w) {
        float wpx = std::round(w * s);
        return (std::round(p * s - wpx * 0.5f) + wpx * 0.5f) / s;
    };
    auto isMultiple = [](double v, double step) {
        double k = v / step;
        return std::fabs(k - std::round(k)) < 1e-6;
    };
    double x0 = vp.xmin(), x1 = vp.xmax(), y0 = vp.ymin(), y1 = vp.ymax();
    bool showY = settings.yAxis && x0 <= 0 && x1 >= 0;
    bool showX = settings.xAxis && y0 <= 0 && y1 >= 0;

    if (settings.grid) {
        Color minor = Black.withAlpha(MinorGridOpacity), major = Black.withAlpha(MajorGridOpacity);
        if (settings.minorGrid) {
            for (double v : gridValues(x0, x1, st.minorX)) {
                if (isMultiple(v, st.majorX)) continue;
                float x = snap(ox + float(vp.sx(v)), GridLineWidth);
                b.line(x, oy, x, oy + area.h, GridLineWidth, minor);
            }
            for (double v : gridValues(y0, y1, st.minorY)) {
                if (isMultiple(v, st.majorY)) continue;
                float y = snap(oy + float(vp.sy(v)), GridLineWidth);
                b.line(ox, y, ox + area.w, y, GridLineWidth, minor);
            }
        }
        for (double v : gridValues(x0, x1, st.majorX)) {
            if (v == 0 && showY) continue;
            float x = snap(ox + float(vp.sx(v)), GridLineWidth);
            b.line(x, oy, x, oy + area.h, GridLineWidth, major);
        }
        for (double v : gridValues(y0, y1, st.majorY)) {
            if (v == 0 && showX) continue;
            float y = snap(oy + float(vp.sy(v)), GridLineWidth);
            b.line(ox, y, ox + area.w, y, GridLineWidth, major);
        }
    }
    Color axis = Black.withAlpha(AxisOpacity);
    float ax = snap(ox + float(vp.sx(0)), AxisLineWidth);
    float ay = snap(oy + float(vp.sy(0)), AxisLineWidth);
    if (showY) b.line(ax, oy, ax, oy + area.h, AxisLineWidth, axis);
    if (showX) b.line(ox, ay, ox + area.w, ay, AxisLineWidth, axis);

    if (settings.axisNumbers) {
        float asc = LabelSize * 0.72f;   // Arial cap height-ish
        float m = LabelEdgeMargin;
        float axisScreenX = ox + float(vp.sx(0)), axisScreenY = oy + float(vp.sy(0));
        if (settings.xAxis) {
            for (double v : gridValues(x0, x1, st.majorX)) {
                AxisLabel l = formatAxisLabel(v, st.majorX);
                if (l.mantissa == "0") continue;
                float w = axisLabelWidth(atlas, l);
                float minusW = v < 0 ? atlas.advance(Font::Ui, sym::Minus, LabelSize) : 0;
                float x = ox + float(vp.sx(v)) - (w - minusW) * 0.5f - minusW;
                float top = axisScreenY + LabelOffset;
                float clampedTop = std::clamp(top, oy + m, oy + area.h - m - asc);
                if (x < ox + m || x + w > ox + area.w - m) continue;
                Color c = clampedTop == top ? Text : LabelHanging;
                drawAxisLabel(b, l, x, clampedTop + asc, c);
                if (showX) {
                    float tx = snap(ox + float(vp.sx(v)), AxisLineWidth);
                    b.line(tx, ay - TickHalfLength, tx, ay + TickHalfLength, AxisLineWidth, axis);
                }
            }
        }
        if (settings.yAxis) {
            for (double v : gridValues(y0, y1, st.majorY)) {
                AxisLabel l = formatAxisLabel(v, st.majorY);
                if (l.mantissa == "0") continue;
                float w = axisLabelWidth(atlas, l);
                float x = axisScreenX - LabelOffset - w;
                float clampedX = std::clamp(x, ox + m, ox + area.w - m - w);
                float baseline = oy + float(vp.sy(v)) + asc * 0.5f;
                if (baseline - asc < oy + m || baseline > oy + area.h - m) continue;
                Color c = clampedX == x ? Text : LabelHanging;
                drawAxisLabel(b, l, clampedX, baseline, c);
                if (showY) {
                    float ty = snap(oy + float(vp.sy(v)), AxisLineWidth);
                    b.line(ax - TickHalfLength, ty, ax + TickHalfLength, ty, AxisLineWidth, axis);
                }
            }
        }
        if (showX && showY) {
            AxisLabel zero = formatAxisLabel(0, 1);
            float w = axisLabelWidth(atlas, zero);
            drawAxisLabel(b, zero, axisScreenX - LabelOffset - w, axisScreenY + LabelOffset + asc, Text);
        }
    }
    b.popClip();
}

// ---- points, POIs, trace -----------------------------------------------------------------------

void GraphView::updatePois(const std::vector<GraphItem>& items, int selected) {
    const GraphItem* sel = nullptr;
    for (const GraphItem& it : items)
        if (it.row == selected && !it.style.hidden && it.an->graph == GraphKind::ExplicitY &&
            it.an->rel == Cmp::Eq && !it.an->pieces.empty()) { sel = &it; break; }
    if (!sel) {
        m_poi = {};
        return;
    }
    const Program& f = sel->an->pieces[0].f;
    uint64_t h = f.hash();
    std::vector<const Program*> others;
    for (const GraphItem& it : items) {
        if (&it == sel || it.style.hidden) continue;
        if (it.an->graph == GraphKind::ExplicitY && it.an->rel == Cmp::Eq)
            for (auto& p : it.an->pieces) { others.push_back(&p.f); h = h * 1315423911u + p.f.hash(); }
    }
    if (m_poi.rowId == uint32_t(sel->key >> 20) && m_poi.hash == h && m_poi.vp.sameView(vp)) return;
    m_poi.rowId = uint32_t(sel->key >> 20);
    m_poi.hash = h;
    m_poi.vp = vp;
    findPois(f, others, vp, m_poi.pois);
}

void GraphView::drawOver(Batch2D& b, const std::vector<GraphItem>& items, int selected) {
    b.pushClip(area);
    float ox = area.x, oy = area.y;
    for (const GraphItem& it : items) {
        const RowAnalysis& an = *it.an;
        if (it.style.hidden || an.graph != GraphKind::Point) continue;
        bool hl = it.row == selected || it.row == m_hoverRow;
        // Point lists (tables) may be connected by lines.
        if (it.style.showLines && it.connectPoints && an.pieces.size() > 1) {
            float w = it.style.lineWidth + (hl ? 1 : 0);
            Color c = it.style.color.withAlpha(hl ? 1 : it.style.lineOpacity);
            for (size_t k = 1; k < an.pieces.size(); ++k) {
                const auto& a = an.pieces[k - 1];
                const auto& p = an.pieces[k];
                if (!std::isfinite(a.px + a.py + p.px + p.py)) continue;
                b.line(ox + float(vp.sx(a.px)), oy + float(vp.sy(a.py)), ox + float(vp.sx(p.px)),
                       oy + float(vp.sy(p.py)), w, c);
            }
        }
        if (!it.style.showPoints) continue;
        for (auto& p : an.pieces) {
            if (!std::isfinite(p.px) || !std::isfinite(p.py)) continue;
            drawPoint(b, ox + float(vp.sx(p.px)), oy + float(vp.sy(p.py)), it.style, hl);
        }
    }
    for (size_t k = 0; k < m_poi.pois.size(); ++k) {
        const Poi& p = m_poi.pois[k];
        float x = ox + float(vp.sx(p.x)), y = oy + float(vp.sy(p.y));
        bool hot = int(k) == m_hoverPoi;
        b.circle(x, y, PoiDiameter * 0.5f + (hot ? 1 : 0), PoiGray.withAlpha(hot ? 1 : 0.85f));
        if (hot) {
            std::string t = coordText(p.x, p.y, vp.scale);
            b.text(Font::Ui, CoordLabelSize, x + 8, y - 8, t, Text, LabelHalo);
        }
    }
    if (m_trace.active && m_trace.item >= 0 && m_trace.item < int(items.size())) {
        float x = ox + float(vp.sx(m_trace.x)), y = oy + float(vp.sy(m_trace.y));
        b.circle(x, y, PointDiameter * 0.5f + 1, items[m_trace.item].style.color);
        std::string t = coordText(m_trace.x, m_trace.y, vp.scale);
        b.text(Font::Ui, CoordLabelSize, x + 8, y - 8, t, Text, LabelHalo);
    }
    b.popClip();
}

// ---- controls ----------------------------------------------------------------------------------

Rect GraphView::buttonRect(Button bt) const {
    float sz = ZoomButtonSize, x = area.right() - ZoomButtonInset - sz, y = area.y + ZoomButtonInset;
    switch (bt) {
    case Button::Wrench: return {x, y, sz, sz};
    case Button::ZoomIn: return {x, y + sz + ZoomButtonGap, sz, sz};
    case Button::ZoomOut: return {x, y + 2 * sz + ZoomButtonGap, sz, sz};
    case Button::Home: return {x, y + 3 * sz + 2 * ZoomButtonGap, sz, sz};
    default: return {};
    }
}

GraphView::Button GraphView::buttonAt(float x, float y) const {
    for (Button bt : {Button::Wrench, Button::ZoomIn, Button::ZoomOut, Button::Home}) {
        if (bt == Button::Home && viewIsDefault()) continue;
        if (buttonRect(bt).contains(x, y)) return bt;
    }
    return Button::None;
}

Rect GraphView::settingsRect() const {
    Rect w = buttonRect(Button::Wrench);
    return {w.x - kSettingsWidth - 8, w.y, kSettingsWidth, kSettingsRows * kSettingsRowH + 12};
}

int GraphView::settingAt(float x, float y) const {
    if (!m_settingsOpen) return -1;
    Rect r = settingsRect();
    if (!r.contains(x, y)) return -1;
    int i = int((y - r.y - 6) / kSettingsRowH);
    return i >= 0 && i < kSettingsRows ? i : -1;
}

void GraphView::drawControls(Batch2D& b) const {
    using widgets::Interaction;
    auto state = [&](Button bt) {
        if (m_pressButton == bt && m_hoverButton == bt) return Interaction::Pressed;
        if (m_hoverButton == bt) return Interaction::Hovered;
        return Interaction::Normal;
    };
    widgets::floatingButton(b, buttonRect(Button::Wrench), widgets::Icon::Wrench, state(Button::Wrench));
    Rect zi = buttonRect(Button::ZoomIn), zo = buttonRect(Button::ZoomOut);
    b.shadow({zi.x, zi.y + 1, zi.w, zi.h + zo.h}, ZoomButtonRadius, ShadowBlur, ShadowColor);
    widgets::floatingButton(b, zi, widgets::Icon::Plus, state(Button::ZoomIn), true, false);
    widgets::floatingButton(b, zo, widgets::Icon::Minus, state(Button::ZoomOut), false, true);
    b.line(zi.x + 1, zi.bottom(), zi.right() - 1, zi.bottom(), 1, ZoomButtonBorder);
    if (!viewIsDefault())
        widgets::floatingButton(b, buttonRect(Button::Home), widgets::Icon::Home, state(Button::Home));

    if (!m_settingsOpen) return;
    Rect r = settingsRect();
    b.shadow({r.x, r.y + 2, r.w, r.h}, 6, 8, ShadowColor.withAlpha(0.2f));
    b.rect(r, White, 6, 1, Border);
    const char* labels[] = {"Grid", "Minor Gridlines", "Axis Numbers"};
    bool values[] = {settings.grid, settings.minorGrid, settings.axisNumbers};
    for (int i = 0; i < 3; ++i) {
        float y = r.y + 6 + i * kSettingsRowH;
        if (m_hoverSetting == i) b.rect({r.x + 4, y, r.w - 8, kSettingsRowH}, HoverOverlay, 4);
        Rect box{r.x + 14, y + (kSettingsRowH - kCheckboxSize) * 0.5f, kCheckboxSize, kCheckboxSize};
        if (values[i]) {
            b.rect(box, Accent, 3);
            b.line(box.x + 3.5f, box.y + 8.5f, box.x + 6.5f, box.y + 11.5f, 2, White);
            b.line(box.x + 6.5f, box.y + 11.5f, box.x + 12.5f, box.y + 4.5f, 2, White);
        } else {
            b.rect(box, White, 3, 1.5f, Black.withAlpha(0.35f));
        }
        b.text(Font::Ui, 15, box.right() + 10, y + kSettingsRowH * 0.5f + 5, std::string(labels[i]), Text);
    }
    // Radians | Degrees segmented control
    float y = r.y + 6 + 3 * kSettingsRowH + 4;
    Rect seg{r.x + 14, y, r.w - 28, kSettingsRowH - 8};
    b.rect(seg, White, 4, 1, Accent);
    Rect half{seg.x, seg.y, seg.w * 0.5f, seg.h};
    Rect left = half, right{seg.x + seg.w * 0.5f, seg.y, seg.w * 0.5f, seg.h};
    b.rect(settings.degrees ? right : left, Accent, 4);
    float lw = b.atlas().measure(Font::Ui, std::string("Radians"), 14);
    float dw = b.atlas().measure(Font::Ui, std::string("Degrees"), 14);
    b.text(Font::Ui, 14, left.x + (left.w - lw) * 0.5f, seg.y + seg.h * 0.5f + 5, std::string("Radians"),
           settings.degrees ? Accent : White);
    b.text(Font::Ui, 14, right.x + (right.w - dw) * 0.5f, seg.y + seg.h * 0.5f + 5, std::string("Degrees"),
           settings.degrees ? White : Accent);
}

// ---- picking -----------------------------------------------------------------------------------

bool GraphView::pickCurve(float x, float y, const std::vector<GraphItem>& items, Trace& out, int onlyItem) const {
    float mx = x - area.x, my = y - area.y;
    double wx = vp.wx(mx), wy = vp.wy(my);
    float best = HoverPickRadius;
    if (onlyItem >= 0) best = INFINITY;  // tracing: follow the same curve wherever the pointer is
    bool found = false;
    for (int i = int(items.size()) - 1; i >= 0; --i) {
        if (onlyItem >= 0 && i != onlyItem) continue;
        const GraphItem& it = items[i];
        if (it.style.hidden) continue;
        const RowAnalysis& an = *it.an;
        for (size_t p = 0; p < an.pieces.size(); ++p) {
            const GraphPiece& pc = an.pieces[p];
            auto consider = [&](float d, double px, double py) {
                if (d < best) { best = d; out = {i, int(p), px, py, true}; found = true; }
            };
            switch (an.graph) {
            case GraphKind::Point:
                consider(std::hypot(float(vp.sx(pc.px)) - mx, float(vp.sy(pc.py)) - my), pc.px, pc.py);
                break;
            case GraphKind::ExplicitY: {
                double fy = evaluate(pc.f, wx);
                if (std::isfinite(fy)) consider(std::fabs(float(vp.sy(fy)) - my), wx, fy);
                break;
            }
            case GraphKind::ExplicitX: {
                double fx = evaluate(pc.f, 0, wy);
                if (std::isfinite(fx)) consider(std::fabs(float(vp.sx(fx)) - mx), fx, wy);
                break;
            }
            case GraphKind::Implicit: {
                double radius = (onlyItem >= 0 ? 4 * HoverPickRadius : HoverPickRadius) / vp.scale;
                double rx, ry;
                if (nearestImplicitRoot(pc.f, wx, wy, radius, rx, ry))
                    consider(float(std::hypot(rx - wx, ry - wy) * vp.scale), rx, ry);
                break;
            }
            case GraphKind::Polar: case GraphKind::Parametric: {
                auto c = m_cache.find(it.key + p);
                if (c == m_cache.end()) break;
                const CurvePoints& cp = c->second;
                float tx = float(vp.sx(cp.anchorX)), ty = float(vp.sy(cp.anchorY));
                float zm = cp.scale > 0 ? float(vp.scale / cp.scale) : 1.0f;
                for (size_t k = 0; k + 1 < cp.xy.size(); k += 2) {
                    float px = cp.xy[k] * zm + tx, py = cp.xy[k + 1] * zm + ty;
                    consider(std::hypot(px - mx, py - my), vp.wx(px), vp.wy(py));
                }
                break;
            }
            default: break;
            }
        }
    }
    return found;
}

// ---- events ------------------------------------------------------------------------------------

bool GraphView::mouseDown(float x, float y, int clicks, Mods m, const std::vector<GraphItem>& items) {
    m_downX = m_lastX = x;
    m_downY = m_lastY = y;
    if (m_settingsOpen) {
        int s = settingAt(x, y);
        if (s >= 0) {
            if (s == 0) settings.grid = !settings.grid;
            else if (s == 1) settings.minorGrid = !settings.minorGrid;
            else if (s == 2) settings.axisNumbers = !settings.axisNumbers;
            else {
                Rect r = settingsRect();
                settings.degrees = x > r.x + r.w * 0.5f;
                if (onSettingsChanged) onSettingsChanged();
            }
            return true;
        }
        if (settingsRect().contains(x, y)) return true;
        if (buttonAt(x, y) != Button::Wrench) {
            m_settingsOpen = false;
            return true;
        }
    }
    if (!area.contains(x, y)) return false;
    m_pressButton = buttonAt(x, y);
    m_hoverButton = m_pressButton;
    if (m_pressButton != Button::None) return true;
    Trace t;
    if (pickCurve(x, y, items, t)) {
        m_trace = t;
        if (onSelectRow) onSelectRow(items[t.item].row);
        return true;
    }
    m_panning = true;
    return true;
}

bool GraphView::mouseDragged(float x, float y, const std::vector<GraphItem>& items) {
    float dx = x - m_lastX, dy = y - m_lastY;
    m_lastX = x;
    m_lastY = y;
    if (m_pressButton != Button::None) {
        Button h = buttonAt(x, y);
        bool changed = h != m_hoverButton;
        m_hoverButton = h;
        return changed;
    }
    if (m_trace.active) {
        if (m_trace.item < int(items.size())) {
            const RowAnalysis& an = *items[m_trace.item].an;
            double wx = vp.wx(x - area.x), wy = vp.wy(y - area.y);
            if (m_trace.piece < int(an.pieces.size()) && an.graph == GraphKind::ExplicitY) {
                m_trace.x = wx;
                m_trace.y = evaluate(an.pieces[m_trace.piece].f, wx);
            } else if (m_trace.piece < int(an.pieces.size()) && an.graph == GraphKind::ExplicitX) {
                m_trace.y = wy;
                m_trace.x = evaluate(an.pieces[m_trace.piece].f, 0, wy);
            } else {
                Trace t;
                if (pickCurve(x, y, items, t, m_trace.item)) m_trace = t;
            }
        }
        return true;
    }
    if (m_panning) {
        vp.pan(dx, dy);
        return true;
    }
    return false;
}

bool GraphView::mouseUp(float x, float y) {
    if (m_pressButton != Button::None) {
        Button b = m_pressButton;
        m_pressButton = Button::None;
        if (buttonAt(x, y) == b) {
            switch (b) {
            case Button::Wrench: m_settingsOpen = !m_settingsOpen; break;
            case Button::ZoomIn: zoomBy(ZoomButtonFactor); break;
            case Button::ZoomOut: zoomBy(1.0 / ZoomButtonFactor); break;
            case Button::Home: resetView(); break;
            default: break;
            }
        }
        return true;
    }
    if (m_trace.active) {
        m_trace.active = false;
        return true;
    }
    if (m_panning) {
        m_panning = false;
        bool click = std::hypot(x - m_downX, y - m_downY) < DragThreshold;
        if (click && onSelectRow) onSelectRow(-1);
        return true;
    }
    return false;
}

bool GraphView::mouseMoved(float x, float y, const std::vector<GraphItem>& items) {
    bool changed = false;
    Button hb = area.contains(x, y) ? buttonAt(x, y) : Button::None;
    if (hb != m_hoverButton) { m_hoverButton = hb; changed = true; }
    int hs = settingAt(x, y);
    if (hs != m_hoverSetting) { m_hoverSetting = hs; changed = true; }
    int hoverRow = -1;
    Trace t;
    if (area.contains(x, y) && hb == Button::None && hs < 0 && !settingsRect().contains(x, y) &&
        pickCurve(x, y, items, t))
        hoverRow = items[t.item].row;
    if (hoverRow != m_hoverRow) { m_hoverRow = hoverRow; changed = true; }
    int hp = -1;
    for (size_t k = 0; k < m_poi.pois.size(); ++k) {
        float px = area.x + float(vp.sx(m_poi.pois[k].x)), py = area.y + float(vp.sy(m_poi.pois[k].y));
        if (std::hypot(px - x, py - y) < HoverPickRadius) { hp = int(k); break; }
    }
    if (hp != m_hoverPoi) { m_hoverPoi = hp; changed = true; }
    return changed;
}

bool GraphView::mouseExited() {
    bool changed = m_hoverRow >= 0 || m_hoverButton != Button::None || m_hoverPoi >= 0;
    m_hoverRow = -1;
    m_hoverButton = Button::None;
    m_hoverPoi = -1;
    return changed;
}

bool GraphView::scroll(float x, float y, float dx, float dy, bool precise) {
    if (!area.contains(x, y)) return false;
    double k = precise ? ScrollZoomSensitivity : 0.1;
    double factor = std::exp(double(dy) * k);
    vp.zoomAbout(x - area.x, y - area.y, factor);
    return true;
}

bool GraphView::magnify(float x, float y, float amount) {
    if (!area.contains(x, y)) return false;
    vp.zoomAbout(x - area.x, y - area.y, std::max(0.1, 1.0 + amount));
    return true;
}

// ---- background resampling ---------------------------------------------------------------------

namespace {
struct AsyncJob {
    GraphView* view;
    uint64_t key, gen, hash;
    GraphKind kind;
    Program f, g;      // copies: the analysis may change while the job runs
    double t0, t1;
    Viewport vp;
    CurvePoints result;
};
}

void GraphView::scheduleAsync(uint64_t key, const GraphPiece& pc, GraphKind kind, double t0, double t1,
                              uint64_t hash) {
    if (m_inflight.count(key)) return;  // one job per curve; a newer view is picked up when it lands
    auto* job = new AsyncJob{this, key, ++m_generation, hash, kind, pc.f, pc.g, t0, t1, vp, {}};
    m_inflight[key] = job->gen;
    dispatch_async_f(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), job, asyncWork);
}

void GraphView::asyncWork(void* ctx) {
    auto* job = static_cast<AsyncJob*>(ctx);
    auto t0 = std::chrono::steady_clock::now();
    switch (job->kind) {
    case GraphKind::ExplicitY: sampleExplicit(job->f, job->vp, false, job->result); break;
    case GraphKind::ExplicitX: sampleExplicit(job->f, job->vp, true, job->result); break;
    case GraphKind::Polar: sampleParametric(job->f, nullptr, job->t0, job->t1, true, job->vp, job->result); break;
    case GraphKind::Parametric: sampleParametric(job->f, &job->g, job->t0, job->t1, false, job->vp, job->result); break;
    default: break;
    }
    job->result.hash = job->hash;
    job->result.costMs = msSince(t0);
    dispatch_async_f(dispatch_get_main_queue(), job, asyncDone);
}

void GraphView::asyncDone(void* ctx) {
    auto* job = static_cast<AsyncJob*>(ctx);
    GraphView& v = *job->view;
    auto it = v.m_inflight.find(job->key);
    if (it != v.m_inflight.end() && it->second == job->gen) {
        v.m_inflight.erase(it);
        auto c = v.m_cache.find(job->key);
        if (c != v.m_cache.end()) c->second = std::move(job->result);  // still on screen: swap in
        if (v.onAsyncUpdate) v.onAsyncUpdate();
    }
    delete job;
}

CursorKind GraphView::cursorAt(float x, float y) const {
    if (m_panning) return CursorKind::Grabbing;
    if (buttonAt(x, y) != Button::None || settingAt(x, y) >= 0) return CursorKind::Pointer;
    if (m_hoverRow >= 0 || m_hoverPoi >= 0) return CursorKind::Pointer;
    return CursorKind::Arrow;
}

} // namespace calc
