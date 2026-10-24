// Tables: a header row of column names (x_1, y_1, ...) and a grid of value cells. The first column is
// the independent variable; every other column is plotted against it as points. A header that is an
// expression of the first column (e.g. x_1^2) makes a computed column (read-only, grey values).
// There is always one empty row at the bottom (and, while focused, an empty column on the right);
// typing into them makes them real.
#include "ui/ExpressionList.hpp"
#include "math/MathTree.hpp"

#include <algorithm>
#include <cmath>

namespace calc {

using namespace theme;

namespace {
constexpr float kTableTopPad = 10;
constexpr float kCellPad = 8;
constexpr float kColumnIconRadius = 7;
constexpr float kIconSpace = 20;

bool cursorAtStart(const MathTree& t) { return t.cursor().block == &t.root() && t.cursor().index == 0 && !t.hasSelection(); }
bool cursorAtEnd(const MathTree& t) {
    return t.cursor().block == &t.root() && t.cursor().index == int(t.root().nodes.size()) && !t.hasSelection();
}
}

int ExpressionList::tableRowCount(const ExpressionRow& r) const {
    size_t n = 0;
    for (auto& c : r.columns) n = std::max(n, c.cells.size());
    return int(n);
}

void ExpressionList::trimTable(ExpressionRow& r, bool keepFocused) {
    int row = int(&r - rows.data());
    bool focusedHere = keepFocused && focus.row == row;
    // Trailing empty rows.
    while (true) {
        int n = tableRowCount(r);
        if (n == 0) break;
        if (focusedHere && focus.field == Field::TableCell && focus.cell >= n - 1) break;
        bool empty = true;
        for (auto& c : r.columns)
            if (int(c.cells.size()) >= n && !c.cells[n - 1].empty()) { empty = false; break; }
        if (!empty) break;
        for (auto& c : r.columns) if (int(c.cells.size()) >= n) c.cells.pop_back();
    }
    // Trailing empty columns (keep at least two).
    while (r.columns.size() > 2) {
        int last = int(r.columns.size()) - 1;
        if (focusedHere && focus.col >= last) break;
        auto& c = r.columns.back();
        bool empty = c.header.empty();
        for (auto& cell : c.cells) empty = empty && cell.empty();
        if (!empty) break;
        r.columns.pop_back();
    }
}

float ExpressionList::layoutTable(int i, std::vector<FieldBox>& boxes) {
    ExpressionRow& r = rows[i];
    trimTable(r, true);
    bool focusedHere = focus.row == i;
    int ncols = int(r.columns.size()) + (focusedHere ? 1 : 0);
    int nrows = tableRowCount(r) + 1;
    float x0 = contentX(r) - 2;
    float em = TableTextSize;
    for (int c = 0; c < ncols; ++c) {
        float cx = x0 + c * TableColumnWidth;
        float iconSpace = c > 0 && c < int(r.columns.size()) ? kIconSpace : 0;
        float hb = kTableTopPad + TableHeaderHeight * 0.5f + em * 0.3f;
        boxes.push_back({Field::TableHeader, c, 0,
                         Rect{cx + kCellPad + iconSpace, kTableTopPad, TableColumnWidth - kCellPad - iconSpace, TableHeaderHeight},
                         hb, em});
        bool computed = c < int(r.columns.size()) && r.columns[c].computed;
        for (int k = 0; k < nrows; ++k) {
            if (computed) break;  // computed cells aren't editable
            float cy = kTableTopPad + TableHeaderHeight + k * TableCellHeight;
            boxes.push_back({Field::TableCell, c, k, Rect{cx + kCellPad, cy, TableColumnWidth - kCellPad, TableCellHeight},
                             cy + TableCellHeight * 0.5f + em * 0.3f, em});
        }
    }
    r.mathBaseline = kTableTopPad + TableHeaderHeight * 0.5f;
    return kTableTopPad + TableHeaderHeight + nrows * TableCellHeight + 14;
}

void ExpressionList::drawTable(Batch2D& b, int i, float top, bool sel, bool cursorVisible) const {
    const ExpressionRow& r = rows[i];
    int ncols = int(r.columns.size()) + (focus.row == i ? 1 : 0);
    int nrows = tableRowCount(r) + 1;
    float x0 = contentX(r) - 2;
    float y0 = top + kTableTopPad;
    float w = ncols * TableColumnWidth, h = TableHeaderHeight + nrows * TableCellHeight;
    b.pushClip({contentX(r) - 4, top, area.right() - contentX(r) + 4, r.h});
    b.rect({x0, y0, w, TableHeaderHeight}, TableHeaderBg);
    // Computed columns' cells (grey values).
    for (int c = 0; c < int(r.columns.size()); ++c) {
        const TableColumn& col = r.columns[c];
        if (!col.computed) continue;
        for (size_t k = 0; k < col.computedValues.size(); ++k) {
            double v = col.computedValues[k];
            if (!std::isfinite(v)) continue;
            float cy = y0 + TableHeaderHeight + k * TableCellHeight;
            drawLatex(b, formatNumber(v), Font::MathRoman, TableTextSize, x0 + c * TableColumnWidth + kCellPad,
                      cy + TableCellHeight * 0.5f + TableTextSize * 0.3f, ComputedText);
        }
    }
    for (const FieldBox& fb : m_boxes[i]) {
        bool placeholderCol = fb.col >= int(r.columns.size());
        drawFieldBox(b, i, fb, top, cursorVisible, placeholderCol ? Placeholder : Text);
    }
    // Grid
    for (int c = 0; c <= ncols; ++c) {
        float x = x0 + c * TableColumnWidth;
        b.line(x + 0.5f, y0, x + 0.5f, y0 + h, 1, TableGrid);
    }
    for (int k = 0; k <= nrows + 1; ++k) {
        float y = k == 0 ? y0 : y0 + TableHeaderHeight + (k - 1) * TableCellHeight;
        b.line(x0, y + 0.5f, x0 + w, y + 0.5f, 1, TableGrid);
    }
    // Column icons (plotted columns)
    for (int c = 1; c < int(r.columns.size()); ++c) {
        const TableColumn& col = r.columns[c];
        float cx = x0 + c * TableColumnWidth + kCellPad + kColumnIconRadius, cy = y0 + TableHeaderHeight * 0.5f;
        if (col.style.hidden) b.circle(cx, cy, kColumnIconRadius, White, 1.5f, col.style.color.withAlpha(0.6f));
        else b.circle(cx, cy, kColumnIconRadius, col.style.color);
    }
    // Focused cell frame
    if (sel && (focus.field == Field::TableCell || focus.field == Field::TableHeader)) {
        float cx = x0 + focus.col * TableColumnWidth;
        float cy = focus.field == Field::TableHeader ? y0 : y0 + TableHeaderHeight + focus.cell * TableCellHeight;
        float ch = focus.field == Field::TableHeader ? TableHeaderHeight : TableCellHeight;
        b.rect({cx, cy, TableColumnWidth + 1, ch + 1}, Color{0, 0, 0, 0}, 0, 2, Accent);
    }
    b.popClip();
}

ListHit ExpressionList::hitTable(int i, float x, float y) const {
    const ExpressionRow& r = rows[i];
    ListHit h;
    h.row = i;
    if (x < area.x + TabWidth) { h.kind = ListHit::Tab; return h; }
    float top = rowTopOnScreen(i);
    float x0 = contentX(r) - 2, y0 = top + kTableTopPad;
    for (int c = 1; c < int(r.columns.size()); ++c) {
        float cx = x0 + c * TableColumnWidth + kCellPad + kColumnIconRadius, cy = y0 + TableHeaderHeight * 0.5f;
        if (std::hypot(x - cx, y - cy) <= kColumnIconRadius + 3) {
            h.kind = ListHit::ColumnIcon;
            h.col = c;
            return h;
        }
    }
    for (const FieldBox& fb : m_boxes[i]) {
        Rect rr{fb.rect.x - kCellPad, top + fb.rect.y, fb.rect.w + kCellPad, fb.rect.h};
        if (rr.contains(x, y)) {
            h.kind = ListHit::Field;
            h.field = fb.field;
            h.col = fb.col;
            h.cell = fb.cell;
            return h;
        }
    }
    h.kind = ListHit::Tab;
    return h;
}

bool ExpressionList::keyTable(Key k, Mods m, const std::u32string& text) {
    int row = focus.row;
    ExpressionRow& r = rows[row];
    MathTree* t = focusedTree();
    if (!t) return false;
    bool header = focus.field == Field::TableHeader;
    int nrows = tableRowCount(r);
    auto go = [&](Field f, int col, int cell, bool atEnd = true) { focusField({row, f, col, cell}, atEnd); };
    auto edit = [&](auto&& fn) {
        if (onContentWillChange) onContentWillChange();
        if (fn()) edited(row);
        else if (MathField* mf = fieldFor(focus)) mf->dirty = true;
    };
    switch (k) {
    case Key::Char: edit([&] { return t->typeText(text); }); return true;
    case Key::Backspace: edit([&] { return t->backspace(); }); return true;
    case Key::Delete: edit([&] { return t->deleteForward(); }); return true;
    case Key::Enter:
        if (header) go(Field::TableCell, focus.col, 0);
        else go(Field::TableCell, focus.col, std::min(focus.cell + 1, nrows));
        return true;
    case Key::Up:
        if (t->moveUp()) break;
        if (header) {
            int i = row - 1;
            while (i >= 0 && !rows[i].visibleInList) --i;
            if (i >= 0) focusRow(i);
        } else if (focus.cell == 0) {
            go(Field::TableHeader, focus.col, 0);
        } else {
            go(Field::TableCell, focus.col, focus.cell - 1);
        }
        return true;
    case Key::Down:
        if (t->moveDown()) break;
        if (header) go(Field::TableCell, focus.col, 0);
        else if (focus.cell < nrows) go(Field::TableCell, focus.col, focus.cell + 1);
        else {
            int i = row + 1;
            while (i < int(rows.size()) && !rows[i].visibleInList) ++i;
            if (i < int(rows.size())) focusRow(i);
        }
        return true;
    case Key::Left:
        if (cursorAtStart(*t) && !m.shift && focus.col > 0) { go(focus.field, focus.col - 1, focus.cell, true); return true; }
        t->moveLeft(m.shift);
        break;
    case Key::Right:
        if (cursorAtEnd(*t) && !m.shift && focus.col < int(r.columns.size())) { go(focus.field, focus.col + 1, focus.cell, false); return true; }
        t->moveRight(m.shift);
        break;
    case Key::Tab:
        if (m.shift) {
            if (focus.col > 0) go(focus.field, focus.col - 1, focus.cell);
        } else if (focus.col + 1 < int(r.columns.size())) {
            go(focus.field, focus.col + 1, focus.cell, false);
        } else if (!header) {
            go(Field::TableCell, 0, std::min(focus.cell + 1, nrows), false);
        }
        return true;
    case Key::Home: t->moveHome(m.shift); break;
    case Key::End: t->moveEnd(m.shift); break;
    case Key::Escape: blur(); return true;
    }
    if (MathField* mf = fieldFor(focus)) mf->dirty = true;
    return true;
}

} // namespace calc
