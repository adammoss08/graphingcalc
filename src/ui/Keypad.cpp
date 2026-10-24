#include "ui/Keypad.hpp"
#include "ui/MathView.hpp"
#include "ui/Theme.hpp"
#include "ui/Widgets.hpp"

#include <algorithm>

namespace calc {

using namespace theme;

namespace {

constexpr Color kKeypadBg = Color::hex(0xDBDDE1);
constexpr Color kKeyBg = Color::hex(0xFFFFFF);
constexpr Color kDarkKeyBg = Color::hex(0xC8CBD1);
constexpr Color kKeyShadow = Color::hex(0x000000, 0.18f);
constexpr float kGap = 6;
constexpr float kKeyRadius = 5;
constexpr float kKeyLabel = 19;

using K = KeypadKey;

// Each page: rows of keys. Spans add up to the same total per row.
const std::vector<std::vector<K>>& pageKeys(int page) {
    static const std::vector<std::vector<K>> main = {
        {{K::Type, "x", "x"}, {K::Type, "y", "y"}, {K::Type, "a^{2}", "^2"}, {K::Type, "a^{b}", "^"},
         {K::Type, "7", "7"}, {K::Type, "8", "8"}, {K::Type, "9", "9"}, {K::Type, "\u00F7", "/", false},
         {K::Page, "functions", "", false, true, 2, 1}},
        {{K::Type, "(", "(", false}, {K::Type, ")", ")", false}, {K::Type, "<", "<"}, {K::Type, ">", ">"},
         {K::Type, "4", "4"}, {K::Type, "5", "5"}, {K::Type, "6", "6"}, {K::Type, "\\times", "*"},
         {K::Left, "←", "", false, true}, {K::Right, "→", "", false, true}},
        {{K::Type, "\\left|a\\right|", "|"}, {K::Type, ",", ","}, {K::Type, "\\le", "<="}, {K::Type, "\\ge", ">="},
         {K::Type, "1", "1"}, {K::Type, "2", "2"}, {K::Type, "3", "3"}, {K::Type, "-", "-"},
         {K::Backspace, "⌫", "", false, true, 2}},
        {{K::Page, "ABC", "", false, true, 1, 2}, {K::Type, "\\sqrt{}", "sqrt"}, {K::Type, "\\pi", "pi"},
         {K::Type, "\\theta", "theta"}, {K::Type, "0", "0"}, {K::Type, ".", "."}, {K::Type, "=", "="},
         {K::Type, "+", "+"}, {K::Enter, "↵", "", false, true, 2}},
    };
    static const std::vector<std::vector<K>> funcs = {
        {{K::Type, "\\sin", "sin("}, {K::Type, "\\cos", "cos("}, {K::Type, "\\tan", "tan("},
         {K::Type, "\\sin^{-1}", "arcsin("}, {K::Type, "\\cos^{-1}", "arccos("}, {K::Type, "\\tan^{-1}", "arctan("},
         {K::Type, "\\ln", "ln("}, {K::Type, "\\log", "log("}, {K::Page, "main", "", false, true, 2, 0}},
        {{K::Type, "\\csc", "csc("}, {K::Type, "\\sec", "sec("}, {K::Type, "\\cot", "cot("},
         {K::Type, "e^{x}", "e^"}, {K::Type, "10^{x}", "10^"}, {K::Type, "\\log_{a}", "log_"},
         {K::Type, "\\sqrt[n]{}", "nthroot"}, {K::Type, "\\sqrt[3]{}", "cbrt"},
         {K::Left, "←", "", false, true}, {K::Right, "→", "", false, true}},
        {{K::Type, "\\frac{d}{dx}", "d/dx"}, {K::Type, "f'", "'"}, {K::Type, "\\sum", "sum"},
         {K::Type, "\\prod", "prod"}, {K::Type, "\\int", "int"}, {K::Type, "n!", "!"},
         {K::Type, "\\operatorname{mod}", "mod("}, {K::Type, "\\operatorname{floor}", "floor("},
         {K::Backspace, "⌫", "", false, true, 2}},
        {{K::Type, "\\operatorname{round}", "round("}, {K::Type, "\\operatorname{ceil}", "ceil("},
         {K::Type, "\\min", "min("}, {K::Type, "\\max", "max("}, {K::Type, "\\operatorname{total}", "total("},
         {K::Type, "\\operatorname{mean}", "mean("}, {K::Type, "\\left[\\right]", "["}, {K::Type, "\\sim", "~"},
         {K::Enter, "↵", "", false, true, 2}},
    };
    static const std::vector<std::vector<K>> abc = [] {
        std::vector<std::vector<K>> rows(4);
        static const char* r1[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"};
        static const char* r2[] = {"a", "s", "d", "f", "g", "h", "j", "k", "l"};
        static const char* r3[] = {"z", "x", "c", "v", "b", "n", "m"};
        for (auto* c : r1) rows[0].push_back({K::Type, c, c});
        rows[1].push_back({K::Type, "\\theta", "theta", true, false, 0.5f});
        for (auto* c : r2) rows[1].push_back({K::Type, c, c});
        rows[1].push_back({K::Type, "_{a}", "_", true, true, 0.5f});
        rows[2].push_back({K::Type, "a^{b}", "^", true, true, 1.5f});
        for (auto* c : r3) rows[2].push_back({K::Type, c, c});
        rows[2].push_back({K::Backspace, "⌫", "", false, true, 1.5f});
        rows[3].push_back({K::Page, "123", "", false, true, 2, 0});
        rows[3].push_back({K::Type, "(", "(", false});
        rows[3].push_back({K::Type, ")", ")", false});
        rows[3].push_back({K::Type, "=", "="});
        rows[3].push_back({K::Type, ",", ","});
        rows[3].push_back({K::Left, "←", "", false, true});
        rows[3].push_back({K::Right, "→", "", false, true});
        rows[3].push_back({K::Enter, "↵", "", false, true, 2});
        return rows;
    }();
    return page == 1 ? funcs : page == 2 ? abc : main;
}

} // namespace

std::vector<Keypad::Placed> Keypad::layout(const Rect& area) const {
    std::vector<Placed> out;
    const auto& rows = pageKeys(page);
    float rowH = (area.h - kGap * (rows.size() + 1)) / rows.size();
    for (size_t r = 0; r < rows.size(); ++r) {
        float units = 0;
        for (auto& k : rows[r]) units += k.span;
        float unitW = (area.w - kGap * (rows[r].size() + 1)) / units;
        float x = area.x + kGap;
        float y = area.y + kGap + r * (rowH + kGap);
        for (auto& k : rows[r]) {
            float w = unitW * k.span + kGap * (k.span - 1);
            out.push_back({&k, Rect{x, y, w, rowH}});
            x += w + kGap;
        }
    }
    return out;
}

void Keypad::draw(Batch2D& b, const Rect& area) const {
    if (!open) return;
    b.rect(area, kKeypadBg);
    b.line(area.x, area.y + 0.5f, area.right(), area.y + 0.5f, 1, Border);
    for (const Placed& p : layout(area)) {
        const KeypadKey& k = *p.key;
        bool pressed = m_pressed == p.key, hover = m_hover == p.key;
        Color bg = k.dark ? kDarkKeyBg : kKeyBg;
        if (pressed) bg = bg.mix(Black, 0.12f);
        else if (hover) bg = bg.mix(Black, 0.05f);
        b.rect({p.rect.x, p.rect.y + 1, p.rect.w, p.rect.h}, kKeyShadow, kKeyRadius);
        b.rect(p.rect, bg, kKeyRadius);
        float cx = p.rect.x + p.rect.w * 0.5f, cy = p.rect.y + p.rect.h * 0.5f;
        if (k.math) {
            float w = measureLatex(b.atlas(), k.label, kKeyLabel);
            drawLatex(b, k.label, Font::MathRoman, kKeyLabel, cx - w * 0.5f, cy + kKeyLabel * 0.3f, Text);
        } else {
            float size = std::string(k.label).size() > 3 ? 15.0f : 20.0f;
            float w = b.atlas().measure(Font::Ui, std::string(k.label), size);
            b.text(Font::Ui, size, cx - w * 0.5f, cy + size * 0.35f, std::string(k.label), Text);
        }
    }
}

const KeypadKey* Keypad::keyAt(const Rect& area, float x, float y) const {
    if (!open || !area.contains(x, y)) return nullptr;
    for (const Placed& p : layout(area))
        if (p.rect.contains(x, y)) return p.key;
    return nullptr;
}

bool Keypad::mouseMoved(const Rect& area, float x, float y) {
    const KeypadKey* h = keyAt(area, x, y);
    bool changed = h != m_hover;
    m_hover = h;
    return changed;
}

} // namespace calc
