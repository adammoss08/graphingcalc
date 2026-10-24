#pragma once
// Popover menus: the style menu (from a colour icon) and the "+" add menu.
#include "render/Batch2D.hpp"
#include "ui/RowModel.hpp"

#include <functional>

namespace calc {

class StyleMenu {
public:
    bool open = false;
    int row = -1, col = -1;          // col >= 0: a table column
    Rect anchor;
    bool showLines = true, showFill = false, showPoints = false, showPointToggles = false;
    std::function<void()> onWillChange;  // first edit of an open menu (undo snapshot)

    void show(int r, int c, Rect a, bool lines, bool fill, bool points, bool pointToggles);
    void close() { open = false; }
    Rect rect(float viewW, float viewH) const;
    void draw(Batch2D& b, const Style& s, float viewW, float viewH) const;
    // Returns true if the event was inside the menu (consumed). `s` is edited in place.
    bool mouseDown(float x, float y, Style& s, float viewW, float viewH);
    bool mouseDragged(float x, float y, Style& s, float viewW, float viewH);
    void mouseUp() { m_drag = Drag::None; }
    bool mouseMoved(float x, float y, float viewW, float viewH);

private:
    enum class Drag { None, Width, Opacity, Fill, PointSize } m_drag = Drag::None;
    int m_hover = -1;
    bool m_changed = false;
    struct Layout {
        Rect box;
        Rect swatches[theme::ColorPaletteCount];
        Rect lineStyles[3];
        Rect widthTrack, opacityTrack, fillTrack, pointStyles[3], sizeTrack, pointsToggle, linesToggle;
        float height;
    };
    Layout layout(float viewW, float viewH) const;
    void dragTo(Drag d, float x, Style& s, const Layout& L);
    void willChange();
};

class AddMenu {
public:
    enum Item { Expression, Note, Table, Folder, Image, Count };
    bool open = false;
    Rect anchor;
    void draw(Batch2D& b) const;
    int itemAt(float x, float y) const;   // -1 if none
    Rect rect() const;
    bool mouseMoved(float x, float y);
private:
    int m_hover = -1;
};

} // namespace calc
