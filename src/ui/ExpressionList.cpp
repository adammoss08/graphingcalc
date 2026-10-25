#include "ui/ExpressionList.hpp"
#include "math/Latex.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace calc {

using namespace theme;

namespace {
constexpr double kLongPressSeconds = 0.45;
}

// ---- rows --------------------------------------------------------------------------------------

Color ExpressionList::nextColor() {
    Color c = ColorCycle[m_colorIdx % ColorCycleCount];
    m_colorIdx = (m_colorIdx + 1) % ColorCycleCount;
    return c;
}

int ExpressionList::indexOfId(uint32_t id) const {
    for (int i = 0; i < int(rows.size()); ++i)
        if (rows[i].id == id) return i;
    return -1;
}

int ExpressionList::folderEnd(int folderRow) const {
    uint32_t id = rows[folderRow].id;
    int i = folderRow + 1;
    while (i < int(rows.size()) && rows[i].folderId == id) ++i;
    return i;
}

int ExpressionList::addRow(int at, const std::string& latex, bool focusIt, RowType type) {
    at = std::clamp(at, 0, int(rows.size()));
    ExpressionRow r;
    r.id = m_nextId++;
    r.type = type;
    if (type == RowType::Expression || type == RowType::Image) r.color = nextColor();
    r.syncStyle();
    if (!latex.empty()) {
        if (type == RowType::Expression) r.tree.insertLatex(latex);
        else r.text.setUtf8(latex);
    }
    // A row inserted inside a folder's block joins that folder.
    if (at > 0 && at <= int(rows.size())) {
        const ExpressionRow& prev = rows[at - 1];
        if (prev.type == RowType::Folder && type != RowType::Folder) r.folderId = prev.id;
        else if (prev.folderId && type != RowType::Folder) r.folderId = prev.folderId;
    }
    rows.insert(rows.begin() + at, std::move(r));
    if (focus.row >= at) ++focus.row;
    if (focusIt) focusRow(at);
    changed();
    return at;
}

int ExpressionList::addTable(int at, bool focusIt) {
    // Next free subscript n so the columns are x_n, y_n.
    int n = 1;
    auto used = [&](int k) {
        std::string xs = "x_{" + std::to_string(k) + "}";
        for (auto& r : rows) {
            if (r.type == RowType::Table)
                for (auto& c : r.columns) if (c.header.latex() == xs) return true;
            if (r.type == RowType::Expression && blockToLatex(r.tree.root()).rfind(xs + "=", 0) == 0) return true;
        }
        return false;
    };
    while (used(n)) ++n;
    int i = addRow(at, "", false, RowType::Table);
    ExpressionRow& r = rows[i];
    for (int c = 0; c < 2; ++c) {
        TableColumn col;
        col.id = m_nextId++;
        col.header.set(std::string(c == 0 ? "x" : "y") + "_{" + std::to_string(n) + "}");
        col.style.color = c == 0 ? Color{} : nextColor();
        r.columns.push_back(std::move(col));
    }
    if (focusIt) focusField({i, Field::TableCell, 0, 0});
    changed();
    return i;
}

void ExpressionList::removeRow(int i) {
    if (i < 0 || i >= int(rows.size())) return;
    int end = rows[i].type == RowType::Folder ? folderEnd(i) : i + 1;
    rows.erase(rows.begin() + i, rows.begin() + end);
    int removed = end - i;
    if (focus.row >= i && focus.row < end) focus = {};
    else if (focus.row >= end) focus.row -= removed;
    changed();
}

// ---- focus -------------------------------------------------------------------------------------

void ExpressionList::focusRow(int i, bool atEnd) {
    if (i < 0 || i >= int(rows.size())) { blur(); return; }
    Focus f{i, Field::Main, 0, 0};
    switch (rows[i].type) {
    case RowType::Note: case RowType::Folder: f.field = Field::Text; break;
    case RowType::Table: f.field = Field::TableCell; break;
    case RowType::Image: f.field = Field::ImageCenter; break;
    default: break;
    }
    focusField(f, atEnd);
}

