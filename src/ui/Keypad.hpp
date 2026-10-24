#pragma once
// On-screen keypad (Desmos "show keypad"): a drawer across the bottom of the window with a main
// page (variables, digits, operators), a functions page and an ABC page. Keys feed the focused field.
#include "render/Batch2D.hpp"

#include <string>
#include <vector>

namespace calc {

struct KeypadKey {
    enum Action : uint8_t { Type, Left, Right, Backspace, Enter, Page, Close } action = Type;
    const char* label;      // LaTeX if math, else plain text
    const char* text;       // typed through the editor (auto-commands apply), for Type
    bool math = true;       // label is LaTeX
    bool dark = false;      // grey key (functions, navigation)
    float span = 1;         // width in key units
    int page = 0;           // target page, for Page
};

class Keypad {
public:
    bool open = false;
    int page = 0;   // 0 main, 1 functions, 2 ABC

    float height() const { return open ? kHeight : 0; }
    void draw(Batch2D& b, const Rect& area) const;
    const KeypadKey* keyAt(const Rect& area, float x, float y) const;
    void setPressed(const KeypadKey* k) { m_pressed = k; }
    bool mouseMoved(const Rect& area, float x, float y);

    static constexpr float kHeight = 232;

private:
    const KeypadKey* m_pressed = nullptr;
    const KeypadKey* m_hover = nullptr;
    struct Placed { const KeypadKey* key; Rect rect; };
    std::vector<Placed> layout(const Rect& area) const;
};

} // namespace calc
