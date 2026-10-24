#include "ui/RowModel.hpp"
#include "math/Latex.hpp"

#include <algorithm>

namespace calc {

std::string MathField::latex() const { return blockToLatex(tree.root()); }

void TextField::insert(const std::u32string& s) {
    if (hasSelection()) {
        int a = std::min(anchor, cursor), b = std::max(anchor, cursor);
        text.erase(a, b - a);
        cursor = a;
    }
    anchor = -1;
    text.insert(cursor, s);
    cursor += int(s.size());
}

bool TextField::backspace() {
    if (hasSelection()) { insert(U""); return true; }
    anchor = -1;
    if (cursor == 0) return false;
    text.erase(--cursor, 1);
    return true;
}

bool TextField::deleteForward() {
    if (hasSelection()) { insert(U""); return true; }
    anchor = -1;
    if (cursor >= int(text.size())) return false;
    text.erase(cursor, 1);
    return true;
}

void TextField::move(int delta, bool select) {
    if (select) { if (anchor < 0) anchor = cursor; }
    else if (hasSelection()) {
        cursor = delta < 0 ? std::min(anchor, cursor) : std::max(anchor, cursor);
        anchor = -1;
        return;
    } else {
        anchor = -1;
    }
    cursor = std::clamp(cursor + delta, 0, int(text.size()));
}

void TextField::home(bool select) {
    if (select) { if (anchor < 0) anchor = cursor; } else anchor = -1;
    cursor = 0;
}

void TextField::end(bool select) {
    if (select) { if (anchor < 0) anchor = cursor; } else anchor = -1;
    cursor = int(text.size());
}

const char* lineStyleName(LineStyle s) {
    switch (s) {
    case LineStyle::Dashed: return "DASHED";
    case LineStyle::Dotted: return "DOTTED";
    default: return "SOLID";
    }
}

LineStyle lineStyleFromName(const std::string& s) {
    if (s == "DASHED") return LineStyle::Dashed;
    if (s == "DOTTED") return LineStyle::Dotted;
    return LineStyle::Solid;
}

const char* pointStyleName(PointStyle s) {
    switch (s) {
    case PointStyle::Open: return "OPEN";
    case PointStyle::Cross: return "CROSS";
    default: return "POINT";
    }
}

PointStyle pointStyleFromName(const std::string& s) {
    if (s == "OPEN") return PointStyle::Open;
    if (s == "CROSS") return PointStyle::Cross;
    return PointStyle::Point;
}

} // namespace calc