void ExpressionList::focusField(const Focus& f, bool atEnd) {
    if (focus.row >= 0 && focus.row < int(rows.size()) && !(focus == f)) {
        if (MathTree* t = focusedTree()) {
            t->solidifyBrackets();
            t->clearSelection();
        }
        rows[focus.row].layoutDirty = true;
        if (MathField* mf = fieldFor(focus)) mf->dirty = true;
        if (rows[focus.row].type == RowType::Table) trimTable(rows[focus.row], false);
    }
    focus = (f.row >= 0 && f.row < int(rows.size())) ? f : Focus{};
    if (focus.row < 0) return;
    ExpressionRow& r = rows[focus.row];
    if (r.type == RowType::Table) fieldFor(focus);  // materialise the placeholder cell/column
    // Focusing a row inside a collapsed folder opens the folder.
    if (r.folderId) {
        int fi = indexOfId(r.folderId);
        if (fi >= 0) rows[fi].collapsed = false;
    }
    if (MathTree* t = focusedTree()) {
        if (atEnd) t->moveEnd();
        else t->moveHome();
    } else if (TextField* tf = focusedText()) {
        if (atEnd) tf->end(false);
        else tf->home(false);
    }
    r.layoutDirty = true;
    if (MathField* mf = fieldFor(focus)) mf->dirty = true;
    scrollToRow(focus.row);
}

MathField* ExpressionList::fieldFor(const Focus& f) {
    if (f.row < 0 || f.row >= int(rows.size())) return nullptr;
    ExpressionRow& r = rows[f.row];
    switch (f.field) {
    case Field::SliderMin: return &r.sliderMinField;
    case Field::SliderMax: return &r.sliderMaxField;
    case Field::SliderStep: return &r.sliderStepField;
    case Field::DomainMin: return &r.domainMin;
    case Field::DomainMax: return &r.domainMax;
    case Field::ImageCenter: return &r.imageCenter;
    case Field::ImageWidth: return &r.imageWidth;
    case Field::ImageHeight: return &r.imageHeight;
    case Field::TableHeader: case Field::TableCell: {
        if (r.type != RowType::Table || f.col < 0) return nullptr;
        // Typing into the placeholder column / row creates it.
        while (int(r.columns.size()) <= f.col) {
            TableColumn c;
            c.id = m_nextId++;
            c.style.color = nextColor();
            r.columns.push_back(std::move(c));
        }
        if (f.field == Field::TableHeader) return &r.columns[f.col].header;
        int need = f.cell + 1;
        for (auto& c : r.columns)
            while (int(c.cells.size()) < need) c.cells.emplace_back();
        return &r.columns[f.col].cells[f.cell];
    }
    default: return nullptr;
    }
}

const MathTree* ExpressionList::treeFor(const Focus& f) const {
    if (f.row < 0 || f.row >= int(rows.size())) return nullptr;
    const ExpressionRow& r = rows[f.row];
    if (f.field == Field::Main) return r.type == RowType::Expression ? &r.tree : nullptr;
    auto* self = const_cast<ExpressionList*>(this);
    if ((f.field == Field::TableHeader || f.field == Field::TableCell) &&
        (f.col >= int(r.columns.size()) || (f.field == Field::TableCell && f.cell >= int(r.columns[f.col].cells.size()))))
        return nullptr;
    MathField* mf = self->fieldFor(f);
    return mf ? &mf->tree : nullptr;
}

MathTree* ExpressionList::focusedTree() {
    if (focus.row < 0) return nullptr;
    ExpressionRow& r = rows[focus.row];
    if (focus.field == Field::Main) return r.type == RowType::Expression ? &r.tree : nullptr;
    if (focus.field == Field::Text || focus.field == Field::Label) return nullptr;
    MathField* mf = fieldFor(focus);
    return mf ? &mf->tree : nullptr;
}

TextField* ExpressionList::focusedText() {
    if (focus.row < 0) return nullptr;
    if (focus.field == Field::Text) return &rows[focus.row].text;
    if (focus.field == Field::Label) return &rows[focus.row].label;
    return nullptr;
}

const FieldBox* ExpressionList::boxFor(int row, const Focus& f) const {
    if (row < 0 || row >= int(m_boxes.size())) return nullptr;
    for (const FieldBox& b : m_boxes[row])
        if (b.field == f.field && b.col == f.col && b.cell == f.cell) return &b;
    return nullptr;
}

void ExpressionList::changed() {
    if (focus.row >= 0 && focus.row < int(rows.size())) rows[focus.row].layoutDirty = true;
    if (onContentChanged) onContentChanged();
}

void ExpressionList::edited(int row) {
    if (row >= 0 && row < int(rows.size())) {
        rows[row].layoutDirty = true;
        if (MathField* mf = fieldFor(focus)) mf->dirty = true;
    }
    if (onContentChanged) onContentChanged();
}

