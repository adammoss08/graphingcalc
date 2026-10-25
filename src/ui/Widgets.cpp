#include "ui/Widgets.hpp"

#include <cmath>

namespace calc::widgets {

using namespace theme;

void drawIcon(Batch2D& b, Icon icon, float cx, float cy, float size, Color c) {
    float s = size * 0.5f;
    float t = IconStroke;
    switch (icon) {
    case Icon::Plus:
        b.line(cx - s, cy, cx + s, cy, t, c);
        b.line(cx, cy - s, cx, cy + s, t, c);
        break;
    case Icon::Minus:
        b.line(cx - s, cy, cx + s, cy, t, c);
        break;
    case Icon::Close: {
        float k = s * 0.75f;
        b.line(cx - k, cy - k, cx + k, cy + k, t, c);
        b.line(cx - k, cy + k, cx + k, cy - k, t, c);
        break;
    }
    case Icon::Home: {
        float w = s * 0.8f;
        b.line(cx - s, cy - 0.05f * s, cx, cy - s, t, c);
        b.line(cx, cy - s, cx + s, cy - 0.05f * s, t, c);
        b.line(cx - w, cy - 0.2f * s, cx - w, cy + s, t, c);
        b.line(cx + w, cy - 0.2f * s, cx + w, cy + s, t, c);
        b.line(cx - w, cy + s, cx + w, cy + s, t, c);
        break;
    }
    case Icon::Undo: case Icon::Redo: {
        float dir = icon == Icon::Undo ? -1.f : 1.f;
        // arc from the arrow tip around to the bottom
        const int n = 10;
        float px = 0, py = 0;
        for (int i = 0; i <= n; ++i) {
            float a = -1.9f + 3.0f * i / n;  // radians, sweep under the centre
            float x = cx - dir * std::cos(a) * s * 0.75f, y = cy + 0.15f * s + std::sin(a) * s * 0.6f;
            if (i) b.line(px, py, x, y, t, c);
            px = x; py = y;
        }
        float tipX = cx - dir * std::cos(-1.9f) * s * 0.75f, tipY = cy + 0.15f * s + std::sin(-1.9f) * s * 0.6f;
        b.line(tipX, tipY, tipX + dir * 0.45f * s, tipY - 0.35f * s, t, c);
        b.line(tipX, tipY, tipX + dir * 0.45f * s, tipY + 0.4f * s, t, c);
        break;
    }
    case Icon::Wrench: {
        // handle + open jaw
        b.line(cx - 0.8f * s, cy + 0.8f * s, cx + 0.15f * s, cy - 0.15f * s, t * 1.5f, c);
        float jx = cx + 0.4f * s, jy = cy - 0.4f * s, r = 0.42f * s;
        const int n = 8;
        float px = 0, py = 0;
        for (int i = 0; i <= n; ++i) {
            float a = 0.9f + 4.4f * i / n;
            float x = jx + std::cos(a) * r, y = jy + std::sin(a) * r;
            if (i) b.line(px, py, x, y, t, c);
            px = x; py = y;
        }
        break;
    }
    case Icon::CollapseLeft: case Icon::ExpandRight: {
        float dir = icon == Icon::CollapseLeft ? 1.f : -1.f;
        for (float off : {-0.35f * s, 0.35f * s}) {
            float x = cx + off;
            b.line(x + dir * 0.3f * s, cy - 0.5f * s, x - dir * 0.3f * s, cy, t, c);
            b.line(x - dir * 0.3f * s, cy, x + dir * 0.3f * s, cy + 0.5f * s, t, c);
        }
        break;
    }
    case Icon::Warning: {
        b.line(cx, cy - s, cx - s, cy + 0.75f * s, t, c);
        b.line(cx - s, cy + 0.75f * s, cx + s, cy + 0.75f * s, t, c);
        b.line(cx + s, cy + 0.75f * s, cx, cy - s, t, c);
        b.line(cx, cy - 0.35f * s, cx, cy + 0.2f * s, t, c);
        b.circle(cx, cy + 0.48f * s, t * 0.6f, c);
        break;
    }
    case Icon::Keyboard: {
        Rect k{cx - s * 1.15f, cy - s * 0.7f, s * 2.3f, s * 1.4f};
        b.rect(k, Color{0, 0, 0, 0}, 2, t, c);
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 4; ++col)
                b.rect({k.x + 3 + col * (k.w - 6) / 4, k.y + 3 + row * 4.5f, 2.2f, 2.2f}, c, 0.5f);
        b.line(k.x + 5, k.bottom() - 3.5f, k.right() - 5, k.bottom() - 3.5f, t, c);
        break;
    }
    case Icon::Play:
        b.line(cx - 0.4f * s, cy - 0.6f * s, cx + 0.6f * s, cy, t, c);
        b.line(cx + 0.6f * s, cy, cx - 0.4f * s, cy + 0.6f * s, t, c);
        b.line(cx - 0.4f * s, cy + 0.6f * s, cx - 0.4f * s, cy - 0.6f * s, t, c);
        break;
    }
}

