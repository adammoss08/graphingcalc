#include "ui/Popovers.hpp"
#include "ui/Widgets.hpp"

#include <algorithm>
#include <cmath>

namespace calc {

using namespace theme;

namespace {

constexpr float kMenuWidth = 270;
constexpr float kPad = 14;
constexpr float kSwatch = 30;
constexpr float kRowH = 34;
constexpr float kRadius = 8;
constexpr float kMaxLineWidth = 10;
constexpr float kMinPointSize = 2, kMaxPointSize = 30;

float sliderT(float v, float lo, float hi) { return std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f); }

void drawTrack(Batch2D& b, const Rect& tr, float t, Color c) {
    float cy = tr.y + tr.h * 0.5f;
    b.rect({tr.x, cy - 1.5f, tr.w, 3}, Black.withAlpha(0.15f), 1.5f);
    b.rect({tr.x, cy - 1.5f, tr.w * t, 3}, c, 1.5f);
    b.circle(tr.x + tr.w * t, cy, 7, White, 1.5f, c);
}

void drawCheck(Batch2D& b, const Rect& box, bool on) {
    if (on) {
        b.rect(box, Accent, 3);
        b.line(box.x + 3.5f, box.y + 8.5f, box.x + 6.5f, box.y + 11.5f, 2, White);
        b.line(box.x + 6.5f, box.y + 11.5f, box.x + 12.5f, box.y + 4.5f, 2, White);
    } else {
        b.rect(box, White, 3, 1.5f, Black.withAlpha(0.35f));
    }
}

} // namespace

void StyleMenu::show(int r, int c, Rect a, bool lines, bool fill, bool points, bool pointToggles, bool labelToggle) {
    showLabelToggle = labelToggle;
    open = true;
    row = r;
    col = c;
    anchor = a;
    showLines = lines;
    showFill = fill;
    showPoints = points;
    showPointToggles = pointToggles;
    m_changed = false;
    m_drag = Drag::None;
}

StyleMenu::Layout StyleMenu::layout(float viewW, float viewH) const {
    Layout L{};
    float x = anchor.right() + 8, y = anchor.y - 6;
    float cy = kPad;
    // swatches
    for (int i = 0; i < ColorPaletteCount; ++i)
        L.swatches[i] = {kPad + i * (kSwatch + 10), cy, kSwatch, kSwatch};
    cy += kSwatch + 14;
    if (showPointToggles) {
        L.pointsToggle = {kPad, cy + 9, 16, 16};
        L.linesToggle = {kPad + 110, cy + 9, 16, 16};
        cy += kRowH;
    }
    if (showLines) {
        for (int i = 0; i < 3; ++i) L.lineStyles[i] = {kPad + i * 56, cy + 4, 48, 26};
        cy += kRowH + 4;
        L.widthTrack = {kPad + 70, cy + 7, kMenuWidth - 2 * kPad - 70, 20};
        cy += kRowH - 4;
        L.opacityTrack = {kPad + 70, cy + 7, kMenuWidth - 2 * kPad - 70, 20};
        cy += kRowH - 4;
    }
    if (showFill) {
        L.fillTrack = {kPad + 70, cy + 7, kMenuWidth - 2 * kPad - 70, 20};
        cy += kRowH - 4;
    }
    if (showPoints) {
        for (int i = 0; i < 3; ++i) L.pointStyles[i] = {kPad + i * 56, cy + 4, 48, 26};
        cy += kRowH + 4;
        L.sizeTrack = {kPad + 70, cy + 7, kMenuWidth - 2 * kPad - 70, 20};
        cy += kRowH - 4;
    }
    if (showLabelToggle) {
        L.labelToggle = {kPad, cy + 9, 16, 16};
        cy += kRowH;
    }
    L.height = cy + kPad - 6;
    // Keep the menu on screen.
    x = std::min(x, viewW - kMenuWidth - 8);
    y = std::clamp(y, 8.0f, std::max(8.0f, viewH - L.height - 8));
    L.box = {x, y, kMenuWidth, L.height};
    auto off = [&](Rect& r) { r.x += x; r.y += y; };
    for (auto& r : L.swatches) off(r);
    for (auto& r : L.lineStyles) off(r);
    for (auto& r : L.pointStyles) off(r);
    for (Rect* r : {&L.widthTrack, &L.opacityTrack, &L.fillTrack, &L.sizeTrack, &L.pointsToggle, &L.linesToggle, &L.labelToggle}) off(*r);
    return L;
}

Rect StyleMenu::rect(float viewW, float viewH) const { return layout(viewW, viewH).box; }