// ---- layout ------------------------------------------------------------------------------------

void ExpressionList::layout(TextAtlas& a) {
    atlas = &a;
    m_boxes.assign(rows.size(), {});
    float y = 0;
    for (int i = 0; i < int(rows.size()); ++i) {
        ExpressionRow& r = rows[i];
        r.indent = 0;
        r.visibleInList = true;
        if (r.folderId) {
            int fi = indexOfId(r.folderId);
            if (fi >= 0) {
                r.indent = FolderIndent;
                r.visibleInList = !rows[fi].collapsed;
            } else {
                r.folderId = 0;  // folder was deleted
            }
        }
        r.y = y;
        if (!r.visibleInList) { r.h = 0; continue; }
        float h;
        switch (r.type) {
        case RowType::Table: h = layoutTable(i, m_boxes[i]); break;
        case RowType::Expression: h = layoutExpression(i, m_boxes[i]); break;
        default: h = layoutMisc(i, m_boxes[i]); break;
        }
        r.h = std::ceil(h);
        y += r.h;
    }
    scrollBy(0);
    updateFieldScroll();
}

void ExpressionList::updateFieldScroll() {
    if (!(focus == m_scrollFocus)) {
        m_scrollFocus = focus;
        m_scrollX = 0;
    }
    const FieldBox* fb = focus.row >= 0 ? boxFor(focus.row, focus) : nullptr;
    const MathTree* t = focus.row >= 0 ? treeFor(focus) : nullptr;
    const MathLayout* l = nullptr;
    if (fb && t) {
        if (focus.field == Field::Main) {
            l = &rows[focus.row].layout;
        } else if (MathField* mf = fieldFor(focus)) {
            mf->relayout(*atlas, fb->em);
            l = &mf->layout;
        }
    }
    float cx, ctop, cbot;
    if (!l || !l->caretRect(t->cursor(), cx, ctop, cbot)) {
        m_scrollX = 0;
        return;
    }
    float visible = fb->rect.w - (fb->underline ? 4 : 0);
    float margin = std::min(theme::FieldScrollMargin, visible * 0.25f);
    if (cx - m_scrollX > visible - margin) m_scrollX = cx - (visible - margin);
    if (cx - m_scrollX < margin) m_scrollX = cx - margin;
    // Never scroll past the end of the content, nor before its start.
    m_scrollX = std::clamp(m_scrollX, 0.0f, std::max(0.0f, l->width() + 2 - visible));
}

float ExpressionList::contentHeight() const {
    float h = 0;
    for (auto& r : rows) h = std::max(h, r.y + r.h);
    return h + NewRowHeight;
}

void ExpressionList::scrollBy(float dy) {
    float maxScroll = std::max(0.0f, contentHeight() - area.h);
    scrollY = std::clamp(scrollY + dy, 0.0f, maxScroll);
}

void ExpressionList::scrollToRow(int i) {
    if (i < 0 || i >= int(rows.size())) return;
    const auto& r = rows[i];
    if (r.y < scrollY) scrollY = r.y;
    else if (r.y + r.h > scrollY + area.h) scrollY = r.y + r.h - area.h;
}

// ---- drawing -----------------------------------------------------------------------------------

void ExpressionList::drawFieldBox(Batch2D& b, int row, const FieldBox& fb, float top, bool cursorVisible,
                                  Color color) const {
    Focus f{row, fb.field, fb.col, fb.cell};
    bool focused = focus == f;
    const MathTree* t = nullptr;
    const MathLayout* l = nullptr;
    const ExpressionRow& r = rows[row];
    if (fb.field == Field::Main) {
        t = &r.tree;
        l = &r.layout;
    } else {
        t = treeFor(f);
        if (!t) {
            if (fb.underline) b.line(fb.rect.x, top + fb.rect.bottom() - 0.5f, fb.rect.right(), top + fb.rect.bottom() - 0.5f, 1, Border);
            return;
        }
        auto* self = const_cast<ExpressionList*>(this);
        MathField* mf = self->fieldFor(f);
        mf->relayout(*atlas, fb.em);
        l = &mf->layout;
    }
    if (fb.underline) {
        float uy = top + fb.rect.bottom();
        if (focused) b.rect({fb.rect.x, uy - 2, fb.rect.w, 2}, Accent);
        else b.line(fb.rect.x, uy - 0.5f, fb.rect.right(), uy - 0.5f, 1, Border);
    }
    b.pushClip({fb.rect.x - 2, top + fb.rect.y - 4, fb.rect.w + 4, fb.rect.h + 8});
    float x = fb.rect.x + (fb.underline ? 2 : 0) - fieldScroll(f);
    l->draw(b, x, top + fb.baseline, color, t, focused, cursorVisible);
    b.popClip();
}

