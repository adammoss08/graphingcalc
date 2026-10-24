#include "render/Batch2D.hpp"
#include "math/MathTree.hpp"

#include <algorithm>
#include <cmath>

namespace calc {

UiInstance Batch2D::base(float kind) const {
    UiInstance u{};
    u.params = {0, 0, kind, 0};
    u.clip = m_clip;
    return u;
}

void Batch2D::pushClip(const Rect& r) {
    m_clips.push_back(m_clip);
    simd_float4 c = {r.x, r.y, r.right(), r.bottom()};
    m_clip = {std::max(c.x, m_clip.x), std::max(c.y, m_clip.y), std::min(c.z, m_clip.z), std::min(c.w, m_clip.w)};
}

void Batch2D::popClip() {
    if (m_clips.empty()) return;
    m_clip = m_clips.back();
    m_clips.pop_back();
}

void Batch2D::rect(const Rect& r, Color fill, float radius, float borderWidth, Color border) {
    UiInstance u = base(UI_KIND_RECT);
    u.rect = {r.x, r.y, r.w, r.h};
    u.color = toSimd(fill);
    u.color2 = toSimd(border);
    u.params.x = radius;
    u.params.y = borderWidth;
    m_items.push_back(u);
}

void Batch2D::gradient(const Rect& r, Color top, Color bottom) {
    UiInstance u = base(UI_KIND_GRADIENT);
    u.rect = {r.x, r.y, r.w, r.h};
    u.color = toSimd(top);
    u.color2 = toSimd(bottom);
    m_items.push_back(u);
}

void Batch2D::line(float x0, float y0, float x1, float y1, float thickness, Color c) {
    UiInstance u = base(UI_KIND_LINE);
    u.rect = {x0, y0, x1, y1};
    u.color = toSimd(c);
    u.params.w = thickness;
    m_items.push_back(u);
}

void Batch2D::circle(float cx, float cy, float r, Color fill, float borderWidth, Color border) {
    rect({cx - r, cy - r, 2 * r, 2 * r}, fill, r, borderWidth, border);
}

void Batch2D::shadow(const Rect& r, float radius, float blur, Color c) {
    UiInstance u = base(UI_KIND_SHADOW);
    u.rect = {r.x, r.y, r.w, r.h};
    u.color = toSimd(c);
    u.params.x = radius;
    u.params.y = blur;
    m_items.push_back(u);
}

void Batch2D::glyph(Font f, float sizePt, float x, float baseline, char32_t ch, Color c, float halo,
                    Color haloColor) {
    const Glyph& g = m_atlas.glyph(f, ch, sizePt);
    if (g.w <= 2 * TextAtlas::kPad) return;  // whitespace
    float s = m_atlas.scale();
    // Snap the pen to device pixels so glyphs stay crisp.
    float px = std::round(x * s) + g.left;
    float py = std::round(baseline * s) - g.top;
    UiInstance u = base(UI_KIND_GLYPH);
    u.rect = {px / s, py / s, g.w / s, g.h / s};
    u.uv = {g.u0, g.v0, g.u1, g.v1};
    u.color = toSimd(c);
    u.color2 = toSimd(haloColor);
    u.params.y = halo;
    m_items.push_back(u);
}

float Batch2D::text(Font f, float sizePt, float x, float baseline, const std::u32string& str, Color c,
                    float halo, Color haloColor) {
    float pen = x;
    for (char32_t ch : str) {
        glyph(f, sizePt, pen, baseline, ch, c, halo, haloColor);
        pen += m_atlas.advance(f, ch, sizePt);
    }
    return pen - x;
}

float Batch2D::text(Font f, float sizePt, float x, float baseline, const std::string& utf8, Color c,
                    float halo, Color haloColor) {
    return text(f, sizePt, x, baseline, fromUtf8(utf8), c, halo, haloColor);
}

} // namespace calc
