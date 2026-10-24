#pragma once
// CoreText glyph rasteriser + shelf-packed R8 atlas texture. Glyphs are rasterised once per
// (font, code point, pixel size) at device resolution and cached.
#include "ui/Theme.hpp"

#include <Metal/Metal.hpp>

#include <string>
#include <unordered_map>

namespace calc {

struct Glyph {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    float left = 0, top = 0;    // bitmap top-left relative to pen/baseline (pixels, y up for top)
    float w = 0, h = 0;         // bitmap size (pixels), includes padding
    float advance = 0;          // pixels
};

struct FontMetrics { float ascent, descent, xHeight; };  // points

class TextAtlas {
public:
    static constexpr int kSize = 2048;
    static constexpr int kPad = 4;

    TextAtlas();
    ~TextAtlas();
    void init(MTL::Device* dev);
    void setScale(float devicePixelsPerPoint);
    float scale() const { return m_scale; }

    const Glyph& glyph(Font f, char32_t c, float sizePt);
    float advance(Font f, char32_t c, float sizePt) { return glyph(f, c, sizePt).advance / m_scale; }
    float measure(Font f, const std::u32string& s, float sizePt);
    float measure(Font f, const std::string& utf8, float sizePt);
    FontMetrics metrics(Font f, float sizePt);
    MTL::Texture* texture() const { return m_tex; }

private:
    MTL::Texture* m_tex = nullptr;
    float m_scale = 2;
    int m_shelfX = 0, m_shelfY = 0, m_shelfH = 0;
    std::unordered_map<uint64_t, Glyph> m_glyphs;
    void* m_fonts[int(Font::Count)][64] = {};   // CTFontRef cache by pixel size bucket
    void* fontRef(Font f, int px);
    Glyph rasterize(Font f, char32_t c, int px);
    void reset();
};

} // namespace calc
