#include "ui/Theme.hpp"

#include <utility>
#include <vector>

namespace calc::theme {

namespace {

bool g_dark = false;

// Every token that differs in dark mode, with its dark value. The light values are whatever the
// tokens were initialised with (captured on the first call).
std::vector<std::pair<Color*, Color>> darkTable() {
    return {
        {&Paper, Color::hex(0x1E1F21)},
        {&Ink, Color::hex(0xFFFFFF)},
        {&GraphBg, Color::hex(0x1E1F21)},
        {&Text, Color::hex(0xE8E8E8)},
        {&TextSecondary, Color::hex(0xFFFFFF, 0.62f)},
        {&TextMuted, Color::hex(0x8C8C8C)},
        {&Border, Color::hex(0xFFFFFF, 0.2f)},
        {&RowBorder, Color::hex(0x3A3B3E)},
        {&TabBg, Color::hex(0x2A2B2E)},
        {&TabBorder, Color::hex(0xFFFFFF, 0.06f)},
        {&HoverOverlay, Color::hex(0xFFFFFF, 0.08f)},
        {&PressedOverlay, Color::hex(0xFFFFFF, 0.15f)},
        {&PoiGray, Color::hex(0xA0A0A0)},
        {&LabelHanging, Color::hex(0x7A7A7A)},
        {&TopBarTop, Color::hex(0x313235)},
        {&TopBarBottom, Color::hex(0x28292C)},
        {&IconColor, Color::hex(0xFFFFFF, 0.85f)},
        {&TableHeaderBg, Color::hex(0x2A2B2E)},
        {&TableGrid, Color::hex(0x3E3F42)},
        {&ComputedText, Color::hex(0xFFFFFF, 0.5f)},
        {&Placeholder, Color::hex(0xFFFFFF, 0.35f)},
        {&CursorColor, Color::hex(0xFFFFFF)},
        {&SelectionColor, Color::hex(0x2F5A8F)},
        {&GhostBracket, Color::hex(0xFFFFFF, 0.25f)},
        {&ZoomButtonBorder, Color::hex(0xFFFFFF, 0.18f)},
        {&ShadowColor, Color::hex(0x000000, 0.45f)},
        {&TooltipBg, Color::hex(0x000000, 0.85f)},
        {&EmptyBlock, Color::hex(0xFFFFFF, 0.14f)},
        {&FolderIcon, Color::hex(0xFFFFFF, 0.6f)},
        {&EvalBg, Color::hex(0xFFFFFF, 0.06f)},
        {&SliderTrack, Color::hex(0xFFFFFF, 0.25f)},
        {&DeleteIcon, Color::hex(0xFFFFFF, 0.5f)},
        {&KeypadBg, Color::hex(0x2B2C30)},
        {&KeyBg, Color::hex(0x45474C)},
        {&DarkKeyBg, Color::hex(0x36383C)},
        {&KeyShadow, Color::hex(0x000000, 0.4f)},
    };
}

} // namespace

void setDarkMode(bool on) {
    static std::vector<std::pair<Color*, Color>> dark = darkTable();
    static std::vector<std::pair<Color*, Color>> light = [] {
        std::vector<std::pair<Color*, Color>> l;
        for (auto& [tok, c] : dark) l.push_back({tok, *tok});
        return l;
    }();
    for (auto& [tok, c] : on ? dark : light) *tok = c;
    g_dark = on;
}

bool darkMode() { return g_dark; }

Color onPaper(Color c) {
    if (!g_dark) return c;
    float luma = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
    return luma < 0.12f ? Color{0.9f, 0.9f, 0.9f, c.a} : c;
}

} // namespace calc::theme
