#pragma once
// Application core: owns the expression list, graph view and renderer; turns platform events into
// actions; re-analyses and redraws only when something changed. Platform.mm is a thin shell
// around this class. Keep Objective-C out of this header.
#include "ui/ExpressionList.hpp"
#include "ui/GraphView.hpp"
#include "ui/Keypad.hpp"
#include "ui/Popovers.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MTL { class Device; class Texture; }
namespace CA { class MetalDrawable; }

namespace calc {

class Renderer;
struct SavedGraph;

class App {
public:
    App();
    ~App();

    bool init(MTL::Device* device, const std::string& metallibPath, std::string& error);
    void setSize(float widthPts, float heightPts, float scale);
    // Rasterises the common glyphs at `scale` so the first frame doesn't wait on CoreText.
    void warmUp(float scale);

    // Rendering
    bool needsRedraw() const { return m_dirty || animating() || m_renderer->wantsAnotherFrame(); }
    void setNeedsRedraw() { m_dirty = true; }
    bool animating() const { return m_list.animating(); }
    void render(MTL::Texture* target, CA::MetalDrawable* drawable, bool wait = false);
    // Renders one frame offscreen at the current size and writes a PNG (no window needed).
    bool screenshot(const std::string& path);
    bool wantsBlinkTimer() const { return m_list.editing(); }
    void blinkTick();   // toggles the cursor (call every theme::CursorBlinkSeconds)
    bool perfHud = false;

    // Events (points, origin top-left of the content view)
    void mouseDown(float x, float y, int clicks, Mods m);
    void mouseDragged(float x, float y);
    void mouseUp(float x, float y);
    void mouseMoved(float x, float y);
    void mouseExited();
    void scroll(float x, float y, float dx, float dy, bool precise);
    void magnify(float x, float y, float amount);
    void key(Key k, Mods m, const std::u32string& text = {});
    CursorKind cursorAt(float x, float y) const;
    bool isWindowDragRegion(float x, float y) const;

    // Commands
    void undo();
    void redo();
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    std::string copyText();            // LaTeX of the selection (or whole field) / note text
    void pasteText(const std::string& text);
    void cut();
    void selectAll();
    void addExpression(const std::string& latex);
    void addItem(int addMenuItem);     // AddMenu::Item
    bool addImage(const std::string& path);
    void focusRow(int i);              // 0-based, -1 = none
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void setView(double xmin, double xmax, double ymin, double ymax);
    void hoverAt(float x, float y) { mouseMoved(x, y); }
    void toggleKeypad() { m_keypad.open = !m_keypad.open; m_dirty = true; }
    void toggle3D() { m_graph.settings.threeD = !m_graph.settings.threeD; m_needsAnalysis = true; m_dirty = true; }
    bool is3D() const { return m_graph.settings.threeD; }
    void setDarkMode(bool on) { theme::setDarkMode(on); m_dirty = true; }
    void setCamera3D(double azimuth, double elevation, double halfSize) {
        Camera3D& c = m_graph.cam;
        c.orbit(azimuth - c.azimuth, elevation - c.elevation);
        c.halfSize = halfSize;
        m_dirty = true;
    }
    void openStyleMenu(int row, int col);  // for tests / CLI

    std::function<void()> onRequestImage;  // platform: show an open panel, then call addImage()
    std::function<void()> onRedrawNeeded;  // platform: something changed off the event path (background work)

    void newGraph();
    bool loadFile(const std::string& path, std::string& error);
    bool saveFile(const std::string& path, std::string& error);
    const std::string& filePath() const { return m_path; }
    std::string title() const;
    bool modified() const { return m_modified; }

    Renderer& renderer() { return *m_renderer; }
    ExpressionList& list() { return m_list; }
    GraphView& graph() { return m_graph; }
    double lastFrameCpuMs() const { return m_lastFrameMs; }
    const std::vector<GraphItem>& items() { rebuildItems(); return m_items; }

private:
    enum class TopButton { None, Add, Undo, Redo, Collapse, Expand, Keypad };

    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<Batch2D> m_ui;
    std::unique_ptr<GraphFrame> m_frame;
    ExpressionList m_list;
    GraphView m_graph;
    StyleMenu m_styleMenu;
    AddMenu m_addMenu;
    Keypad m_keypad;
    std::vector<GraphItem> m_items;
    std::vector<std::unique_ptr<MathTree>> m_synthetic;   // table columns as analysis rows
    float m_w = 1200, m_h = 800, m_scale = 2;
    float m_panelW = theme::PanelDefaultWidth;
    bool m_panelCollapsed = false;
    bool m_dirty = true;
    bool m_needsAnalysis = true;
    bool m_cursorVisible = true;
    bool m_modified = false;
    std::string m_path;
    double m_lastFrameMs = 0;
    double m_lastTick = 0;

    // pointer routing
    enum class Capture { None, List, Graph, PanelResize, TopBar, StyleMenu, Keypad } m_capture = Capture::None;
    TopButton m_hoverTop = TopButton::None, m_pressTop = TopButton::None;
    bool m_hoverDivider = false;

    // undo
    std::vector<SavedGraph> m_undo, m_redo;
    double m_lastSnapshotTime = 0;
    int m_lastSnapshotRow = -2;

    void layoutRects();
    Rect headerRect() const;
    Rect topBarRect() const;
    Rect graphRect() const;
    Rect listRect() const;
    Rect topButtonRect(TopButton b) const;
    TopButton topButtonAt(float x, float y) const;
    bool onDivider(float x, float y) const;

    void analyze();
    void rebuildItems();
    Style* styleTarget();
    void drawChrome(Batch2D& b);
    void drawPerfHud(Batch2D& b);
    SavedGraph snapshot() const;
    void restore(const SavedGraph& g);
    void willChange();
    void contentChanged();
    void resetCursorBlink() { m_cursorVisible = true; }
    void keypadKey(const KeypadKey& k);
};

} // namespace calc