void ExpressionList::draw(Batch2D& b, bool cursorVisible) const {
    b.pushClip(area);  // background is the white clear colour
    int index = 0;     // Desmos numbers every visible row, folders included
    for (int i = 0; i < int(rows.size()); ++i) {
        const ExpressionRow& r = rows[i];
        if (!r.visibleInList) continue;
        ++index;
        float top = rowTopOnScreen(i);
        if (top > area.bottom() || top + r.h < area.y) continue;
        bool sel = focus.row == i;
        bool hover = m_hover.row == i;

        // Tab (gutter)
        Rect tab{area.x, top, TabWidth, r.h};
        b.rect(tab, sel ? Accent : TabBg);
        if (!sel) b.line(tab.right() - 0.5f, top, tab.right() - 0.5f, top + r.h, 1, TabBorder);
        char idx[16];
        std::snprintf(idx, sizeof idx, "%d", index);
        b.text(Font::Ui, TabIndexSize, area.x + 3, top + 2 + TabIndexSize * 0.9f, std::string(idx),
               sel ? White : TextSecondary);
        if (r.indent > 0) b.line(area.x + TabWidth + 6.5f, top, area.x + TabWidth + 6.5f, top + r.h, 1, RowBorder);

        switch (r.type) {
        case RowType::Expression: drawExpression(b, i, top, sel, hover, cursorVisible); break;
        case RowType::Table: drawTable(b, i, top, sel, cursorVisible); break;
        default: drawMisc(b, i, top, sel, cursorVisible); break;
        }

        // Delete ×
        if (hover || sel) {
            bool hot = hover && m_hover.kind == ListHit::Delete;
            widgets::drawIcon(b, widgets::Icon::Close, area.right() - 18, top + IconCenterY, DeleteSize,
                              hot ? Text : DeleteIcon);
        }
        // Borders: row separator; selected rows get the accent frame.
        if (sel) {
            b.rect({area.x, top - 1, area.w, 2}, Accent);
            b.rect({area.x, top + r.h - 1, area.w, 2}, Accent);
            b.line(area.right() - 0.5f, top, area.right() - 0.5f, top + r.h, 1, Accent);
        } else {
            b.line(area.x, top + 0.5f, area.right(), top + 0.5f, 1, RowBorder);
        }
    }
    // Reorder drag: dim the dragged block and show where it will land.
    if (m_dragRow >= 0) {
        int end = rows[m_dragRow].type == RowType::Folder ? folderEnd(m_dragRow) : m_dragRow + 1;
        for (int i = m_dragRow; i < end; ++i)
            if (rows[i].visibleInList) b.rect({area.x, rowTopOnScreen(i), area.w, rows[i].h}, Paper.withAlpha(0.55f));
        if (m_dropIndex >= 0) {
            float y = m_dropIndex < int(rows.size()) ? rowTopOnScreen(m_dropIndex) : area.y + contentHeight() - NewRowHeight - scrollY;
            float indent = 0;
            if (rows[m_dragRow].type != RowType::Folder && m_dropIndex > 0) {
                const ExpressionRow& prev = rows[m_dropIndex - 1];
                bool intoFolder = (prev.type == RowType::Folder && !prev.collapsed) ||
                                  (prev.folderId && indexOfId(prev.folderId) >= 0 && !rows[indexOfId(prev.folderId)].collapsed);
                if (intoFolder) indent = FolderIndent;
            }
            b.rect({area.x + TabWidth + indent, y - 1.5f, area.w - TabWidth - indent, 3}, Accent, 1.5f);
        }
    }
    // The "new expression" row.
    float top = area.y + contentHeight() - NewRowHeight - scrollY;
    if (top < area.bottom()) {
        bool sel = focus.row >= 0 && focus.row == int(rows.size()) - 1;
        if (!sel) b.line(area.x, top + 0.5f, area.right(), top + 0.5f, 1, RowBorder);
        b.rect({area.x, top + 1, TabWidth, NewRowHeight - 1}, TabBg.withAlpha(0.5f));
        char idx[16];
        std::snprintf(idx, sizeof idx, "%d", index + 1);
        b.text(Font::Ui, TabIndexSize, area.x + 3, top + 2 + TabIndexSize * 0.9f, std::string(idx),
               TextSecondary.withAlpha(0.3f));
        b.line(area.x, top + NewRowHeight - 0.5f, area.right(), top + NewRowHeight - 0.5f, 1, RowBorder);
    }
    b.popClip();
}

