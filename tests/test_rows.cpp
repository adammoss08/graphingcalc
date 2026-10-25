#include "Test.hpp"

#include "app/App.hpp"
#include "math/Latex.hpp"
#include "render/Renderer.hpp"

#include <Metal/Metal.hpp>

#include <cstdlib>

using namespace calc;

namespace {

App& fresh() {
    static MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    static App* app = nullptr;
    delete app;
    app = new App();
    std::string err;
    if (!app->init(dev, METALLIB_PATH, err)) std::printf("init failed: %s\n", err.c_str());
    app->setSize(1200, 800, 2);
    return *app;
}

void type(App& a, const char* s) {
    for (const char* c = s; *c; ++c) a.key(Key::Char, {}, std::u32string(1, char32_t(*c)));
}

void frame(App& a) {
    MTL::Texture* t = a.renderer().makeOffscreenTarget(64, 64);
    a.render(t, nullptr, true);
    t->release();
}

ExpressionList& L(App& a) { return a.list(); }

} // namespace

TEST(rows_typing_quote_makes_a_note) {
    App& a = fresh();
    type(a, "\"");
    CHECK(L(a).rows[0].type == RowType::Note);
    type(a, "hello world");
    CHECK_EQ(L(a).rows[0].text.utf8(), std::string("hello world"));
    a.key(Key::Enter, {});
    CHECK(L(a).rows.size() == 2 && L(a).rows[1].type == RowType::Expression);
}

TEST(rows_folders_contain_hide_and_collapse) {
    App& a = fresh();
    a.addItem(AddMenu::Folder);
    int f = L(a).focusedRow();
    CHECK(L(a).rows[f].type == RowType::Folder);
    type(a, "Group");
    a.key(Key::Enter, {});                       // new row inside the folder
    type(a, "y=x");
    frame(a);
    int child = L(a).focusedRow();
    CHECK_EQ(L(a).rows[child].folderId, L(a).rows[f].id);
    CHECK_EQ(a.items().size(), size_t(1));
    L(a).rows[f].hidden = true;                  // hiding the folder hides its children's graphs
    frame(a);
    CHECK(a.items()[0].style.hidden);
    L(a).rows[f].collapsed = true;
    frame(a);
    CHECK(!L(a).rows[child].visibleInList);
}

TEST(rows_tables_plot_and_compute) {
    App& a = fresh();
    a.addItem(AddMenu::Table);
    int t = L(a).focusedRow();
    CHECK(L(a).rows[t].type == RowType::Table);
    // Fill x_1 = 1, 2, 3 and y_1 = 2, 4, 6 by typing and navigating like a user.
    const char* xs[] = {"1", "2", "3"};
    const char* ys[] = {"2", "4", "6"};
    for (int k = 0; k < 3; ++k) {
        L(a).focusField({t, Field::TableCell, 0, k});
        type(a, xs[k]);
        a.key(Key::Tab, {});
        type(a, ys[k]);
    }
    frame(a);
    ExpressionRow& r = L(a).rows[t];
    CHECK_EQ(r.columns[0].cells.size(), size_t(3));
    CHECK(r.columns[1].analysis.graph == GraphKind::Point);
    CHECK_EQ(r.columns[1].analysis.pieces.size(), size_t(3));
    CHECK_NEAR(r.columns[1].analysis.pieces[2].py, 6, 0);
    // A third column with an expression header is computed from the first column.
    L(a).focusField({t, Field::TableHeader, 2, 0});
    type(a, "x_1");
    a.key(Key::Right, {});   // leave the subscript
    type(a, "^2");
    L(a).blur();
    frame(a);
    CHECK(L(a).rows[t].columns[2].computed);
    CHECK_EQ(L(a).rows[t].columns[2].computedValues.size(), size_t(3));
    if (L(a).rows[t].columns[2].computedValues.size() == 3)
        CHECK_NEAR(L(a).rows[t].columns[2].computedValues[2], 9, 0);
    // The table's lists are usable elsewhere.
    a.addExpression("\\operatorname{total}\\left(y_{1}\\right)");
    frame(a);
    CHECK_NEAR(L(a).rows.back().analysis.value, 12, 0);
}

TEST(rows_slider_bounds_and_play) {
    App& a = fresh();
    type(a, "a=2");
    frame(a);
    ExpressionRow* r = &L(a).rows[0];
    CHECK(r->analysis.kind == RowKind::Slider);
    L(a).focusField({0, Field::SliderMax});
    for (int k = 0; k < 2; ++k) a.key(Key::Backspace, {});
    type(a, "4");
    frame(a);
    r = &L(a).rows[0];
    CHECK_NEAR(r->sliderMax, 4, 0);
    L(a).focusField({0, Field::SliderStep});
    type(a, "0.5");
    frame(a);
    CHECK_NEAR(L(a).rows[0].sliderStep, 0.5, 0);
    // Play: the value moves on each tick and stays on the step grid.
    L(a).rows[0].playing = true;
    CHECK(a.animating());
    double before = L(a).rows[0].analysis.sliderValue;
    L(a).tick(1.0, 0.0);
    L(a).tick(1.6, 0.6);
    frame(a);
    double after = L(a).rows[0].analysis.sliderValue;
    CHECK(after != before);
    CHECK_NEAR(std::fmod(after * 2, 1.0), 0, 1e-9);
}

