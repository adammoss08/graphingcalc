#pragma once
// The left-hand expression list. Rows are expressions, notes, folders, tables or images; each row
// lays out a set of focusable fields (main math, slider bounds, domain limits, table cells, image
// fields, note text). Keyboard and keypad input go to the focused field.
//   ExpressionList.cpp  container: rows, focus, layout dispatch, scrolling, keyboard, shared chrome
//   RowExpression.cpp   expression rows (math, "= value", sliders + play, domains, regressions)
//   RowTable.cpp        tables
//   RowMisc.cpp         notes, folders, images
#include "math/Analyzer.hpp"
#include "render/Batch2D.hpp"
#include "ui/MathView.hpp"
#include "ui/RowModel.hpp"
#include "ui/Widgets.hpp"

#include <functional>
#include <string>
#include <vector>

namespace calc {

enum class Key { Char, Left, Right, Up, Down, Backspace, Delete, Enter, Escape, Tab, Home, End };

struct Mods {
    bool shift = false, cmd = false, alt = false, ctrl = false;
};

// What a pointer press landed on.
struct ListHit {
    enum Kind {
        None, Field, Icon, Delete, Slider, Play, Suggestion, SuggestionAll, NewRow, Tab, FolderToggle,
        ColumnIcon, EvalToggle,
    } kind = None;
    int row = -1;
    calc::Field field = calc::Field::Main;
    int col = 0, cell = 0;
    int index = 0;   // suggestion index
};

// A focusable field's on-screen box, computed by layout (x absolute, y relative to the row top).
struct FieldBox {
    calc::Field field;
    int col = 0, cell = 0;
    Rect rect;
    float baseline = 0;  // relative to the row top
    float em = 0;
    bool underline = false;  // inline field style (Desmos .dcg-math-field)
};

class ExpressionList {
public:
    std::vector<ExpressionRow> rows;
    Focus focus;
    Rect area;              // visible list area on screen
    float scrollY = 0;
    TextAtlas* atlas = nullptr;

    // Callbacks into the app.
    std::function<void()> onContentWillChange;   // before an undoable change (snapshot)
    std::function<void()> onContentChanged;      // after text/rows changed (re-analyse)
    std::function<void()> onStyleChanged;        // colour/visibility changed (redraw only)
    std::function<void(int row, int col, Rect anchor)> onStyleMenu;  // open the style popover

    // ---- rows ----
    int addRow(int at, const std::string& latex = "", bool focus = true, RowType type = RowType::Expression);
    int addTable(int at, bool focus = true);
    void removeRow(int i);
    Color nextColor();
    void resetColorCycle() { m_colorIdx = 0; }
    void setColorCycleIndex(int i) { m_colorIdx = i; }
    uint32_t newId() { return m_nextId++; }
    void noteId(uint32_t id) { if (id >= m_nextId) m_nextId = id + 1; }
    int indexOfId(uint32_t id) const;
    bool inFolder(int i, uint32_t folderId) const { return rows[i].folderId == folderId && folderId != 0; }
    int folderEnd(int folderRow) const;   // one past the folder's last child

    // ---- focus ----
    int focusedRow() const { return focus.row; }
    void focusRow(int i, bool atEnd = true);
    void focusField(const Focus& f, bool atEnd = true);
    void blur() { focusField({}); }
    bool editing() const { return focus.row >= 0; }
    MathTree* focusedTree();
    MathField* fieldFor(const Focus& f);       // non-main math fields (may create table cells)
    TextField* focusedText();
    const MathTree* treeFor(const Focus& f) const;

    // ---- layout / drawing ----
    void layout(TextAtlas& atlas);
    float contentHeight() const;
    void scrollBy(float dy);
    void scrollToRow(int i);
    float rowTopOnScreen(int i) const { return area.y + rows[i].y - scrollY; }
    void draw(Batch2D& b, bool cursorVisible) const;
    const std::vector<FieldBox>& boxes(int row) const { return m_boxes[row]; }

    // ---- input ----
    ListHit hitTest(float x, float y) const;
    bool mouseDown(float x, float y, int clicks, Mods m);
    bool mouseDragged(float x, float y);
    bool mouseUp(float x, float y);
    bool mouseMoved(float x, float y);
    bool key(Key k, Mods m, const std::u32string& text);
    void selectAll();

    // ---- animation (slider play, long-press) ----
    bool animating() const;
    bool tick(double now, double dt);   // returns true if content changed

private:
    int m_colorIdx = 0;
    uint32_t m_nextId = 1;
    ListHit m_hover, m_pressed;
    bool m_selectingText = false;
    int m_sliderDrag = -1;
    int m_dragCandidate = -1;          // pressed a tab: becomes a reorder drag past the threshold
    int m_dragRow = -1;                // row (block) being dragged
    int m_dropIndex = -1;              // insert position (row index) for the drop
    float m_dragY = 0;
    double m_pressTime = -1;           // icon long-press (opens the style menu)
    float m_pressX = 0, m_pressY = 0;
    bool m_longPressFired = false;
    std::vector<std::vector<FieldBox>> m_boxes;
    // Horizontal scroll of the focused math field when its content is wider than the box (MathQuill
    // scrolls to keep the caret visible). Other fields draw from their start.
    Focus m_scrollFocus;
    float m_scrollX = 0;
    void updateFieldScroll();
    float fieldScroll(const Focus& f) const { return f == m_scrollFocus ? m_scrollX : 0; }

    float contentX(const ExpressionRow& r) const { return area.x + theme::TabWidth + theme::MathLeftPad + r.indent; }
    float contentRight() const { return area.right() - theme::MathRightPad; }
    const FieldBox* boxFor(int row, const Focus& f) const;
    void changed();
    void edited(int row);              // a field of `row` changed: relayout + notify

    // per row type (RowExpression.cpp / RowTable.cpp / RowMisc.cpp)
    float layoutExpression(int i, std::vector<FieldBox>& boxes);
    void drawExpression(Batch2D& b, int i, float top, bool sel, bool hover, bool cursorVisible) const;
    ListHit hitExpression(int i, float x, float y) const;
    Rect sliderTrack(int i) const;
    std::vector<Rect> suggestionRects(int i) const;
    Rect playButton(int i) const;
    void setSliderFromX(int i, float x);
    bool keyExpressionField(Key k, Mods m, const std::u32string& text);

    float layoutTable(int i, std::vector<FieldBox>& boxes);
    void drawTable(Batch2D& b, int i, float top, bool sel, bool cursorVisible) const;
    ListHit hitTable(int i, float x, float y) const;
    bool keyTable(Key k, Mods m, const std::u32string& text);
    void trimTable(ExpressionRow& r, bool keepFocused);
    int tableRowCount(const ExpressionRow& r) const;

    float layoutMisc(int i, std::vector<FieldBox>& boxes);
    void drawMisc(Batch2D& b, int i, float top, bool sel, bool cursorVisible) const;
    ListHit hitMisc(int i, float x, float y) const;
    bool keyText(Key k, Mods m, const std::u32string& text);

    void drawFieldBox(Batch2D& b, int row, const FieldBox& fb, float top, bool cursorVisible, Color color) const;
    void drawTextLine(Batch2D& b, const TextField& tf, float x, float baseline, bool focused, bool cursorVisible,
                      const char* placeholder) const;
    int dropIndexAt(float y) const;
    void moveBlock(int from, int dropBefore);
public:
    bool reordering() const { return m_dragRow >= 0; }
    float focusedFieldScroll() const { return fieldScroll(focus); }
    friend class App;
};

} // namespace calc