void ExpressionList::drawTextLine(Batch2D& b, const TextField& tf, float x, float baseline, bool focused,
                                  bool cursorVisible, const char* placeholder) const {
    if (tf.text.empty() && !focused) {
        b.text(Font::Ui, NoteTextSize, x, baseline, std::string(placeholder), Placeholder);
        return;
    }
    if (focused && tf.hasSelection()) {
        int a = std::min(tf.anchor, tf.cursor), z = std::max(tf.anchor, tf.cursor);
        float x1 = x + atlas->measure(Font::Ui, tf.text.substr(0, a), NoteTextSize);
        float x2 = x + atlas->measure(Font::Ui, tf.text.substr(0, z), NoteTextSize);
        b.rect({x1, baseline - NoteTextSize, x2 - x1, NoteLineHeight}, SelectionColor);
    }
    b.text(Font::Ui, NoteTextSize, x, baseline, tf.text, Text);
    if (focused && cursorVisible && !tf.hasSelection()) {
        float cx = x + atlas->measure(Font::Ui, tf.text.substr(0, tf.cursor), NoteTextSize);
        b.line(cx, baseline - NoteTextSize * 0.85f, cx, baseline + NoteTextSize * 0.25f, 1, CursorColor);
    }
}

// ---- reordering --------------------------------------------------------------------------------

int ExpressionList::dropIndexAt(float y) const {
    int end = rows[m_dragRow].type == RowType::Folder ? folderEnd(m_dragRow) : m_dragRow + 1;
    for (int i = 0; i < int(rows.size()); ++i) {
        if (!rows[i].visibleInList || (i >= m_dragRow && i < end)) continue;
        if (y < rowTopOnScreen(i) + rows[i].h * 0.5f) return i;
    }
    return int(rows.size());
}

void ExpressionList::moveBlock(int from, int dropBefore) {
    bool folder = rows[from].type == RowType::Folder;
    int end = folder ? folderEnd(from) : from + 1;
    if (dropBefore >= from && dropBefore <= end) return;  // dropped onto itself
    uint32_t focusedId = focus.row >= 0 ? rows[focus.row].id : 0;
    std::vector<ExpressionRow> block(std::make_move_iterator(rows.begin() + from), std::make_move_iterator(rows.begin() + end));
    rows.erase(rows.begin() + from, rows.begin() + end);
    if (dropBefore > from) dropBefore -= end - from;
    auto collapsedFolder = [&](uint32_t id) { int fi = indexOfId(id); return fi >= 0 && rows[fi].collapsed; };
    if (folder) {
        // Folders don't nest: a drop inside another folder's block goes after that block.
        if (dropBefore > 0 && dropBefore < int(rows.size()) && rows[dropBefore].folderId)
            dropBefore = folderEnd(indexOfId(rows[dropBefore].folderId));
    } else {
        // A row dropped right under an open folder (or among its children) joins that folder.
        uint32_t fid = 0;
        if (dropBefore > 0) {
            const ExpressionRow& prev = rows[dropBefore - 1];
            if (prev.type == RowType::Folder && !prev.collapsed) fid = prev.id;
            else if (prev.folderId && !collapsedFolder(prev.folderId)) fid = prev.folderId;
        }
        block[0].folderId = fid;
    }
    rows.insert(rows.begin() + dropBefore, std::make_move_iterator(block.begin()), std::make_move_iterator(block.end()));
    for (auto& r : rows) r.layoutDirty = true;
    focus.row = focusedId ? indexOfId(focusedId) : -1;
}

// ---- input -------------------------------------------------------------------------------------

ListHit ExpressionList::hitTest(float x, float y) const {
    ListHit h;
    if (!area.contains(x, y)) return h;
    for (int i = 0; i < int(rows.size()); ++i) {
        const auto& r = rows[i];
        if (!r.visibleInList) continue;
        float top = rowTopOnScreen(i);
        if (y < top || y >= top + r.h) continue;
        h.row = i;
        if (x > area.right() - 34 && y < top + IconCenterY + 16) { h.kind = ListHit::Delete; return h; }
        switch (r.type) {
        case RowType::Expression: return hitExpression(i, x, y);
        case RowType::Table: return hitTable(i, x, y);
        default: return hitMisc(i, x, y);
        }
    }
    h.kind = ListHit::NewRow;
    return h;
}

