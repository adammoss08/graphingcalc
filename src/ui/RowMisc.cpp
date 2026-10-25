// Notes (plain text, word-wrapped), folders (collapse triangle, title, visibility toggle in the tab)
// and images (thumbnail + center / width / height fields).
#include "ui/ExpressionList.hpp"

#include <algorithm>
#include <cmath>

namespace calc {

using namespace theme;

namespace {

constexpr float kTriangleSize = 10;

// Word-wraps `t` to `width`; returns [start, end) index pairs per line.
std::vector<std::pair<int, int>> wrapLines(TextAtlas& a, const std::u32string& t, float width) {
    std::vector<std::pair<int, int>> lines;
    int n = int(t.size()), start = 0;
    while (start <= n) {
        float w = 0;
        int lastSpace = -1, i = start;
        for (; i < n; ++i) {
            if (t[i] == '\n') break;
            float cw = a.advance(Font::Ui, t[i], NoteTextSize);
            if (w + cw > width && i > start) break;
            if (t[i] == ' ') lastSpace = i;
            w += cw;
        }
        int end = i;
        if (i < n && t[i] != '\n' && lastSpace > start) end = lastSpace + 1;
        lines.push_back({start, end});
        if (end >= n) break;
        start = (end < n && t[end] == '\n') ? end + 1 : end;
        if (start == n && t[n - 1] == '\n') { lines.push_back({n, n}); break; }
    }
    if (lines.empty()) lines.push_back({0, 0});
    return lines;
}

void drawFolderIcon(Batch2D& b, float cx, float cy, Color c, bool filled) {
    Rect body{cx - 9, cy - 5, 18, 12};
    if (filled) b.rect(body, c, 2);
    else b.rect(body, Color{0, 0, 0, 0}, 2, 1.5f, c);
    b.rect({cx - 9, cy - 8, 8, 4}, filled ? c : Color{0, 0, 0, 0}, 1.5f, filled ? 0 : 1.5f, c);
}

void drawImageIcon(Batch2D& b, float cx, float cy, Color c, bool visible) {
    Rect frame{cx - 9, cy - 7, 18, 14};
    b.rect(frame, visible ? Paper : Color{0, 0, 0, 0}, 2, 1.5f, c);
    b.line(cx - 6, cy + 4, cx - 1, cy - 1, 1.5f, c);
    b.line(cx - 1, cy - 1, cx + 2, cy + 2, 1.5f, c);
    b.line(cx + 2, cy + 2, cx + 6, cy - 3, 1.5f, c);
}

} // namespace

float ExpressionList::layoutMisc(int i, std::vector<FieldBox>& boxes) {
    ExpressionRow& r = rows[i];
    float x0 = contentX(r);
    switch (r.type) {
    case RowType::Note: {
        auto lines = wrapLines(*atlas, r.text.text, contentRight() - x0);
        float h = 14 + lines.size() * NoteLineHeight;
        boxes.push_back({Field::Text, 0, 0, Rect{x0, 8, contentRight() - x0, h - 4}, 8 + NoteTextSize, NoteTextSize});
        r.mathBaseline = 8 + NoteTextSize;
        return std::max(MiscRowMinHeight, h + 10);
    }
    case RowType::Folder: {
        float tx = x0 + kTriangleSize + 12;
        boxes.push_back({Field::Text, 0, 0, Rect{tx, 10, contentRight() - tx, MiscRowMinHeight - 20},
                         MiscRowMinHeight * 0.5f + NoteTextSize * 0.35f, NoteTextSize});
        r.mathBaseline = MiscRowMinHeight * 0.5f;
        return MiscRowMinHeight;
    }
    case RowType::Image: {
        float fx = x0 + ImageThumbSize + 16;
        float em = InlineFieldSize;
        auto field = [&](Field f, MathField& mf, float x, float base) {
            mf.relayout(*atlas, em);
            float w = std::max(InlineFieldMinWidth, mf.layout.width() + 6);
            boxes.push_back({f, 0, 0, Rect{x, base - em - 2, w, em * 1.3f + 4}, base, em, true});
            return w;
        };
        float lw = atlas->measure(Font::Ui, std::string("center"), SliderLabelSize) + 8;
        field(Field::ImageCenter, r.imageCenter, fx + lw, 30);
        float ww = atlas->measure(Font::Ui, std::string("width"), SliderLabelSize) + 8;
        float x = fx + ww;
        x += field(Field::ImageWidth, r.imageWidth, x, 62) + 14;
        x += atlas->measure(Font::Ui, std::string("height"), SliderLabelSize) + 8;
        field(Field::ImageHeight, r.imageHeight, x, 62);
        r.mathBaseline = 30;
        return std::max(ImageThumbSize + 22, 80.0f);
    }
    default:
        return RowMinHeight;
    }
}

void ExpressionList::drawMisc(Batch2D& b, int i, float top, bool sel, bool cursorVisible) const {
    const ExpressionRow& r = rows[i];
    float icx = area.x + TabWidth * 0.5f, icy = top + IconCenterY;
    const FieldBox* textBox = boxFor(i, {i, Field::Text});
    bool editingText = sel && focus.field == Field::Text;
    switch (r.type) {
    case RowType::Note: {
        if (!textBox) break;
        auto lines = wrapLines(*atlas, r.text.text, textBox->rect.w);
        float base = top + textBox->baseline;
        for (size_t k = 0; k < lines.size(); ++k) {
            auto [s, e] = lines[k];
            std::u32string line = r.text.text.substr(s, e - s);
            float y = base + k * NoteLineHeight;
            // selection
            if (editingText && r.text.hasSelection()) {
                int a = std::min(r.text.anchor, r.text.cursor), z = std::max(r.text.anchor, r.text.cursor);
                int ls = std::max(a, s), le = std::min(z, e);
                if (ls < le) {
                    float x1 = textBox->rect.x + atlas->measure(Font::Ui, r.text.text.substr(s, ls - s), NoteTextSize);
                    float x2 = textBox->rect.x + atlas->measure(Font::Ui, r.text.text.substr(s, le - s), NoteTextSize);
                    b.rect({x1, y - NoteTextSize, x2 - x1, NoteLineHeight}, SelectionColor);
                }
            }
            b.text(Font::Ui, NoteTextSize, textBox->rect.x, y, line, Text);
            if (editingText && cursorVisible && !r.text.hasSelection() && r.text.cursor >= s &&
                (r.text.cursor < e || (r.text.cursor == e && (k + 1 == lines.size() || e < lines[k + 1].first)))) {
                float cx = textBox->rect.x + atlas->measure(Font::Ui, r.text.text.substr(s, r.text.cursor - s), NoteTextSize);
                b.line(cx, y - NoteTextSize * 0.85f, cx, y + NoteTextSize * 0.25f, 1, CursorColor);
            }
        }
        break;
    }
    case RowType::Folder: {
        drawFolderIcon(b, icx, icy, sel ? White : (r.hidden ? FolderIcon.withAlpha(0.3f) : FolderIcon), !r.hidden);
        // collapse triangle
        float tx = contentX(r) + kTriangleSize * 0.5f, ty = top + MiscRowMinHeight * 0.5f, s = kTriangleSize * 0.5f;
        if (r.collapsed) {
            b.line(tx - s * 0.5f, ty - s, tx + s * 0.6f, ty, 1.8f, IconColor);
            b.line(tx + s * 0.6f, ty, tx - s * 0.5f, ty + s, 1.8f, IconColor);
        } else {
            b.line(tx - s, ty - s * 0.5f, tx, ty + s * 0.6f, 1.8f, IconColor);
            b.line(tx, ty + s * 0.6f, tx + s, ty - s * 0.5f, 1.8f, IconColor);
        }
        if (!textBox) break;
        float base = top + textBox->baseline;
        if (r.text.text.empty() && !editingText) {
            b.text(Font::Ui, NoteTextSize, textBox->rect.x, base, std::string("Untitled folder"), Placeholder);
        } else {
            if (editingText && r.text.hasSelection()) {
                int a = std::min(r.text.anchor, r.text.cursor), z = std::max(r.text.anchor, r.text.cursor);
                float x1 = textBox->rect.x + atlas->measure(Font::Ui, r.text.text.substr(0, a), NoteTextSize);
                float x2 = textBox->rect.x + atlas->measure(Font::Ui, r.text.text.substr(0, z), NoteTextSize);
                b.rect({x1, base - NoteTextSize, x2 - x1, NoteLineHeight}, SelectionColor);
            }
            b.text(Font::UiBold, NoteTextSize, textBox->rect.x, base, r.text.text, Text);
            if (editingText && cursorVisible && !r.text.hasSelection()) {
                float cx = textBox->rect.x + atlas->measure(Font::UiBold, r.text.text.substr(0, r.text.cursor), NoteTextSize);
                b.line(cx, base - NoteTextSize * 0.85f, cx, base + NoteTextSize * 0.25f, 1, CursorColor);
            }
        }
        break;
    }
    case RowType::Image: {
        drawImageIcon(b, icx, icy, sel ? White : (r.hidden ? Placeholder : IconColor), !r.hidden);
        float x0 = contentX(r);
        if (r.imageTexture < 0) {
            b.rect({x0, top + 10, ImageThumbSize, ImageThumbSize}, TableHeaderBg, 4, 1, TableGrid);
            b.text(Font::Ui, SliderLabelSize, x0 + 6, top + 10 + ImageThumbSize * 0.5f + 4, std::string("missing"), Placeholder);
        }
        float fx = x0 + ImageThumbSize + 16;
        b.text(Font::Ui, SliderLabelSize, fx, top + 30, std::string("center"), TextSecondary);
        b.text(Font::Ui, SliderLabelSize, fx, top + 62, std::string("width"), TextSecondary);
        if (const FieldBox* wb = boxFor(i, {i, Field::ImageWidth}))
            b.text(Font::Ui, SliderLabelSize, wb->rect.right() + 14, top + 62, std::string("height"), TextSecondary);
        for (const FieldBox& fb : m_boxes[i]) drawFieldBox(b, i, fb, top, cursorVisible, Text);
        break;
    }
    default: break;
    }
}

ListHit ExpressionList::hitMisc(int i, float x, float y) const {
    const ExpressionRow& r = rows[i];
    ListHit h;
    h.row = i;
    float top = rowTopOnScreen(i);
    if (x < area.x + TabWidth) {
        float dx = x - (area.x + TabWidth * 0.5f), dy = y - (top + IconCenterY);
        bool onIcon = dx * dx + dy * dy <= 16 * 16 && (r.type == RowType::Folder || r.type == RowType::Image);
        h.kind = onIcon ? ListHit::Icon : ListHit::Tab;
        return h;
    }
    if (r.type == RowType::Folder && x < contentX(r) + kTriangleSize + 8) {
        h.kind = ListHit::FolderToggle;
        return h;
    }
    for (const FieldBox& fb : m_boxes[i]) {
        Rect rr{fb.rect.x - 3, top + fb.rect.y - 3, fb.rect.w + 6, fb.rect.h + 6};
        if (rr.contains(x, y) || (fb.field == Field::Text && r.type != RowType::Image)) {
            h.kind = ListHit::Field;
            h.field = fb.field;
            return h;
        }
    }
    h.kind = ListHit::Tab;
    return h;
}

bool ExpressionList::keyText(Key k, Mods m, const std::u32string& text) {
    int row = focus.row;
    ExpressionRow& r = rows[row];
    TextField& tf = *focusedText();
    bool isLabel = focus.field == Field::Label;
    auto edit = [&](auto&& fn) {
        if (onContentWillChange) onContentWillChange();
        if (fn()) edited(row);
    };
    switch (k) {
    case Key::Char: edit([&] { tf.insert(text); return true; }); return true;
    case Key::Backspace:
        if (tf.text.empty() && !isLabel && r.type == RowType::Note && rows.size() > 1) {
            if (onContentWillChange) onContentWillChange();
            removeRow(row);
            focusRow(std::max(0, row - 1));
            return true;
        }
        edit([&] { return tf.backspace(); });
        return true;
    case Key::Delete: edit([&] { return tf.deleteForward(); }); return true;
    case Key::Left: tf.move(-1, m.shift); return true;
    case Key::Right: tf.move(1, m.shift); return true;
    case Key::Home: tf.home(m.shift); return true;
    case Key::End: tf.end(m.shift); return true;
    case Key::Up: {
        int i = row - 1;
        while (i >= 0 && !rows[i].visibleInList) --i;
        if (i >= 0) focusRow(i);
        return true;
    }
    case Key::Down: {
        int i = row + 1;
        while (i < int(rows.size()) && !rows[i].visibleInList) ++i;
        if (i < int(rows.size())) focusRow(i);
        return true;
    }
    case Key::Enter:
        if (isLabel) { focusField({row, Field::Main}); return true; }
        if (onContentWillChange) onContentWillChange();
        if (r.type == RowType::Folder) r.collapsed = false;
        addRow(row + 1);   // after a folder this lands inside it
        return true;
    case Key::Escape: blur(); return true;
    case Key::Tab: return true;
    }
    return false;
}

} // namespace calc
