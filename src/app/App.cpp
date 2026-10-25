#include "app/App.hpp"
#include "Log.hpp"
#include "app/GraphState.hpp"
#include "math/Latex.hpp"
#include "math/Parser.hpp"
#include "render/Renderer.hpp"

#include <Metal/Metal.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
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

constexpr float kDividerGrab = 4;

bool rowGraphs(const ExpressionRow& r) {
    return r.type == RowType::Expression &&
           (r.analysis.kind == RowKind::Graph ||
            (r.analysis.kind == RowKind::FunctionDef && r.analysis.graph != GraphKind::None));
}

// "x_{1}" if the header is a single variable, else "".
std::string headerVariable(const MathField& h) {
    if (h.empty()) return {};
    try {
        AstPool p;
        Statement st = parseStatement(h.tree.root(), p, {});
        if (st.exprs.size() == 1 && st.rels.empty() && !st.regression && p[st.exprs[0]].op == AstOp::Var)
            return p[st.exprs[0]].name;
    } catch (const ParseError&) {
    }
    return {};
}

} // namespace

App::App() = default;
App::~App() = default;

bool App::init(MTL::Device* device, const std::string& metallibPath, std::string& error) {
    m_renderer = std::make_unique<Renderer>();
    if (!m_renderer->init(device, metallibPath, error)) return false;
    m_ui = std::make_unique<Batch2D>(m_renderer->atlas());
    m_frame = std::make_unique<GraphFrame>();
    m_list.atlas = &m_renderer->atlas();
    m_list.onContentWillChange = [this] { willChange(); };
    m_list.onContentChanged = [this] { contentChanged(); };
    m_list.onStyleChanged = [this] { m_modified = true; m_dirty = true; };
    m_list.onStyleMenu = [this](int row, int col, Rect anchor) {
        const ExpressionRow& r = m_list.rows[row];
        if (col >= 0) {
            m_styleMenu.show(row, col, anchor, true, false, true, true);
        } else if (r.type == RowType::Expression) {
            const RowAnalysis& an = r.analysis;
            bool points = an.graph == GraphKind::Point;
            m_styleMenu.show(row, -1, anchor, !points, an.rel != Cmp::Eq, points, false, points);
        } else {
            return;
        }
        m_dirty = true;
    };
    m_styleMenu.onWillChange = [this] { willChange(); m_lastSnapshotTime = 0; };
    m_graph.onSelectRow = [this](int r) {
        if (r >= 0) m_list.focusRow(r);
        else m_list.blur();
        m_dirty = true;
    };
    m_graph.onSettingsChanged = [this] { m_needsAnalysis = true; m_dirty = true; };
    m_graph.valueOf = [this](const std::string& name, double& v) {
        for (const auto& r : m_list.rows) {
            if (r.type != RowType::Expression || r.analysis.definedName != name) continue;
            if (r.analysis.kind == RowKind::Slider) { v = r.analysis.sliderValue; return true; }
            if (r.analysis.showValue) { v = r.analysis.value; return true; }
        }
        return false;
    };
    m_graph.onDragPointStart = [this] { willChange(); m_lastSnapshotTime = 0; };
    m_graph.onDragPoint = [this](int row, size_t piece, double x, double y) {
        if (row < 0 || row >= int(m_list.rows.size())) return;
        const auto& pieces = m_list.rows[row].analysis.pieces;
        if (piece >= pieces.size()) return;
        std::string vars[2] = {pieces[piece].dragX, pieces[piece].dragY};
        double vals[2] = {x, y};
        for (int k = 0; k < 2; ++k) {
            if (vars[k].empty()) continue;
            for (auto& r : m_list.rows) {
                if (r.type != RowType::Expression || r.analysis.kind != RowKind::Slider || r.analysis.definedName != vars[k])
                    continue;
                // Snap to the slider's step, otherwise to what one screen point can resolve.
                double v = r.sliderStep > 0 ? r.sliderMin + std::round((vals[k] - r.sliderMin) / r.sliderStep) * r.sliderStep
                                            : roundForDisplay(vals[k], m_graph.vp.scale);
                r.tree.clear();
                r.tree.insertLatex(vars[k] + "=" + formatNumber(v));
                r.layoutDirty = true;
                r.analysis.sliderValue = v;
            }
        }
        contentChanged();
    };
    m_graph.onAsyncUpdate = [this] {
        m_dirty = true;
        if (onRedrawNeeded) onRedrawNeeded();
    };
    m_list.addRow(0, "", true);
    m_modified = false;
    return true;
}