bool ExpressionList::mouseDown(float x, float y, int clicks, Mods m) {
    ListHit h = hitTest(x, y);
    m_pressed = h;
    m_pressX = x;
    m_pressY = y;
    m_longPressFired = false;
    switch (h.kind) {
    case ListHit::None: return false;
    case ListHit::NewRow: {
        if (onContentWillChange) onContentWillChange();
        int at = int(rows.size());
        int i = addRow(at, "", false);
        rows[i].folderId = 0;  // the new-row line is always top level
        focusRow(i);
        return true;
    }
    case ListHit::Icon: case ListHit::ColumnIcon:
        // Toggle on release; a long press (or right-click) opens the style menu instead.
        if (m.ctrl || m.alt) {
            m_longPressFired = true;
            if (onStyleMenu) {
                const ExpressionRow& r = rows[h.row];
                float top = rowTopOnScreen(h.row);
                Rect anchor = h.kind == ListHit::Icon
                                  ? Rect{area.x + 4, top + IconCenterY - 15, 30, 30}
                                  : Rect{x - 10, y - 10, 20, 20};
                (void)r;
                onStyleMenu(h.row, h.kind == ListHit::Icon ? -1 : h.col, anchor);
            }
            return true;
        }
        m_pressTime = -2;  // armed: tick() stamps the time on the next frame
        if (h.kind == ListHit::Icon) m_dragCandidate = h.row;  // the whole tab is a drag handle
        return true;
    case ListHit::Delete:
        if (onContentWillChange) onContentWillChange();
        removeRow(h.row);
        return true;
    case ListHit::Slider:
        if (onContentWillChange) onContentWillChange();
        m_sliderDrag = h.row;
        setSliderFromX(h.row, x);
        return true;
    case ListHit::Play: {
        ExpressionRow& r = rows[h.row];
        r.playing = !r.playing;
        if (onStyleChanged) onStyleChanged();
        return true;
    }
    case ListHit::EvalToggle:
        rows[h.row].evalAsFraction = !rows[h.row].evalAsFraction;
        if (onStyleChanged) onStyleChanged();
        return true;
    case ListHit::FolderToggle:
        rows[h.row].collapsed = !rows[h.row].collapsed;
        if (onStyleChanged) onStyleChanged();
        return true;
    case ListHit::Suggestion: case ListHit::SuggestionAll: {
        if (onContentWillChange) onContentWillChange();
        std::vector<std::string> names = rows[h.row].analysis.sliderSuggestions;
        if (h.kind == ListHit::Suggestion) names = {names[h.index]};
        int at = h.row + 1;
        Focus keep = focus;
        for (auto& n : names) addRow(at++, n + "=1", false);
        if (keep.row > h.row) keep.row += int(names.size());
        focus = keep;
        return true;
    }
    case ListHit::Tab:
        focusRow(h.row);
        m_dragCandidate = h.row;   // dragging a tab reorders (Desmos)
        return true;
    case ListHit::Field: {
        Focus f{h.row, h.field, h.col, h.cell};
        bool newFocus = !(focus == f);
        if (newFocus) focusField(f);
        const FieldBox* fb = boxFor(h.row, f);
        if (h.field == Field::Text || h.field == Field::Label) {
            if (TextField* tf = focusedText()) {
                if (clicks >= 2) tf->selectAll();
                else tf->end(false);
            }
            return true;
        }
        MathTree* t = focusedTree();
        if (!t || !fb) return true;
        const MathLayout* l = nullptr;
        if (h.field == Field::Main) l = &rows[h.row].layout;
        else if (MathField* mf = fieldFor(f)) { mf->relayout(*atlas, fb->em); l = &mf->layout; }
        if (!l) return true;
        float lx = x - fb->rect.x - (fb->underline ? 2 : 0) + fieldScroll(f), ly = y - (rowTopOnScreen(h.row) + fb->baseline);
        if (clicks >= 2) {
            t->selectAll();
        } else {
            Cursor c = l->hitTest(lx, ly);
            if (m.shift && !newFocus) t->select(t->cursor(), c);
            else t->setCursor(c);
            m_selectingText = true;
        }
        rows[h.row].layoutDirty = true;
        return true;
    }
    }
    return false;
}

