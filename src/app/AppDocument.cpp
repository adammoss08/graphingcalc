// App: document state — undo snapshots, restore, new/open/save (Desmos-compatible JSON).
#include "app/App.hpp"
#include "app/GraphState.hpp"
#include "math/Latex.hpp"
#include "render/Renderer.hpp"

#include <chrono>
#include <fstream>
#include <map>
#include <sstream>

namespace calc {

using namespace theme;

namespace {

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

constexpr double kUndoCoalesceSeconds = 0.75;
constexpr size_t kMaxUndo = 200;

} // namespace

// ---- undo / snapshots --------------------------------------------------------------------------

SavedGraph App::snapshot() const {
    SavedGraph g;
    for (auto& r : m_list.rows) {
        SavedRow s;
        s.type = r.type;
        s.id = std::to_string(r.id);
        s.folderId = r.folderId ? std::to_string(r.folderId) : "";
        s.style = r.style;
        s.style.color = r.color;
        s.style.hidden = r.hidden;
        s.style.label = r.label.utf8();
        switch (r.type) {
        case RowType::Expression:
            s.latex = blockToLatex(r.tree.root());
            s.hasSlider = r.analysis.kind == RowKind::Slider;
            s.sliderMin = r.sliderMinField.latex();
            s.sliderMax = r.sliderMaxField.latex();
            s.sliderStep = r.sliderStepField.latex();
            s.domainMin = r.domainMin.latex();
            s.domainMax = r.domainMax.latex();
            break;
        case RowType::Note: case RowType::Folder:
            s.text = r.text.utf8();
            s.collapsed = r.collapsed;
            break;
        case RowType::Table:
            for (auto& c : r.columns) {
                SavedColumn sc;
                sc.header = c.header.latex();
                for (auto& cell : c.cells) sc.values.push_back(cell.latex());
                sc.style = c.style;
                s.columns.push_back(std::move(sc));
            }
            break;
        case RowType::Image:
            s.imagePath = r.imagePath;
            s.imageCenter = r.imageCenter.latex();
            s.imageWidth = r.imageWidth.latex();
            s.imageHeight = r.imageHeight.latex();
            s.imageOpacity = r.imageOpacity;
            break;
        }
        g.rows.push_back(std::move(s));
    }
    g.focused = m_list.focus.row;
    g.degrees = m_graph.settings.degrees;
    g.showGrid = m_graph.settings.grid;
    g.minorGrid = m_graph.settings.minorGrid;
    g.axisNumbers = m_graph.settings.axisNumbers;
    g.threeD = m_graph.settings.threeD;
    if (!m_graph.cam.isDefault()) {
        g.hasCamera3D = true;
        g.camAzimuth = m_graph.cam.azimuth;
        g.camElevation = m_graph.cam.elevation;
        g.camHalfSize = m_graph.cam.halfSize;
    }
    return g;
}

void App::restore(const SavedGraph& g) {
    auto& rows = m_list.rows;
    rows.clear();
    m_list.focus = {};
    std::map<std::string, uint32_t> ids;
    for (auto& s : g.rows) {
        ExpressionRow r;
        r.id = m_list.newId();
        if (!s.id.empty()) ids[s.id] = r.id;
        r.type = s.type;
        r.style = s.style;
        r.color = s.style.color;
        r.hidden = s.style.hidden;
        r.label.setUtf8(s.style.label);
        switch (s.type) {
        case RowType::Expression:
            r.tree.insertLatex(s.latex);
            r.sliderMinField.set(s.sliderMin.empty() ? "-10" : s.sliderMin);
            r.sliderMaxField.set(s.sliderMax.empty() ? "10" : s.sliderMax);
            r.sliderStepField.set(s.sliderStep);
            r.domainMin.set(s.domainMin);
            r.domainMax.set(s.domainMax);
            break;
        case RowType::Note: case RowType::Folder:
            r.text.setUtf8(s.text);
            r.collapsed = s.collapsed;
            break;
        case RowType::Table:
            for (auto& sc : s.columns) {
                TableColumn c;
                c.id = m_list.newId();
                c.header.set(sc.header);
                for (auto& v : sc.values) c.cells.emplace_back(v);
                c.style = sc.style;
                r.columns.push_back(std::move(c));
            }
            break;
        case RowType::Image:
            r.imagePath = s.imagePath;
            r.imageCenter.set(s.imageCenter);
            r.imageWidth.set(s.imageWidth);
            r.imageHeight.set(s.imageHeight);
            r.imageOpacity = s.imageOpacity;
            r.imageTexture = s.imagePath.empty() ? -1 : m_renderer->loadImage(s.imagePath, r.imageAspect);
            break;
        }
        r.syncStyle();
        rows.push_back(std::move(r));
    }
    // Resolve folder membership (by the saved string ids).
    for (size_t i = 0; i < g.rows.size(); ++i) {
        auto it = ids.find(g.rows[i].folderId);
        rows[i].folderId = (it != ids.end() && g.rows[i].type != RowType::Folder) ? it->second : 0;
    }
    // Continue the colour cycle after the last expression colour.
    int next = 0;
    for (auto it = g.rows.rbegin(); it != g.rows.rend(); ++it) {
        if (it->type != RowType::Expression) continue;
        for (int k = 0; k < ColorCycleCount; ++k)
            if (colorToHex(ColorCycle[k]) == colorToHex(it->style.color)) next = (k + 1) % ColorCycleCount;
        break;
    }
    m_list.setColorCycleIndex(next);
    m_graph.settings.degrees = g.degrees;
    m_graph.settings.grid = g.showGrid;
    m_graph.settings.minorGrid = g.minorGrid;
    m_graph.settings.axisNumbers = g.axisNumbers;
    m_graph.settings.threeD = g.threeD;
    if (g.focused >= 0 && g.focused < int(rows.size())) m_list.focusRow(g.focused);
    m_styleMenu.close();
    m_needsAnalysis = true;
    m_dirty = true;
}

void App::willChange() {
    double t = nowSeconds();
    bool coalesce = t - m_lastSnapshotTime < kUndoCoalesceSeconds && m_lastSnapshotRow == m_list.focus.row &&
                    !m_undo.empty();
    m_lastSnapshotTime = t;
    m_lastSnapshotRow = m_list.focus.row;
    if (coalesce) return;
    m_undo.push_back(snapshot());
    if (m_undo.size() > kMaxUndo) m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void App::contentChanged() {
    m_needsAnalysis = true;
    m_modified = true;
    m_dirty = true;
    resetCursorBlink();
}

void App::undo() {
    if (m_undo.empty()) return;
    m_redo.push_back(snapshot());
    SavedGraph g = std::move(m_undo.back());
    m_undo.pop_back();
    restore(g);
    m_lastSnapshotTime = 0;
}

void App::redo() {
    if (m_redo.empty()) return;
    m_undo.push_back(snapshot());
    SavedGraph g = std::move(m_redo.back());
    m_redo.pop_back();
    restore(g);
    m_lastSnapshotTime = 0;
}

void App::newGraph() {
    m_undo.push_back(snapshot());
    m_list.rows.clear();
    m_list.focus = {};
    m_list.resetColorCycle();
    m_list.addRow(0, "", true);
    m_graph.resetAllViews();
    m_path.clear();
    m_modified = false;
    m_needsAnalysis = true;
    m_dirty = true;
}

bool App::loadFile(const std::string& path, std::string& error) {
    std::ifstream f(path);
    if (!f) { error = "Couldn't open " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    SavedGraph g;
    if (!parseGraph(ss.str(), g, error)) return false;
    // Relative image paths are relative to the graph file.
    std::string dir = path.substr(0, path.find_last_of('/') + 1);
    for (auto& r : g.rows)
        if (r.type == RowType::Image && !r.imagePath.empty() && r.imagePath[0] != '/') r.imagePath = dir + r.imagePath;
    m_undo.clear();
    m_redo.clear();
    g.focused = -1;
    restore(g);
    if (m_list.rows.empty()) m_list.addRow(0, "", false);
    m_graph.resetAllViews();
    if (g.hasViewport) setView(g.xmin, g.xmax, g.ymin, g.ymax);
    if (g.hasCamera3D) {
        setCamera3D(g.camAzimuth, g.camElevation, g.camHalfSize);
    }
    m_path = path;
    m_modified = false;
    return true;
}

bool App::saveFile(const std::string& path, std::string& error) {
    SavedGraph g = snapshot();
    const Viewport& vp = m_graph.vp;
    g.hasViewport = true;
    g.xmin = vp.xmin();
    g.xmax = vp.xmax();
    g.ymin = vp.ymin();
    g.ymax = vp.ymax();
    std::ofstream f(path);
    if (!f) { error = "Couldn't write " + path; return false; }
    f << serializeGraph(g);
    m_path = path;
    m_modified = false;
    m_dirty = true;
    return true;
}

} // namespace calc