TEST(rows_parametric_domain_fields) {
    App& a = fresh();
    a.addExpression("\\left(\\cos t,\\sin t\\right)");
    frame(a);
    ExpressionRow& r = L(a).rows[0];
    CHECK_EQ(r.domainMax.latex(), std::string("1"));   // Desmos default shown
    L(a).focusField({0, Field::DomainMax});
    a.key(Key::Backspace, {});
    type(a, "2pi");
    frame(a);
    CHECK_NEAR(L(a).rows[0].analysis.tMax, 2 * M_PI, 1e-12);
}

TEST(rows_style_menu_edits_style) {
    App& a = fresh();
    a.addExpression("y=x");
    frame(a);
    StyleMenu m;
    m.show(0, -1, calc::Rect{10, 100, 30, 30}, true, false, false, false);
    calc::Rect box = m.rect(1200, 800);
    calc::Style s = L(a).rows[0].style;
    // Second swatch (blue) and the dashed line-style button.
    CHECK(m.mouseDown(box.x + 14 + 40 + 15, box.y + 14 + 15, s, 1200, 800));
    CHECK(s.color.b == theme::CurveBlue.b);
    CHECK(m.mouseDown(box.x + 14 + 56 + 24, box.y + 14 + 30 + 14 + 4 + 13, s, 1200, 800));
    CHECK(s.line == LineStyle::Dashed);
}

TEST(rows_keypad_types_into_focused_row) {
    App& a = fresh();
    a.toggleKeypad();
    frame(a);
    // First key on the main page is "x" (row 1, column 1).
    float y = 800 - Keypad::kHeight + 6 + 10;
    a.mouseDown(20, y, 1, {});
    a.mouseUp(20, y);
    CHECK_EQ(blockToLatex(L(a).rows[0].tree.root()), std::string("x"));
}

TEST(rows_fraction_display) {
    std::string f;
    CHECK(fractionLatex(1.0 / 3, f));
    CHECK_EQ(f, std::string("\\frac{1}{3}"));
    CHECK(fractionLatex(-0.75, f));
    CHECK_EQ(f, std::string("-\\frac{3}{4}"));
    CHECK(!fractionLatex(M_PI, f));
    CHECK(!fractionLatex(2, f));
}

TEST(rows_showcase_save_load_roundtrip) {
    App& a = fresh();
    std::string err;
    std::string fixture = std::string(METALLIB_PATH);
    fixture = fixture.substr(0, fixture.rfind("/build/")) + "/tests/fixtures/showcase.json";
    CHECK(a.loadFile(fixture, err));
    frame(a);
    auto& rows = L(a).rows;
    CHECK_EQ(rows.size(), size_t(8));
    CHECK(rows[0].type == RowType::Note && rows[1].type == RowType::Folder);
    CHECK_EQ(rows[2].folderId, rows[1].id);
    CHECK(rows[5].style.line == LineStyle::Dashed);
    CHECK(rows[6].type == RowType::Table && rows[6].columns.size() == 3);
    CHECK(rows[7].type == RowType::Image && rows[7].imageTexture >= 0);
    CHECK_NEAR(rows[5].analysis.tMax, M_PI, 1e-12);
    const char* tmp = std::getenv("TMPDIR");
    std::string out = std::string(tmp ? tmp : "/tmp") + "/graphingcalc_showcase.json";
    CHECK(a.saveFile(out, err));
    App& b = fresh();
    CHECK(b.loadFile(out, err));
    frame(b);
    auto& r2 = L(b).rows;
    CHECK_EQ(r2.size(), size_t(8));
    CHECK_EQ(r2[3].sliderMinField.latex(), std::string("-5"));
    CHECK_EQ(r2[6].columns[2].header.latex(), std::string("x_{1}^{2}\\cdot 0.2"));
    CHECK(r2[6].columns[1].style.showLines);
    CHECK_EQ(r2[2].folderId, r2[1].id);
}

#include <CoreFoundation/CoreFoundation.h>

// Heavy curves refine in the background: a zoom frame stays cheap and the sharper samples land later.
TEST(rows_heavy_curve_refines_in_background) {
    App& a = fresh();
    std::string as = "a=\\left[", bs = "b=\\left[";
    for (int k = 1; k <= 2000; ++k) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%s%.4f", k > 1 ? "," : "", std::sin(k * 1.7) / k);
        as += buf;
        std::snprintf(buf, sizeof buf, "%s%.4f", k > 1 ? "," : "", std::cos(k * 0.9) / k);
        bs += buf;
    }
    a.pasteText(as + "\\right]\n" + bs + "\\right]\n"
                "\\left(\\sum_{k=1}^{2000}a\\left[k\\right]\\cos\\left(kt\\right),\\sum_{k=1}^{2000}b\\left[k\\right]\\sin\\left(kt\\right)\\right)");
    L(a).blur();
    a.graph().asyncSampling = true;
    frame(a);                                   // first sample is synchronous (nothing to show yet)
    a.scroll(900, 400, 0, 30, true);            // zoom in
    frame(a);
    CHECK(a.lastFrameCpuMs() < 16.6);           // the stale curve is drawn rescaled, not resampled
    CHECK(a.graph().samplesInFlight() == 1);
    for (int i = 0; i < 400 && a.graph().samplesInFlight() > 0; ++i)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);  // deliver the result (main queue)
    CHECK(a.graph().samplesInFlight() == 0);
    CHECK(a.needsRedraw());
}