bool ExpressionList::mouseDragged(float x, float y) {
    if (m_dragCandidate >= 0 && m_dragRow < 0 && std::fabs(y - m_pressY) > DragThreshold) {
        m_dragRow = m_dragCandidate;
        m_pressed = {};      // a drag from the colour icon doesn't toggle it
        m_pressTime = -1;
    }
    if (m_dragRow >= 0) {
        m_dragY = y;
        m_dropIndex = dropIndexAt(y);
        return true;
    }
    if (m_sliderDrag >= 0) {
        setSliderFromX(m_sliderDrag, x);
        return true;
    }
    if (m_pressTime != -1 && std::hypot(x - m_pressX, y - m_pressY) > DragThreshold) m_pressTime = -1;
    if (m_selectingText && focus.row >= 0) {
        const FieldBox* fb = boxFor(focus.row, focus);
        MathTree* t = focusedTree();
        if (!fb || !t) return false;
        const MathLayout* l = focus.field == Field::Main ? &rows[focus.row].layout
                                                         : (fieldFor(focus) ? &fieldFor(focus)->layout : nullptr);
        if (!l) return false;
        float lx = x - fb->rect.x - (fb->underline ? 2 : 0) + fieldScroll(focus), ly = y - (rowTopOnScreen(focus.row) + fb->baseline);
        t->select(t->anchorOrCursor(), l->hitTest(lx, ly));
        rows[focus.row].layoutDirty = true;
        return true;
    }
    return false;
}

bool ExpressionList::mouseUp(float x, float y) {
    bool was = m_sliderDrag >= 0 || m_selectingText || m_pressed.kind != ListHit::None;
    if ((m_pressed.kind == ListHit::Icon || m_pressed.kind == ListHit::ColumnIcon) && !m_longPressFired) {
        ListHit h = hitTest(x, y);
        if (h.kind == m_pressed.kind && h.row == m_pressed.row && h.col == m_pressed.col) {
            if (onContentWillChange) onContentWillChange();
            ExpressionRow& r = rows[h.row];
            if (h.kind == ListHit::ColumnIcon) r.columns[h.col].style.hidden = !r.columns[h.col].style.hidden;
            else { r.hidden = !r.hidden; r.syncStyle(); }
            if (onStyleChanged) onStyleChanged();
        }
    }
    if (m_dragRow >= 0 && m_dropIndex >= 0) {
        if (onContentWillChange) onContentWillChange();
        moveBlock(m_dragRow, m_dropIndex);
        changed();
        was = true;
    }
    m_dragCandidate = m_dragRow = m_dropIndex = -1;
    m_sliderDrag = -1;
    m_selectingText = false;
    m_pressed = {};
    m_pressTime = -1;
    return was;
}

bool ExpressionList::mouseMoved(float x, float y) {
    ListHit h = hitTest(x, y);
    bool changedHover = h.kind != m_hover.kind || h.row != m_hover.row || h.index != m_hover.index ||
                        h.col != m_hover.col || h.cell != m_hover.cell;
    m_hover = h;
    return changedHover;
}

void ExpressionList::selectAll() {
    if (MathTree* t = focusedTree()) { t->selectAll(); edited(focus.row); }
    else if (TextField* tf = focusedText()) tf->selectAll();
}