void App::warmUp(float scale) {
    TextAtlas& a = m_renderer->atlas();
    a.setScale(scale);
    const char32_t* common = U"0123456789.,+-=()xyabcdefghijklmnopqrstuvwz−θπ";
    for (const char32_t* c = common; *c; ++c) {
        a.glyph(Font::MathItalic, *c, MathSize);
        a.glyph(Font::MathRoman, *c, MathSize);
        a.glyph(Font::Ui, *c, LabelSize);
    }
    for (char32_t c : std::u32string(U"Untitled Graph")) a.glyph(Font::Ui, c, HeaderTitleSize * 0.85f);
    for (char32_t c : std::u32string(U"graphingcalc")) a.glyph(Font::HeaderLight, c, HeaderTitleSize + 4);
    for (char32_t c = '0'; c <= '9'; ++c) a.glyph(Font::Ui, c, TabIndexSize);
}

void App::setSize(float w, float h, float scale) {
    m_w = w;
    m_h = h;
    m_scale = scale;
    m_renderer->atlas().setScale(scale);
    for (auto& r : m_list.rows) r.layoutDirty = true;
    layoutRects();
    m_dirty = true;
}

// ---- layout ------------------------------------------------------------------------------------

Rect App::headerRect() const { return {0, 0, m_w, HeaderHeight}; }

Rect App::topBarRect() const {
    float pw = m_panelCollapsed ? 0 : m_panelW;
    return {0, HeaderHeight + 1, pw, TopBarHeight};
}

Rect App::graphRect() const {
    float x = m_panelCollapsed ? 0 : m_panelW;
    return {x, HeaderHeight + 1, m_w - x, m_h - HeaderHeight - 1 - m_keypad.height()};
}

Rect App::listRect() const {
    Rect tb = topBarRect();
    return {0, tb.bottom() + 1, std::max(0.0f, tb.w - 1),
            std::max(0.0f, m_h - tb.bottom() - 1 - m_keypad.height())};
}

void App::layoutRects() {
    m_panelW = std::clamp(m_panelW, PanelMinWidth, std::max(PanelMinWidth, m_w * 0.6f));
    m_list.area = listRect();
    m_graph.setArea(graphRect());
}

Rect App::topButtonRect(TopButton b) const {
    Rect tb = topBarRect();
    float s = IconButtonSize, y = tb.y + (tb.h - s) * 0.5f;
    switch (b) {
    case TopButton::Add: return {tb.x + 5, y, s, s};
    case TopButton::Collapse: return {tb.right() - 5 - s, y, s, s};
    case TopButton::Redo: return {tb.right() - 5 - 2 * s - 4, y, s, s};
    case TopButton::Undo: return {tb.right() - 5 - 3 * s - 4, y, s, s};
    case TopButton::Expand: {
        Rect g = graphRect();
        return {g.x + ZoomButtonInset, g.y + ZoomButtonInset, ZoomButtonSize, ZoomButtonSize};
    }
    case TopButton::Keypad: {
        // Bottom-left of the window, above the keypad when it's open (Desmos "show keypad").
        float bottom = m_h - m_keypad.height() - ZoomButtonInset;
        return {ZoomButtonInset, bottom - ZoomButtonSize, ZoomButtonSize + 12, ZoomButtonSize};
    }
    default: return {};
    }
}

App::TopButton App::topButtonAt(float x, float y) const {
    if (topButtonRect(TopButton::Keypad).contains(x, y)) return TopButton::Keypad;
    if (m_panelCollapsed) return topButtonRect(TopButton::Expand).contains(x, y) ? TopButton::Expand : TopButton::None;
    for (TopButton b : {TopButton::Add, TopButton::Undo, TopButton::Redo, TopButton::Collapse})
        if (topButtonRect(b).contains(x, y)) return b;
    return TopButton::None;
}

bool App::onDivider(float x, float y) const {
    return !m_panelCollapsed && y > HeaderHeight && y < m_h - m_keypad.height() && std::fabs(x - m_panelW) <= kDividerGrab;
}

bool App::isWindowDragRegion(float x, float y) const { return headerRect().contains(x, y); }