void StyleMenu::draw(Batch2D& b, const Style& s, float viewW, float viewH) const {
    if (!open) return;
    Layout L = layout(viewW, viewH);
    b.shadow({L.box.x, L.box.y + 3, L.box.w, L.box.h}, kRadius, 10, ShadowColor.withAlpha(0.25f));
    b.rect(L.box, White, kRadius, 1, Border);
    for (int i = 0; i < ColorPaletteCount; ++i) {
        const Rect& r = L.swatches[i];
        Color c = ColorPalette[i];
        bool selected = std::fabs(c.r - s.color.r) + std::fabs(c.g - s.color.g) + std::fabs(c.b - s.color.b) < 0.01f;
        float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
        if (selected) b.circle(cx, cy, r.w * 0.5f + 3, White, 2, c);
        b.circle(cx, cy, r.w * 0.5f - (selected ? 2 : 0), c);
    }
    auto label = [&](const Rect& track, const char* text) {
        b.text(Font::Ui, 13, track.x - 70, track.y + 14, std::string(text), TextSecondary);
    };
    if (showPointToggles) {
        drawCheck(b, L.pointsToggle, s.showPoints);
        b.text(Font::Ui, 14, L.pointsToggle.right() + 8, L.pointsToggle.y + 13, std::string("Points"), Text);
        drawCheck(b, L.linesToggle, s.showLines);
        b.text(Font::Ui, 14, L.linesToggle.right() + 8, L.linesToggle.y + 13, std::string("Lines"), Text);
    }
    if (showLines) {
        for (int i = 0; i < 3; ++i) {
            const Rect& r = L.lineStyles[i];
            bool on = int(s.line) == i;
            b.rect(r, on ? Accent.withAlpha(0.12f) : White, 5, 1, on ? Accent : Border);
            float cy = r.y + r.h * 0.5f, x0 = r.x + 8, x1 = r.right() - 8;
            if (i == 0) b.line(x0, cy, x1, cy, 2.5f, s.color);
            else if (i == 1) for (float x = x0; x < x1; x += 10) b.line(x, cy, std::min(x + 5, x1), cy, 2.5f, s.color);
            else for (float x = x0 + 1; x <= x1; x += 6) b.circle(x, cy, 1.4f, s.color);
        }
        label(L.widthTrack, "Thickness");
        drawTrack(b, L.widthTrack, sliderT(s.lineWidth, 0, kMaxLineWidth), s.color);
        label(L.opacityTrack, "Opacity");
        drawTrack(b, L.opacityTrack, s.lineOpacity, s.color);
    }
    if (showFill) {
        label(L.fillTrack, "Fill");
        drawTrack(b, L.fillTrack, s.fillOpacity, s.color);
    }
    if (showPoints) {
        for (int i = 0; i < 3; ++i) {
            const Rect& r = L.pointStyles[i];
            bool on = int(s.point) == i;
            b.rect(r, on ? Accent.withAlpha(0.12f) : White, 5, 1, on ? Accent : Border);
            float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
            if (i == 0) b.circle(cx, cy, 5, s.color);
            else if (i == 1) b.circle(cx, cy, 5, White, 2, s.color);
            else { b.line(cx - 4, cy - 4, cx + 4, cy + 4, 2, s.color); b.line(cx - 4, cy + 4, cx + 4, cy - 4, 2, s.color); }
        }
        label(L.sizeTrack, "Size");
        drawTrack(b, L.sizeTrack, sliderT(s.pointSize, kMinPointSize, kMaxPointSize), s.color);
    }
    if (showLabelToggle) {
        drawCheck(b, L.labelToggle, s.showLabel);
        b.text(Font::Ui, 14, L.labelToggle.right() + 8, L.labelToggle.y + 13, std::string("Label"), Text);
    }
}

void StyleMenu::willChange() {
    if (!m_changed && onWillChange) onWillChange();
    m_changed = true;
}

void StyleMenu::dragTo(Drag d, float x, Style& s, const Layout& L) {
    auto t = [&](const Rect& tr) { return std::clamp((x - tr.x) / tr.w, 0.0f, 1.0f); };
    switch (d) {
    case Drag::Width: s.lineWidth = std::max(0.5f, std::round(t(L.widthTrack) * kMaxLineWidth * 2) / 2); break;
    case Drag::Opacity: s.lineOpacity = std::round(t(L.opacityTrack) * 20) / 20; break;
    case Drag::Fill: s.fillOpacity = std::round(t(L.fillTrack) * 20) / 20; break;
    case Drag::PointSize: s.pointSize = std::round(kMinPointSize + t(L.sizeTrack) * (kMaxPointSize - kMinPointSize)); break;
    default: break;
    }
}