bool ExpressionList::key(Key k, Mods m, const std::u32string& text) {
    if (focus.row < 0 || focus.row >= int(rows.size())) return false;
    ExpressionRow& r = rows[focus.row];
    if (focus.field == Field::Text || focus.field == Field::Label) return keyText(k, m, text);
    if (r.type == RowType::Table) return keyTable(k, m, text);
    if (focus.field != Field::Main) return keyExpressionField(k, m, text);

    MathTree& t = r.tree;
    auto edit = [&](auto&& fn) {
        if (onContentWillChange) onContentWillChange();
        if (fn()) changed();
        else r.layoutDirty = true;
    };
    switch (k) {
    case Key::Char:
        // Desmos: typing a double quote into an empty expression turns it into a note.
        if (t.empty() && text == U"\"") {
            if (onContentWillChange) onContentWillChange();
            r.type = RowType::Note;
            r.text = {};
            focusField({focus.row, Field::Text});
            changed();
            return true;
        }
        edit([&] { return t.typeText(text); });
        return true;
    case Key::Backspace:
        if (t.empty() && !t.hasSelection() && rows.size() > 1) {
            if (onContentWillChange) onContentWillChange();
            int i = focus.row;
            removeRow(i);
            focusRow(std::max(0, i - 1));
            return true;
        }
        edit([&] { return t.backspace(); });
        return true;
    case Key::Delete:
        edit([&] { return t.deleteForward(); });
        return true;
    case Key::Left: t.moveLeft(m.shift); r.layoutDirty = true; return true;
    case Key::Right: t.moveRight(m.shift); r.layoutDirty = true; return true;
    case Key::Home: t.moveHome(m.shift); r.layoutDirty = true; return true;
    case Key::End: t.moveEnd(m.shift); r.layoutDirty = true; return true;
    case Key::Up: {
        if (t.moveUp()) { r.layoutDirty = true; return true; }
        int i = focus.row - 1;
        while (i >= 0 && !rows[i].visibleInList) --i;
        if (i >= 0) focusRow(i);
        return true;
    }
    case Key::Down: {
        if (t.moveDown()) { r.layoutDirty = true; return true; }
        // Into this row's own sub-fields first (domain), then the next row.
        if (r.analysis.graph == GraphKind::Parametric || r.analysis.graph == GraphKind::Polar) {
            focusField({focus.row, Field::DomainMin});
            return true;
        }
        int i = focus.row + 1;
        while (i < int(rows.size()) && !rows[i].visibleInList) ++i;
        if (i < int(rows.size())) focusRow(i);
        return true;
    }
    case Key::Enter:
        if (onContentWillChange) onContentWillChange();
        addRow(focus.row + 1);
        return true;
    case Key::Escape:
        blur();
        return true;
    case Key::Tab:
        return true;
    }
    return false;
}

// ---- animation ---------------------------------------------------------------------------------

bool ExpressionList::animating() const {
    if (m_pressTime != -1 && !m_longPressFired) return true;
    for (auto& r : rows)
        if (r.playing && r.analysis.kind == RowKind::Slider) return true;
    return false;
}

bool ExpressionList::tick(double now, double dt) {
    // Long press on a colour icon opens the style menu.
    if (m_pressTime == -2) m_pressTime = now;
    if (m_pressTime >= 0 && !m_longPressFired && now - m_pressTime >= kLongPressSeconds &&
        (m_pressed.kind == ListHit::Icon || m_pressed.kind == ListHit::ColumnIcon)) {
        m_longPressFired = true;
        m_pressTime = -1;
        if (onStyleMenu) {
            float top = rowTopOnScreen(m_pressed.row);
            Rect anchor = m_pressed.kind == ListHit::Icon ? Rect{area.x + 4, top + IconCenterY - 15, 30, 30}
                                                          : Rect{m_pressX - 10, m_pressY - 10, 20, 20};
            onStyleMenu(m_pressed.row, m_pressed.kind == ListHit::Icon ? -1 : m_pressed.col, anchor);
        }
    }
    // Playing sliders sweep back and forth across their range (Desmos default).
    bool changedAny = false;
    for (int i = 0; i < int(rows.size()); ++i) {
        ExpressionRow& r = rows[i];
        if (!r.playing || r.analysis.kind != RowKind::Slider) continue;
        double range = r.sliderMax - r.sliderMin;
        if (!(range > 0)) continue;
        double v = r.analysis.sliderValue + r.playDirection * range * dt / SliderSweepSeconds;
        if (v >= r.sliderMax) { v = r.sliderMax; r.playDirection = -1; }
        if (v <= r.sliderMin) { v = r.sliderMin; r.playDirection = 1; }
        if (r.sliderStep > 0) v = r.sliderMin + std::round((v - r.sliderMin) / r.sliderStep) * r.sliderStep;
        char buf[64];
        int decimals = std::clamp(int(std::ceil(-std::log10(range / 400.0))), 0, 10);
        std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
        std::string s = buf;
        if (s.find('.') != std::string::npos) {
            while (s.back() == '0') s.pop_back();
            if (s.back() == '.') s.pop_back();
        }
        if (s == "-0") s = "0";
        r.tree.clear();
        r.tree.insertLatex(r.analysis.definedName + "=" + s);
        r.layoutDirty = true;
        r.analysis.sliderValue = v;
        changedAny = true;
    }
    if (changedAny && onContentChanged) onContentChanged();
    return changedAny;
}

} // namespace calc