std::string App::title() const {
    if (m_path.empty()) return "Untitled Graph";
    std::string name = m_path.substr(m_path.find_last_of('/') + 1);
    auto dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

// ---- analysis ----------------------------------------------------------------------------------

void App::analyze() {
    auto& rows = m_list.rows;
    m_synthetic.clear();
    std::vector<RowInput> in(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        ExpressionRow& r = rows[i];
        if (r.type == RowType::Expression) {
            in[i].tree = &r.tree;
            in[i].domainMin = &r.domainMin.tree;
            in[i].domainMax = &r.domainMax.tree;
            in[i].fields = {&r.sliderMinField.tree, &r.sliderMaxField.tree, &r.sliderStepField.tree};
        } else if (r.type == RowType::Image) {
            in[i].fields = {&r.imageCenter.tree, &r.imageWidth.tree, &r.imageHeight.tree};
        }
    }
    // Tables become synthetic rows: each named column defines a list, and every other column is
    // plotted against the first as points.
    struct Synthetic { size_t row; size_t col; bool points; };
    std::vector<Synthetic> syn;
    auto addSynthetic = [&](const std::string& latex, size_t row, size_t col, bool points) {
        auto t = std::make_unique<MathTree>();
        t->insertLatex(latex);
        RowInput ri;
        ri.tree = t.get();
        in.push_back(ri);
        m_synthetic.push_back(std::move(t));
        syn.push_back({row, col, points});
    };
    for (size_t i = 0; i < rows.size(); ++i) {
        ExpressionRow& r = rows[i];
        if (r.type != RowType::Table || r.columns.empty()) continue;
        std::string first = headerVariable(r.columns[0].header);
        for (size_t c = 0; c < r.columns.size(); ++c) {
            TableColumn& col = r.columns[c];
            std::string name = headerVariable(col.header);
            col.computed = name.empty() && !col.header.empty() && c > 0;
            col.analysis = RowAnalysis{};
            col.computedValues.clear();
            if (!name.empty()) {
                std::string list = name + "=\\left[";
                for (size_t k = 0; k < col.cells.size(); ++k) {
                    if (k) list += ",";
                    list += col.cells[k].empty() ? "\\frac{0}{0}" : col.cells[k].latex();
                }
                list += "\\right]";
                addSynthetic(list, i, c, false);
            }
            if (c > 0 && !first.empty() && !col.header.empty())
                addSynthetic("\\left(" + first + "," + (name.empty() ? col.header.latex() : name) + "\\right)", i, c, true);
        }
    }

    auto res = analyzeGraph(in, {m_graph.settings.degrees, m_graph.settings.threeD});
    for (size_t i = 0; i < rows.size(); ++i) {
        ExpressionRow& r = rows[i];
        if (r.type != RowType::Expression && r.type != RowType::Image) { r.analysis = RowAnalysis{}; continue; }
        if (log::verbose() && res[i].error != r.analysis.error && !res[i].error.empty())
            log::info("row %zu: %s", i + 1, res[i].error.c_str());
        r.analysis = std::move(res[i]);
        if (r.type != RowType::Expression) continue;
        // Slider bounds come from their fields (which may reference other definitions).
        const auto& fv = r.analysis.fieldValues;
        auto value = [&](size_t k, double def) {
            return k < fv.size() && fv[k].size() == 1 && std::isfinite(fv[k][0]) ? fv[k][0] : def;
        };
        r.sliderMin = value(0, -10);
        r.sliderMax = value(1, 10);
        r.sliderStep = std::max(0.0, value(2, 0));
        if (r.sliderMax <= r.sliderMin) r.sliderMax = r.sliderMin + 1;
        if (r.analysis.kind == RowKind::Slider) {
            // Desmos widens the slider range when a typed value falls outside it.
            bool editingBounds = m_list.focus.row == int(i) && m_list.focus.field != Field::Main;
            double v = r.analysis.sliderValue;
            if (!editingBounds && v < r.sliderMin) { r.sliderMin = v; r.sliderMinField.set(formatNumber(v)); }
            if (!editingBounds && v > r.sliderMax) { r.sliderMax = v; r.sliderMaxField.set(formatNumber(v)); }
        }
        // Parametric/polar rows show their domain fields with Desmos's defaults.
        if (r.analysis.graph == GraphKind::Parametric || r.analysis.graph == GraphKind::Polar) {
            bool polar = r.analysis.graph == GraphKind::Polar;
            if (r.domainMin.empty() && !(m_list.focus.row == int(i) && m_list.focus.field == Field::DomainMin))
                r.domainMin.set("0");
            if (r.domainMax.empty() && !(m_list.focus.row == int(i) && m_list.focus.field == Field::DomainMax))
                r.domainMax.set(polar ? "12\\pi" : "1");
        }
    }
    for (size_t k = 0; k < syn.size(); ++k) {
        TableColumn& col = rows[syn[k].row].columns[syn[k].col];
        RowAnalysis& a = res[rows.size() + k];
        if (!syn[k].points) continue;
        if (a.graph == GraphKind::Point) {
            if (col.computed) for (auto& p : a.pieces) col.computedValues.push_back(p.py);
            col.analysis = std::move(a);
        }
    }
    m_needsAnalysis = false;
}

void App::rebuildItems() {
    m_items.clear();
    m_graph.images.clear();
    auto& rows = m_list.rows;
    for (int i = 0; i < int(rows.size()); ++i) {
        ExpressionRow& r = rows[i];
        bool folderHidden = false;
        if (r.folderId) {
            int fi = m_list.indexOfId(r.folderId);
            folderHidden = fi >= 0 && rows[fi].hidden;
        }
        r.syncStyle();
        r.style.label = r.label.utf8();
        r.effectiveHidden = r.hidden || folderHidden;
        if (rowGraphs(r)) {
            Style st = r.style;
            st.hidden = r.effectiveHidden;
            st.color = theme::onPaper(st.color);   // display colour (black draws light in dark mode)
            m_items.push_back({&r.analysis, st, uint64_t(r.id) << 20, i});
        } else if (r.type == RowType::Table) {
            for (auto& col : r.columns) {
                if (col.analysis.graph != GraphKind::Point) continue;
                Style st = col.style;
                st.hidden = st.hidden || folderHidden;
                st.color = theme::onPaper(st.color);
                m_items.push_back({&col.analysis, st, uint64_t(col.id) << 20, i, true});
            }
        } else if (r.type == RowType::Image && !r.effectiveHidden && r.imageTexture >= 0) {
            const auto& fv = r.analysis.fieldValues;
            if (fv.size() == 3 && fv[0].size() == 2 && fv[1].size() == 1 && fv[2].size() == 1)
                m_graph.images.push_back({r.imageTexture, fv[0][0], fv[0][1], fv[1][0], fv[2][0], r.imageOpacity, i});
        }
    }
}

Style* App::styleTarget() {
    if (!m_styleMenu.open || m_styleMenu.row < 0 || m_styleMenu.row >= int(m_list.rows.size())) return nullptr;
    ExpressionRow& r = m_list.rows[m_styleMenu.row];
    if (m_styleMenu.col >= 0) {
        if (m_styleMenu.col >= int(r.columns.size())) return nullptr;
        return &r.columns[m_styleMenu.col].style;
    }
    r.syncStyle();
    return &r.style;
}

void App::openStyleMenu(int row, int col) {
    if (row < 0 || row >= int(m_list.rows.size())) return;
    if (m_needsAnalysis) analyze();
    layoutRects();
    m_list.layout(m_renderer->atlas());
    float top = m_list.rowTopOnScreen(row);
    m_list.onStyleMenu(row, col, Rect{m_list.area.x + 4, top + IconCenterY - 15, 30, 30});
}

// ---- commands ----------------------------------------------------------------------------------

std::string App::copyText() {
    if (TextField* tf = m_list.focusedText()) {
        if (!tf->hasSelection()) return tf->utf8();
        int a = std::min(tf->anchor, tf->cursor), b = std::max(tf->anchor, tf->cursor);
        return toUtf8(tf->text.substr(a, b - a));
    }
    if (MathTree* t = m_list.focusedTree()) return t->selectionLatex();
    return {};
}

void App::pasteText(const std::string& text) {
    if (text.empty()) return;
    willChange();
    m_lastSnapshotTime = 0;
    if (TextField* tf = m_list.focusedText()) {
        tf->insert(fromUtf8(text));
        contentChanged();
        return;
    }
    // Multiple lines paste as multiple expressions (like Desmos).
    std::vector<std::string> lines;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    if (lines.empty()) return;
    if (!m_list.focusedTree()) {
        if (!m_list.rows.empty() && m_list.rows.back().type == RowType::Expression && m_list.rows.back().tree.empty())
            m_list.focusRow(int(m_list.rows.size()) - 1);
        else
            m_list.addRow(int(m_list.rows.size()));
    }
    MathTree* t = m_list.focusedTree();
    t->insertLatex(lines[0]);
    m_list.rows[m_list.focus.row].layoutDirty = true;
    if (MathField* mf = m_list.fieldFor(m_list.focus)) mf->dirty = true;
    if (m_list.focus.field == Field::Main)
        for (size_t k = 1; k < lines.size(); ++k) m_list.addRow(m_list.focus.row + 1, lines[k], true);
    contentChanged();
}

void App::cut() {
    if (TextField* tf = m_list.focusedText()) {
        if (!tf->hasSelection()) return;
        willChange();
        tf->insert(U"");
        contentChanged();
        return;
    }
    MathTree* t = m_list.focusedTree();
    if (!t || !t->hasSelection()) return;
    willChange();
    t->deleteSelection();
    m_list.rows[m_list.focus.row].layoutDirty = true;
    if (MathField* mf = m_list.fieldFor(m_list.focus)) mf->dirty = true;
    contentChanged();
}

void App::selectAll() {
    m_list.selectAll();
    m_dirty = true;
}

void App::addExpression(const std::string& latex) {
    // Fill the trailing empty row first (Desmos starts with one empty expression).
    auto& rows = m_list.rows;
    if (!rows.empty() && rows.back().type == RowType::Expression && rows.back().tree.empty()) {
        ExpressionRow& r = rows.back();
        r.tree.insertLatex(latex);
        r.layoutDirty = true;
    } else {
        int i = m_list.addRow(int(rows.size()), latex, false);
        rows[i].folderId = 0;
    }
    m_list.blur();
    contentChanged();
}

void App::addItem(int item) {
    m_addMenu.open = false;
    int at = m_list.focus.row >= 0 ? m_list.focus.row + 1 : int(m_list.rows.size());
    if (at > 0 && at <= int(m_list.rows.size()) && m_list.rows[at - 1].type == RowType::Folder &&
        m_list.rows[at - 1].collapsed)
        at = m_list.folderEnd(at - 1);
    switch (item) {
    case AddMenu::Expression: willChange(); m_list.addRow(at); break;
    case AddMenu::Note: willChange(); m_list.addRow(at, "", true, RowType::Note); break;
    case AddMenu::Table: willChange(); m_list.addTable(at); break;
    case AddMenu::Folder: {
        willChange();
        // Folders are always top level: insert after the current folder's block.
        if (at > 0 && at <= int(m_list.rows.size())) {
            const ExpressionRow& prev = m_list.rows[at - 1];
            uint32_t fid = prev.type == RowType::Folder ? prev.id : prev.folderId;
            if (fid) at = m_list.folderEnd(m_list.indexOfId(fid));
        }
        int i = m_list.addRow(at, "", true, RowType::Folder);
        m_list.rows[i].folderId = 0;
        break;
    }
    case AddMenu::Image:
        if (onRequestImage) onRequestImage();
        break;
    default: break;
    }
    m_dirty = true;
}

bool App::addImage(const std::string& path) {
    float aspect = 1;
    int tex = m_renderer->loadImage(path, aspect);
    if (tex < 0) return false;
    willChange();
    int at = m_list.focus.row >= 0 ? m_list.focus.row + 1 : int(m_list.rows.size());
    int i = m_list.addRow(at, "", false, RowType::Image);
    ExpressionRow& r = m_list.rows[i];
    r.imagePath = path;
    r.imageTexture = tex;
    r.imageAspect = aspect;
    r.imageHeight.set(formatNumber(std::round(10 * aspect * 100) / 100));
    m_list.blur();
    contentChanged();
    return true;
}

void App::focusRow(int i) {
    m_list.focusRow(i);
    m_dirty = true;
}

void App::zoomIn() { m_graph.zoomBy(ZoomButtonFactor); m_dirty = true; }
void App::zoomOut() { m_graph.zoomBy(1.0 / ZoomButtonFactor); m_dirty = true; }
void App::resetZoom() { m_graph.resetView(); m_dirty = true; }

void App::setView(double xmin, double xmax, double ymin, double ymax) {
    layoutRects();
    Viewport& vp = m_graph.vp;
    vp.cx = 0.5 * (xmin + xmax);
    vp.cy = 0.5 * (ymin + ymax);
    vp.scale = std::min(vp.width / (xmax - xmin), vp.height / (ymax - ymin));
    m_dirty = true;
}

// ---- events ------------------------------------------------------------------------------------

void App::keypadKey(const KeypadKey& k) {
    if (k.action == KeypadKey::Page) { m_keypad.page = k.page; return; }
    if (k.action == KeypadKey::Close) { m_keypad.open = false; return; }
    if (!m_list.editing()) {
        auto& rows = m_list.rows;
        if (!rows.empty() && rows.back().type == RowType::Expression && rows.back().tree.empty())
            m_list.focusRow(int(rows.size()) - 1);
        else
            m_list.addRow(int(rows.size()));
    }
    switch (k.action) {
    case KeypadKey::Type: {
        std::string t = k.text;
        m_list.key(Key::Char, {}, fromUtf8(t));
        if (t == "d/dx") m_list.key(Key::Right, {}, {});  // leave the d/dx fraction
        break;
    }
    case KeypadKey::Left: m_list.key(Key::Left, {}, {}); break;
    case KeypadKey::Right: m_list.key(Key::Right, {}, {}); break;
    case KeypadKey::Backspace: m_list.key(Key::Backspace, {}, {}); break;
    case KeypadKey::Enter: m_list.key(Key::Enter, {}, {}); break;
    default: break;
    }
}

void App::mouseDown(float x, float y, int clicks, Mods m) {
    resetCursorBlink();
    m_dirty = true;
    Rect kpad{0, m_h - m_keypad.height(), m_w, m_keypad.height()};
    if (const KeypadKey* k = m_keypad.keyAt(kpad, x, y)) {
        m_keypad.setPressed(k);
        m_capture = Capture::Keypad;
        keypadKey(*k);
        return;
    }
    if (m_styleMenu.open) {
        if (Style* st = styleTarget()) {
            if (m_styleMenu.mouseDown(x, y, *st, m_w, m_h)) {
                ExpressionRow& r = m_list.rows[m_styleMenu.row];
                if (m_styleMenu.col < 0) { r.color = r.style.color; r.hidden = r.style.hidden; }
                m_modified = true;
                m_capture = Capture::StyleMenu;
                return;
            }
        }
        m_styleMenu.close();
        return;  // a click outside a popover only closes it
    }
    if (m_addMenu.open) {
        int item = m_addMenu.itemAt(x, y);
        if (item >= 0) addItem(item);
        else m_addMenu.open = false;
        return;
    }
    if (m_graph.settingsOpen()) {
        m_capture = Capture::Graph;
        if (m_graph.mouseDown(x, y, clicks, m, items())) return;
    }
    if (TopButton b = topButtonAt(x, y); b != TopButton::None) {
        m_pressTop = b;
        m_capture = Capture::TopBar;
        return;
    }
    if (onDivider(x, y)) { m_capture = Capture::PanelResize; return; }
    if (!m_panelCollapsed && m_list.area.contains(x, y)) {
        m_capture = Capture::List;
        m_list.mouseDown(x, y, clicks, m);
        return;
    }
    if (graphRect().contains(x, y)) {
        m_capture = Capture::Graph;
        m_graph.mouseDown(x, y, clicks, m, items());
        return;
    }
    m_capture = Capture::None;
}

void App::mouseDragged(float x, float y) {
    switch (m_capture) {
    case Capture::List: if (m_list.mouseDragged(x, y)) m_dirty = true; break;
    case Capture::Graph: if (m_graph.mouseDragged(x, y, items())) m_dirty = true; break;
    case Capture::StyleMenu:
        if (Style* st = styleTarget()) {
            if (m_styleMenu.mouseDragged(x, y, *st, m_w, m_h)) {
                ExpressionRow& r = m_list.rows[m_styleMenu.row];
                if (m_styleMenu.col < 0) { r.color = r.style.color; r.hidden = r.style.hidden; }
                m_dirty = true;
            }
        }
        break;
    case Capture::PanelResize:
        m_panelW = x;
        layoutRects();
        m_dirty = true;
        break;
    case Capture::TopBar: {
        TopButton h = topButtonAt(x, y);
        if (h != m_hoverTop) { m_hoverTop = h; m_dirty = true; }
        break;
    }
    default: break;
    }
}

void App::mouseUp(float x, float y) {
    switch (m_capture) {
    case Capture::List: m_list.mouseUp(x, y); break;
    case Capture::Graph: m_graph.mouseUp(x, y); break;
    case Capture::StyleMenu: m_styleMenu.mouseUp(); break;
    case Capture::Keypad: m_keypad.setPressed(nullptr); break;
    case Capture::TopBar:
        if (topButtonAt(x, y) == m_pressTop) {
            switch (m_pressTop) {
            case TopButton::Add:
                m_addMenu.anchor = topButtonRect(TopButton::Add);
                m_addMenu.open = !m_addMenu.open;
                break;
            case TopButton::Undo: undo(); break;
            case TopButton::Redo: redo(); break;
            case TopButton::Collapse: m_panelCollapsed = true; layoutRects(); break;
            case TopButton::Expand: m_panelCollapsed = false; layoutRects(); break;
            case TopButton::Keypad: toggleKeypad(); layoutRects(); break;
            default: break;
            }
        }
        m_pressTop = TopButton::None;
        break;
    default: break;
    }
    m_capture = Capture::None;
    m_dirty = true;
}

void App::mouseMoved(float x, float y) {
    bool changed = false;
    TopButton h = topButtonAt(x, y);
    if (h != m_hoverTop) { m_hoverTop = h; changed = true; }
    bool div = onDivider(x, y);
    if (div != m_hoverDivider) { m_hoverDivider = div; changed = true; }
    changed |= m_addMenu.mouseMoved(x, y);
    changed |= m_keypad.mouseMoved(Rect{0, m_h - m_keypad.height(), m_w, m_keypad.height()}, x, y);
    changed |= m_list.mouseMoved(x, y);
    changed |= m_graph.mouseMoved(x, y, items());
    if (changed) m_dirty = true;
}

void App::mouseExited() {
    if (m_graph.mouseExited()) m_dirty = true;
    if (m_list.mouseMoved(-1, -1)) m_dirty = true;
}

void App::scroll(float x, float y, float dx, float dy, bool precise) {
    if (!m_panelCollapsed && m_list.area.contains(x, y)) {
        m_list.scrollBy(-dy * (precise ? 1.0f : 20.0f));
        m_dirty = true;
        return;
    }
    if (m_graph.scroll(x, y, dx, dy, precise)) m_dirty = true;
}

void App::magnify(float x, float y, float amount) {
    if (m_graph.magnify(x, y, amount)) m_dirty = true;
}

void App::key(Key k, Mods m, const std::u32string& text) {
    resetCursorBlink();
    if (k == Key::Escape && (m_styleMenu.open || m_addMenu.open || m_graph.settingsOpen())) {
        m_styleMenu.close();
        m_addMenu.open = false;
        m_graph.closeSettings();
        m_dirty = true;
        return;
    }
    if (m_list.key(k, m, text)) m_dirty = true;
}

CursorKind App::cursorAt(float x, float y) const {
    if (m_keypad.open && y >= m_h - m_keypad.height()) return CursorKind::Pointer;
    if (m_capture == Capture::PanelResize || onDivider(x, y)) return CursorKind::Grab;  // shown as resize
    if (topButtonAt(x, y) != TopButton::None) return CursorKind::Pointer;
    if (!m_panelCollapsed && m_list.area.contains(x, y)) {
        ListHit h = m_list.hitTest(x, y);
        if (h.kind == ListHit::Field) return CursorKind::IBeam;
        if (h.kind == ListHit::None) return CursorKind::Arrow;
        return CursorKind::Pointer;
    }
    if (graphRect().contains(x, y)) return m_graph.cursorAt(x, y);
    return CursorKind::Arrow;
}

void App::blinkTick() {
    if (!m_list.editing()) return;
    m_cursorVisible = !m_cursorVisible;
    m_dirty = true;
}

// ---- drawing -----------------------------------------------------------------------------------

void App::drawChrome(Batch2D& b) {
    using widgets::Interaction;
    auto state = [&](TopButton t) {
        if (t == TopButton::Undo && !canUndo()) return Interaction::Disabled;
        if (t == TopButton::Redo && !canRedo()) return Interaction::Disabled;
        if (m_pressTop == t && m_hoverTop == t) return Interaction::Pressed;
        return m_hoverTop == t ? Interaction::Hovered : Interaction::Normal;
    };
    if (!m_panelCollapsed) {
        Rect tb = topBarRect();
        b.gradient(tb, TopBarTop, TopBarBottom);
        b.line(tb.x, tb.bottom() + 0.5f, tb.right(), tb.bottom() + 0.5f, 1, Border);
        widgets::iconButton(b, topButtonRect(TopButton::Add), widgets::Icon::Plus,
                            m_addMenu.open ? Interaction::Pressed : state(TopButton::Add));
        widgets::iconButton(b, topButtonRect(TopButton::Undo), widgets::Icon::Undo, state(TopButton::Undo));
        widgets::iconButton(b, topButtonRect(TopButton::Redo), widgets::Icon::Redo, state(TopButton::Redo));
        widgets::iconButton(b, topButtonRect(TopButton::Collapse), widgets::Icon::CollapseLeft, state(TopButton::Collapse));
        // panel right border (darker while hovering the resize handle)
        Color div = (m_hoverDivider || m_capture == Capture::PanelResize) ? Accent : Border;
        b.line(m_panelW - 0.5f, HeaderHeight, m_panelW - 0.5f, m_h - m_keypad.height(), (m_hoverDivider ? 2.f : 1.f), div);
    } else {
        widgets::floatingButton(b, topButtonRect(TopButton::Expand), widgets::Icon::ExpandRight, state(TopButton::Expand));
    }
    m_graph.drawControls(b);

    // Keypad toggle (keyboard icon) and the keypad itself
    Rect kb = topButtonRect(TopButton::Keypad);
    widgets::floatingButton(b, kb, widgets::Icon::Keyboard, m_keypad.open ? Interaction::Pressed : state(TopButton::Keypad));
    m_keypad.draw(b, Rect{0, m_h - m_keypad.height(), m_w, m_keypad.height()});

    // App header (dark bar; traffic lights sit at its left)
    Rect h = headerRect();
    b.rect(h, HeaderBg);
    b.line(0, h.bottom() + 0.5f, m_w, h.bottom() + 0.5f, 1, HeaderBorder);
    std::string t = title() + (m_modified ? " •" : "");
    b.text(Font::Ui, HeaderTitleSize * 0.85f, HeaderTrafficLightInset, h.h * 0.5f + 5.5f, t, HeaderText);
    const std::string brand = "graphingcalc";
    float bw = b.atlas().measure(Font::HeaderLight, brand, HeaderTitleSize + 4);
    b.text(Font::HeaderLight, HeaderTitleSize + 4, (m_w - bw) * 0.5f, h.h * 0.5f + 7, brand, White);

    // Popovers last (on top of everything)
    m_addMenu.draw(b);
    if (Style* st = styleTarget()) m_styleMenu.draw(b, *st, m_w, m_h);
}

void App::drawPerfHud(Batch2D& b) {
    const RenderStats& s = m_renderer->stats();
    char buf[160];
    std::snprintf(buf, sizeof buf, "cpu %.2f ms  sample %.2f ms  gpu %.2f ms  verts %zu  ui %zu",
                  m_lastFrameMs, m_graph.lastSampleMs(), s.gpuMs, s.curveVertices, s.uiInstances);
    Rect g = graphRect();
    widgets::tooltip(b, g.x + 10, g.bottom() - 34, buf);
}

void App::render(MTL::Texture* target, CA::MetalDrawable* drawable, bool wait) {
    auto t0 = std::chrono::steady_clock::now();
    double now = nowSeconds();
    double dt = m_lastTick > 0 ? std::min(now - m_lastTick, 0.1) : 0;
    m_lastTick = now;
    m_list.tick(now, dt);
    if (m_needsAnalysis) analyze();
    layoutRects();
    m_list.layout(m_renderer->atlas());
    rebuildItems();
    Batch2D& ui = *m_ui;
    ui.clear();
    int selected = m_list.focus.row;
    m_graph.buildFrame(m_items, selected, *m_frame);
    m_graph.drawUnder(ui);
    size_t under = ui.size();
    m_graph.drawOver(ui, m_items, selected);
    size_t over = ui.size();
    std::vector<ImageDraw> thumbs;
    if (!m_panelCollapsed) {
        m_list.draw(ui, m_cursorVisible);
        for (int i = 0; i < int(m_list.rows.size()); ++i) {
            const ExpressionRow& r = m_list.rows[i];
            if (r.type != RowType::Image || r.imageTexture < 0 || !r.visibleInList) continue;
            float x0 = m_list.area.x + TabWidth + MathLeftPad + r.indent, y0 = m_list.rowTopOnScreen(i) + 10;
            float s = ImageThumbSize, w = s, h = s;
            if (r.imageAspect > 1) w = s / r.imageAspect; else h = s * r.imageAspect;
            thumbs.push_back({r.imageTexture, Rect{x0 + (s - w) * 0.5f, y0 + (s - h) * 0.5f, w, h}, r.hidden ? 0.4f : 1.0f});
        }
    }
    size_t listEnd = ui.size();
    drawChrome(ui);
    if (perfHud) drawPerfHud(ui);

    FrameDesc f;
    f.ui = &ui;
    f.underEnd = under;
    f.overEnd = over;
    f.listEnd = listEnd;
    f.listImages = std::move(thumbs);
    f.listClip = m_list.area;
    f.graph = m_frame.get();
    f.viewW = m_w;
    f.viewH = m_h;
    f.scale = m_scale;
    m_renderer->render(f, target, drawable, wait);
    m_dirty = animating();
    m_lastFrameMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (m_lastFrameMs > 16.6) log::info("slow CPU frame: %.2f ms (sampling %.2f ms)", m_lastFrameMs, m_graph.lastSampleMs());
}

bool App::screenshot(const std::string& path) {
    int w = int(std::lround(m_w * m_scale)), h = int(std::lround(m_h * m_scale));
    MTL::Texture* tex = m_renderer->makeOffscreenTarget(w, h);
    // Two passes: the first warms glyph caches/samples, the second is what we save (both are cheap).
    render(tex, nullptr, true);
    m_dirty = true;
    render(tex, nullptr, true);
    bool ok = Renderer::savePng(tex, path);
    tex->release();
    return ok;
}

} // namespace calc
