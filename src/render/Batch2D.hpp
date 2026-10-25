#pragma once
// Immediate-mode builder for all 2D primitives in a frame. Everything ends up in one instance buffer,
// drawn in order by the UI pipeline (ranges let the renderer interleave curves).
#include "render/ShaderTypes.hpp"
#include "render/TextAtlas.hpp"
#include "ui/Theme.hpp"

#include <string>
#include <vector>

namespace calc {

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
};

class Batch2D {
public:
    explicit Batch2D(TextAtlas& atlas) : m_atlas(atlas) {}

    void clear() { m_items.clear(); m_clips.clear(); m_clip = {-1e6f, -1e6f, 1e6f, 1e6f}; }
    size_t size() const { return m_items.size(); }
    const std::vector<UiInstance>& items() const { return m_items; }
    TextAtlas& atlas() { return m_atlas; }

    void pushClip(const Rect& r);
    void popClip();

    void rect(const Rect& r, Color fill, float radius = 0, float borderWidth = 0, Color border = {});
    void gradient(const Rect& r, Color top, Color bottom);
    void line(float x0, float y0, float x1, float y1, float thickness, Color c);
    void circle(float cx, float cy, float radius, Color fill, float borderWidth = 0, Color border = {});
    void shadow(const Rect& r, float radius, float blur, Color c);

    // Text at a baseline. Returns the advance width in points. Glyphs snap to device pixels.
    float text(Font f, float sizePt, float x, float baseline, const std::u32string& s, Color c,
               float halo = 0, Color haloColor = theme::GraphBg);
    float text(Font f, float sizePt, float x, float baseline, const std::string& utf8, Color c,
               float halo = 0, Color haloColor = theme::GraphBg);
    void glyph(Font f, float sizePt, float x, float baseline, char32_t ch, Color c, float halo = 0,
               Color haloColor = theme::GraphBg);

private:
    TextAtlas& m_atlas;
    std::vector<UiInstance> m_items;
    std::vector<simd_float4> m_clips;
    simd_float4 m_clip = {-1e6f, -1e6f, 1e6f, 1e6f};

    UiInstance base(float kind) const;
};

inline simd_float4 toSimd(Color c) { return {c.r, c.g, c.b, c.a}; }

} // namespace calc
