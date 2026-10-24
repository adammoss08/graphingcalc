#include "render/TextAtlas.hpp"
#include "math/MathTree.hpp"

#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>

#include <cmath>
#include <vector>

namespace calc {

static const char* kFontNames[int(Font::Count)] = {
    "ArialMT", "Arial-BoldMT", "TimesNewRomanPS-ItalicMT", "TimesNewRomanPSMT", "HelveticaNeue-Light",
};

TextAtlas::TextAtlas() = default;

TextAtlas::~TextAtlas() {
    if (m_tex) m_tex->release();
    for (auto& row : m_fonts)
        for (void* f : row)
            if (f) CFRelease(f);
}

void TextAtlas::init(MTL::Device* dev) {
    auto* d = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatR8Unorm, kSize, kSize, false);
    d->setStorageMode(MTL::StorageModeShared);
    d->setUsage(MTL::TextureUsageShaderRead);
    m_tex = dev->newTexture(d);
    reset();
}

void TextAtlas::reset() {
    std::vector<uint8_t> zeros(size_t(kSize) * kSize, 0);
    m_tex->replaceRegion(MTL::Region(0, 0, kSize, kSize), 0, zeros.data(), kSize);
    m_glyphs.clear();
    m_shelfX = m_shelfY = m_shelfH = 0;
}

void TextAtlas::setScale(float s) {
    if (s != m_scale) {
        m_scale = s;
        reset();
    }
}

void* TextAtlas::fontRef(Font f, int px) {
    int bucket = std::min(px, 63);
    void*& slot = m_fonts[int(f)][bucket];
    if (px < 64 && slot) return slot;
    CFStringRef name = CFStringCreateWithCString(nullptr, kFontNames[int(f)], kCFStringEncodingUTF8);
    CTFontRef font = CTFontCreateWithName(name, px, nullptr);
    CFRelease(name);
    if (px < 64) slot = (void*)font;
    return (void*)font;
}

Glyph TextAtlas::rasterize(Font f, char32_t c, int px) {
    CTFontRef base = (CTFontRef)fontRef(f, px);
    UniChar chars[2];
    CFIndex n = 1;
    if (c > 0xFFFF) {
        char32_t v = c - 0x10000;
        chars[0] = UniChar(0xD800 + (v >> 10));
        chars[1] = UniChar(0xDC00 + (v & 0x3FF));
        n = 2;
    } else {
        chars[0] = UniChar(c);
    }
    CGGlyph glyphs[2] = {0, 0};
    CTFontRef font = base;
    bool ownFont = false;
    if (!CTFontGetGlyphsForCharacters(base, chars, glyphs, n) || glyphs[0] == 0) {
        CFStringRef str = CFStringCreateWithCharacters(nullptr, chars, n);
        font = CTFontCreateForString(base, str, CFRangeMake(0, n));
        CFRelease(str);
        ownFont = true;
        CTFontGetGlyphsForCharacters(font, chars, glyphs, n);
    }
    CGRect bounds = CTFontGetBoundingRectsForGlyphs(font, kCTFontOrientationDefault, glyphs, nullptr, 1);
    CGSize adv;
    CTFontGetAdvancesForGlyphs(font, kCTFontOrientationDefault, glyphs, &adv, 1);

    Glyph g;
    g.advance = float(adv.width);
    int minX = int(std::floor(bounds.origin.x)), minY = int(std::floor(bounds.origin.y));
    int maxX = int(std::ceil(bounds.origin.x + bounds.size.width));
    int maxY = int(std::ceil(bounds.origin.y + bounds.size.height));
    int w = std::max(0, maxX - minX) + 2 * kPad;
    int h = std::max(0, maxY - minY) + 2 * kPad;
    g.left = float(minX - kPad);
    g.top = float(minY - kPad + h);
    g.w = float(w);
    g.h = float(h);

    if (m_shelfX + w > kSize) { m_shelfX = 0; m_shelfY += m_shelfH; m_shelfH = 0; }
    if (m_shelfY + h > kSize) {
        // Atlas full: start over (glyphs re-rasterise lazily).
        reset();
    }
    int ax = m_shelfX, ay = m_shelfY;
    m_shelfX += w;
    m_shelfH = std::max(m_shelfH, h);

    std::vector<uint8_t> pixels(size_t(w) * h, 0);
    CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
    CGContextRef ctx = CGBitmapContextCreate(pixels.data(), w, h, 8, w, gray, kCGImageAlphaNone);
    CGColorSpaceRelease(gray);
    CGContextSetGrayFillColor(ctx, 1.0, 1.0);
    CGContextSetAllowsAntialiasing(ctx, true);
    CGContextSetShouldAntialias(ctx, true);
    CGContextSetAllowsFontSmoothing(ctx, false);
    CGContextSetShouldSmoothFonts(ctx, false);
    CGContextSetShouldSubpixelPositionFonts(ctx, false);
    CGPoint pos = CGPointMake(kPad - minX, kPad - minY);
    CTFontDrawGlyphs(font, glyphs, &pos, 1, ctx);
    CGContextRelease(ctx);
    if (ownFont) CFRelease(font);
    if (px >= 64) CFRelease(base);  // not cached (see fontRef)

    m_tex->replaceRegion(MTL::Region(ax, ay, w, h), 0, pixels.data(), w);
    g.u0 = float(ax) / kSize;
    g.v0 = float(ay) / kSize;
    g.u1 = float(ax + w) / kSize;
    g.v1 = float(ay + h) / kSize;
    return g;
}

const Glyph& TextAtlas::glyph(Font f, char32_t c, float sizePt) {
    int px = std::max(1, int(std::lround(sizePt * m_scale)));
    uint64_t key = (uint64_t(f) << 56) | (uint64_t(px) << 32) | uint64_t(c);
    auto it = m_glyphs.find(key);
    if (it != m_glyphs.end()) return it->second;
    Glyph g = rasterize(f, c, px);
    return m_glyphs.emplace(key, g).first->second;
}

float TextAtlas::measure(Font f, const std::u32string& s, float sizePt) {
    float w = 0;
    for (char32_t c : s) w += glyph(f, c, sizePt).advance;
    return w / m_scale;
}

float TextAtlas::measure(Font f, const std::string& utf8, float sizePt) {
    return measure(f, fromUtf8(utf8), sizePt);
}

FontMetrics TextAtlas::metrics(Font f, float sizePt) {
    CTFontRef font = (CTFontRef)fontRef(f, std::max(1, int(std::lround(sizePt * m_scale))));
    float px = float(CTFontGetSize(font));
    float k = sizePt / px;
    FontMetrics m{float(CTFontGetAscent(font)) * k, float(CTFontGetDescent(font)) * k,
                  float(CTFontGetXHeight(font)) * k};
    if (CTFontGetSize(font) >= 64) CFRelease(font);
    return m;
}

} // namespace calc