bool StyleMenu::mouseDown(float x, float y, Style& s, float viewW, float viewH) {
    if (!open) return false;
    Layout L = layout(viewW, viewH);
    if (!L.box.contains(x, y)) { open = false; return false; }
    for (int i = 0; i < ColorPaletteCount; ++i)
        if (L.swatches[i].inset(-3).contains(x, y)) { willChange(); s.color = ColorPalette[i]; return true; }
    if (showPointToggles) {
        if (L.pointsToggle.inset(-4).contains(x, y)) { willChange(); s.showPoints = !s.showPoints; return true; }
        if (L.linesToggle.inset(-4).contains(x, y)) { willChange(); s.showLines = !s.showLines; return true; }
    }
    if (showLines) {
        for (int i = 0; i < 3; ++i)
            if (L.lineStyles[i].contains(x, y)) { willChange(); s.line = LineStyle(i); return true; }
        if (L.widthTrack.inset(-4).contains(x, y)) { willChange(); m_drag = Drag::Width; dragTo(m_drag, x, s, L); return true; }
        if (L.opacityTrack.inset(-4).contains(x, y)) { willChange(); m_drag = Drag::Opacity; dragTo(m_drag, x, s, L); return true; }
    }
    if (showFill && L.fillTrack.inset(-4).contains(x, y)) { willChange(); m_drag = Drag::Fill; dragTo(m_drag, x, s, L); return true; }
    if (showPoints) {
        for (int i = 0; i < 3; ++i)
            if (L.pointStyles[i].contains(x, y)) { willChange(); s.point = PointStyle(i); return true; }
        if (L.sizeTrack.inset(-4).contains(x, y)) { willChange(); m_drag = Drag::PointSize; dragTo(m_drag, x, s, L); return true; }
    }
    if (showLabelToggle && L.labelToggle.inset(-4).contains(x, y)) { willChange(); s.showLabel = !s.showLabel; return true; }
    return true;
}

bool StyleMenu::mouseDragged(float x, float y, Style& s, float viewW, float viewH) {
    if (!open || m_drag == Drag::None) return false;
    dragTo(m_drag, x, s, layout(viewW, viewH));
    return true;
}

bool StyleMenu::mouseMoved(float x, float y, float viewW, float viewH) {
    int h = open && layout(viewW, viewH).box.contains(x, y) ? 1 : -1;
    bool changed = h != m_hover;
    m_hover = h;
    return changed;
}

// ---- add menu ----------------------------------------------------------------------------------

namespace {
constexpr float kAddWidth = 170;
constexpr float kAddItemH = 36;
const char* kAddLabels[AddMenu::Count] = {"expression", "note", "table", "folder", "image"};
}

Rect AddMenu::rect() const { return {anchor.x, anchor.bottom() + 4, kAddWidth, kAddItemH * int(Count) + 8}; }

int AddMenu::itemAt(float x, float y) const {
    Rect r = rect();
    if (!open || !r.contains(x, y)) return -1;
    int i = int((y - r.y - 4) / kAddItemH);
    return i >= 0 && i < Count ? i : -1;
}

bool AddMenu::mouseMoved(float x, float y) {
    int h = itemAt(x, y);
    bool changed = h != m_hover;
    m_hover = h;
    return changed;
}

void AddMenu::draw(Batch2D& b) const {
    if (!open) return;
    Rect r = rect();
    b.shadow({r.x, r.y + 3, r.w, r.h}, 6, 10, ShadowColor.withAlpha(0.25f));
    b.rect(r, White, 6, 1, Border);
    for (int i = 0; i < Count; ++i) {
        Rect it{r.x + 4, r.y + 4 + i * kAddItemH, r.w - 8, kAddItemH};
        if (i == m_hover) b.rect(it, HoverOverlay, 4);
        float cx = it.x + 18, cy = it.y + it.h * 0.5f;
        switch (i) {
        case Expression: b.circle(cx, cy, 7, CurveRed); break;
        case Note: b.text(Font::UiBold, 18, cx - 5, cy + 8, std::string("“"), IconColor); break;
        case Table:
            b.rect({cx - 8, cy - 7, 16, 14}, Color{0, 0, 0, 0}, 1, 1.5f, IconColor);
            b.line(cx - 8, cy - 2, cx + 8, cy - 2, 1.5f, IconColor);
            b.line(cx, cy - 7, cx, cy + 7, 1.5f, IconColor);
            break;
        case Folder:
            b.rect({cx - 8, cy - 4, 16, 11}, Color{0, 0, 0, 0}, 2, 1.5f, IconColor);
            b.rect({cx - 8, cy - 7, 7, 4}, Color{0, 0, 0, 0}, 1, 1.5f, IconColor);
            break;
        case Image:
            b.rect({cx - 8, cy - 6, 16, 12}, Color{0, 0, 0, 0}, 2, 1.5f, IconColor);
            b.line(cx - 5, cy + 3, cx - 1, cy - 1, 1.5f, IconColor);
            b.line(cx - 1, cy - 1, cx + 5, cy + 3, 1.5f, IconColor);
            break;
        }
        b.text(Font::Ui, 15, it.x + 40, cy + 5, std::string(kAddLabels[i]), Text);
    }
}

} // namespace calc