TEST(rows_drag_to_reorder_and_into_folder) {
    App& a = fresh();
    a.pasteText("y=1\ny=2\ny=3");
    frame(a);
    auto latex = [&](int i) { return blockToLatex(L(a).rows[i].tree.root()); };
    // Drag row 3 ("y=3") by its tab up above row 1.
    float tabX = L(a).area.x + 6;
    float y3 = L(a).rowTopOnScreen(2) + 20, y1 = L(a).rowTopOnScreen(0) + 5;
    a.mouseDown(tabX, y3, 1, {});
    a.mouseDragged(tabX, y3 - 20);
    a.mouseDragged(tabX, y1);
    a.mouseUp(tabX, y1);
    frame(a);
    CHECK_EQ(latex(0), std::string("y=3"));
    CHECK_EQ(latex(1), std::string("y=1"));
    a.undo();
    frame(a);
    CHECK_EQ(latex(0), std::string("y=1"));
    // Drop a row just under a folder: it joins the folder.
    L(a).blur();
    a.addItem(AddMenu::Folder);
    frame(a);
    int f = L(a).focusedRow();
    int last = int(L(a).rows.size()) - 1;      // the folder was added after the focused row
    if (f == last) {                            // move "y=1" under the folder
        float yf = L(a).rowTopOnScreen(f) + L(a).rows[f].h - 3;
        float yr = L(a).rowTopOnScreen(0) + 20;
        a.mouseDown(tabX, yr, 1, {});
        a.mouseDragged(tabX, yr + 10);
        a.mouseDragged(tabX, yf + L(a).rows[f].h * 0.6f);
        a.mouseUp(tabX, yf + L(a).rows[f].h * 0.6f);
        frame(a);
        CHECK_EQ(L(a).rows.back().folderId, L(a).rows[L(a).rows.size() - 2].id);
    }
}

TEST(rows_dragging_a_point_moves_its_sliders) {
    App& a = fresh();
    a.pasteText("a=1\nb=2\n\\left(a,b\\right)");
    L(a).blur();
    frame(a);
    const auto& pc = L(a).rows[2].analysis.pieces;
    CHECK(pc.size() == 1 && pc[0].dragX == "a" && pc[0].dragY == "b");
    // Press on the point, drag it to world (3, -1).
    const Viewport& vp = a.graph().vp;
    calc::Rect g = a.graph().area;
    float px = g.x + float(vp.sx(1)), py = g.y + float(vp.sy(2));
    float tx = g.x + float(vp.sx(3)), ty = g.y + float(vp.sy(-1));
    a.mouseDown(px, py, 1, {});
    a.mouseDragged(tx, ty);
    a.mouseUp(tx, ty);
    frame(a);
    CHECK_NEAR(L(a).rows[0].analysis.sliderValue, 3, 0.05);
    CHECK_NEAR(L(a).rows[1].analysis.sliderValue, -1, 0.05);
    a.undo();                                  // one undo step for the whole drag
    frame(a);
    CHECK_NEAR(L(a).rows[0].analysis.sliderValue, 1, 0);
    // Literal points aren't draggable.
    a.addExpression("\\left(4,5\\right)");
    frame(a);
    CHECK(L(a).rows.back().analysis.pieces[0].dragX.empty());
}

TEST(rows_point_labels_save_and_interpolate) {
    App& a = fresh();
    a.pasteText("a=2.5\n\\left(a,1\\right)");
    frame(a);
    ExpressionRow& r = L(a).rows[1];
    r.style.showLabel = true;
    r.hidden = false;
    L(a).focusField({1, Field::Label});
    for (char c : std::string("A at ${a}")) a.key(Key::Char, {}, std::u32string(1, char32_t(c)));
    frame(a);
    double v;
    CHECK(a.graph().valueOf && a.graph().valueOf("a", v) && v == 2.5);
    CHECK_EQ(L(a).rows[1].style.label, std::string("A at ${a}"));
    const char* tmp = std::getenv("TMPDIR");
    std::string out = std::string(tmp ? tmp : "/tmp") + "/graphingcalc_labels.json", err;
    CHECK(a.saveFile(out, err));
    App& b = fresh();
    CHECK(b.loadFile(out, err));
    frame(b);
    CHECK(L(b).rows[1].style.showLabel);
    CHECK_EQ(L(b).rows[1].label.utf8(), std::string("A at ${a}"));
}
