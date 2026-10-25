#pragma once
// The graph paper: grid/axes/labels, curve sampling + caching, points of interest, hover/trace,
// pan/zoom, zoom buttons and the graph-settings popover.
#include "graph/Camera3D.hpp"
#include "graph/Poi.hpp"
#include "graph/Sampler.hpp"
#include "graph/Viewport.hpp"
#include "render/Batch2D.hpp"
#include "render/Renderer.hpp"
#include "ui/ExpressionList.hpp"
#include "ui/RowModel.hpp"

#include <functional>
#include <unordered_map>

namespace calc {

struct GraphSettings {
    bool grid = true;
    bool minorGrid = true;
    bool axisNumbers = true;
    bool xAxis = true, yAxis = true;
    bool degrees = false;
    bool threeD = false;     // 3D mode (Desmos 3D): orbit camera, surfaces, space curves
};

enum class CursorKind { Arrow, IBeam, Pointer, Grab, Grabbing };

// One drawable thing on the graph: an expression row, or one column of a table.
struct GraphItem {
    const RowAnalysis* an;
    Style style;
    uint64_t key;               // cache key base: unique per (row, column); pieces add their index
    int row;                    // expression-list row (selection / highlight)
    bool connectPoints = false; // table columns: points may be joined by lines
};

struct ImageItem {
    int texture = -1;
    double cx = 0, cy = 0, w = 0, h = 0;  // world units
    float opacity = 1;
    int row = -1;
};

class GraphView {
public:
    Viewport vp;
    Rect area;
    GraphSettings settings;
    Camera3D cam;                                 // 3D mode view
    std::function<void(int row)> onSelectRow;     // clicked a curve (-1: clicked empty paper)
    std::function<void()> onSettingsChanged;      // degrees toggled etc. (re-analyse)
    // Heavy curves resample on a background queue while the old samples are drawn rescaled
    // (progressive refinement). Off for headless use: results arrive via the main queue.
    bool asyncSampling = false;
    std::function<void()> onAsyncUpdate;          // a background resample landed (redraw)
    // Dragging a point whose coordinates are sliders: start (undo snapshot), then each move.
    std::function<void()> onDragPointStart;
    std::function<void(int row, size_t piece, double x, double y)> onDragPoint;
    // Point labels: value of a variable for "${a}" (false if unknown).
    std::function<bool(const std::string& name, double& value)> valueOf;
    size_t samplesInFlight() const { return m_inflight.size(); }

    void setArea(const Rect& r);
    void resetView();          // the current mode's view (2D viewport or 3D camera)
    void resetAllViews();      // both (new graph / file load)
    bool viewIsDefault() const;
    void zoomBy(double factor);

    // Samples whatever is stale (in parallel) and fills `out` with this frame's curves.
    std::vector<ImageItem> images;   // set by the app before buildFrame
    void buildFrame(const std::vector<GraphItem>& items, int selected, GraphFrame& out);
    void drawUnder(Batch2D& b) const;
    void drawOver(Batch2D& b, const std::vector<GraphItem>& items, int selected);
    void drawControls(Batch2D& b) const;

    bool mouseDown(float x, float y, int clicks, Mods m, const std::vector<GraphItem>& items);
    bool mouseDragged(float x, float y, const std::vector<GraphItem>& items);
    bool mouseUp(float x, float y);
    bool mouseMoved(float x, float y, const std::vector<GraphItem>& items);
    bool mouseExited();
    bool scroll(float x, float y, float dx, float dy, bool precise);
    bool magnify(float x, float y, float amount);
    CursorKind cursorAt(float x, float y) const;
    bool settingsOpen() const { return m_settingsOpen; }
    void closeSettings() { m_settingsOpen = false; }

    size_t cachedCurveCount() const { return m_cache.size(); }
    double lastSampleMs() const { return m_lastSampleMs; }

private:
    enum class Button { None, Wrench, ZoomIn, ZoomOut, Home, Mode };
    struct Trace {
        int item = -1, piece = -1;
        double x = 0, y = 0;
        bool active = false;
    };
    struct PoiCache {
        uint32_t rowId = 0;
        uint64_t hash = 0;
        Viewport vp;
        std::vector<Poi> pois;
    };

    std::unordered_map<uint64_t, CurvePoints> m_cache;
    std::unordered_map<uint64_t, uint64_t> m_inflight;   // cache key -> generation of its background job
    uint64_t m_generation = 0;
    void scheduleAsync(uint64_t key, const GraphPiece& pc, GraphKind kind, double t0, double t1, uint64_t hash);
    static void asyncWork(void* ctx);
    static void asyncDone(void* ctx);
    bool m_initialised = false;
    Button m_hoverButton = Button::None, m_pressButton = Button::None;
    int m_hoverSetting = -1;
    bool m_settingsOpen = false;
    bool m_panning = false;
    float m_lastX = 0, m_lastY = 0, m_downX = 0, m_downY = 0;
    int m_hoverRow = -1;
    Trace m_trace;
    Trace m_dragPoint;            // active point drag (item/piece)
    int m_hoverDraggable = -1;    // item index of a hovered draggable point
    std::string labelText(const Style& st, double x, double y) const;
    PoiCache m_poi;
    int m_hoverPoi = -1;
    double m_lastSampleMs = 0;

    // 3D mode (GraphView3D.cpp). Space curves and 2D curves laid on z = 0 are sampled in world
    // units, independent of the camera, so orbiting reuses them.
    struct Curve3D {
        uint64_t hash = 0;
        std::vector<simd_float3> pts;   // NaN x breaks the curve
    };
    std::unordered_map<uint64_t, Curve3D> m_cache3d;
    // Implicit surfaces: grid bounds from a coarse CPU probe (where F is defined / changes sign).
    struct ImplicitBox3D {
        uint64_t hash = 0;
        bool any = false;        // F defined somewhere in the box
        simd_float3 lo, hi;
    };
    std::unordered_map<uint64_t, ImplicitBox3D> m_box3d;
    void buildFrame3D(const std::vector<GraphItem>& items, int selected, GraphFrame& out);
    void drawOver3D(Batch2D& b, const std::vector<GraphItem>& items) const;
    double gridStep3D() const;

    Rect buttonRect(Button b) const;
    Button buttonAt(float x, float y) const;
    Rect settingsRect() const;
    int settingAt(float x, float y) const;
    bool pickCurve(float x, float y, const std::vector<GraphItem>& items, Trace& out, int onlyItem = -1) const;
    void updatePois(const std::vector<GraphItem>& items, int selected);
};

} // namespace calc