static Color overlayFor(Interaction s) {
    switch (s) {
    case Interaction::Hovered: return HoverOverlay;
    case Interaction::Pressed: return PressedOverlay;
    default: return Color{0, 0, 0, 0};
    }
}

void iconButton(Batch2D& b, const Rect& r, Icon icon, Interaction s) {
    if (s == Interaction::Hovered || s == Interaction::Pressed) b.rect(r, overlayFor(s), IconButtonRadius);
    Color c = s == Interaction::Disabled ? IconColor.withAlpha(0.25f) : IconColor;
    drawIcon(b, icon, r.x + r.w * 0.5f, r.y + r.h * 0.5f, 14, c);
}

void floatingButton(Batch2D& b, const Rect& r, Icon icon, Interaction s, bool roundTop, bool roundBottom) {
    float rad = ZoomButtonRadius;
    if (roundTop && roundBottom) b.shadow({r.x, r.y + 1, r.w, r.h}, rad, ShadowBlur, ShadowColor);
    // Square corners where stacked: draw a rounded rect plus square patches.
    b.rect(r, White, rad, 1, ZoomButtonBorder);
    if (!roundTop) b.rect({r.x, r.y - 1, r.w, rad + 1}, White, 0, 0);
    if (!roundBottom) b.rect({r.x, r.bottom() - rad, r.w, rad + 1}, White, 0, 0);
    if (!roundTop || !roundBottom) {
        // side borders through the squared corners
        b.line(r.x + 0.5f, r.y, r.x + 0.5f, r.bottom(), 1, ZoomButtonBorder);
        b.line(r.right() - 0.5f, r.y, r.right() - 0.5f, r.bottom(), 1, ZoomButtonBorder);
    }
    if (s == Interaction::Hovered || s == Interaction::Pressed)
        b.rect(r.inset(1), overlayFor(s), roundTop && roundBottom ? rad - 1 : 0);
    drawIcon(b, icon, r.x + r.w * 0.5f, r.y + r.h * 0.5f, 14, IconColor);
}

void floatingTextButton(Batch2D& b, const Rect& r, const std::string& label, Interaction s) {
    b.shadow({r.x, r.y + 1, r.w, r.h}, ZoomButtonRadius, ShadowBlur, ShadowColor);
    b.rect(r, White, ZoomButtonRadius, 1, ZoomButtonBorder);
    if (s == Interaction::Hovered || s == Interaction::Pressed) b.rect(r.inset(1), overlayFor(s), ZoomButtonRadius - 1);
    float w = b.atlas().measure(Font::Ui, label, ModeButtonTextSize);
    b.text(Font::Ui, ModeButtonTextSize, r.x + (r.w - w) * 0.5f, r.y + r.h * 0.5f + ModeButtonTextSize * 0.36f, label,
           IconColor);
}

float pillButton(Batch2D& b, float x, float y, const std::string& label, Interaction s, bool measureOnly) {
    float size = SliderLabelSize;
    float w = b.atlas().measure(Font::Ui, label, size) + 16;
    if (measureOnly) return w;
    Rect r{x, y, w, SuggestionHeight - 4};
    Color bg = s == Interaction::Pressed ? Accent.mix(Black, 0.15f) : s == Interaction::Hovered ? Accent.mix(Black, 0.08f) : Accent;
    b.rect(r, bg, r.h * 0.5f);
    b.text(Font::Ui, size, x + 8, y + r.h * 0.5f + size * 0.35f, label, White);
    return w;
}

void tooltip(Batch2D& b, float x, float y, const std::string& text) {
    float w = b.atlas().measure(Font::Ui, text, TooltipTextSize) + 2 * TooltipPadX;
    float h = TooltipTextSize + 2 * TooltipPadY;
    b.rect({x, y, w, h}, TooltipBg, TooltipRadius);
    b.text(Font::Ui, TooltipTextSize, x + TooltipPadX, y + TooltipPadY + TooltipTextSize * 0.8f, text, TooltipText);
}

} // namespace calc::widgets
