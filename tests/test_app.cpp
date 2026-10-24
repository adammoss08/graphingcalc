#include "Test.hpp"

#include "app/App.hpp"
#include "math/Latex.hpp"

#include <Metal/Metal.hpp>

using namespace calc;

namespace {

App& freshApp() {
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

std::string latexOfRow(App& a, int i) { return blockToLatex(a.list().rows[i].tree.root()); }

void frame(App& a) {
    MTL::Texture* t = a.renderer().makeOffscreenTarget(64, 64);
    a.render(t, nullptr, true);
    t->release();
}

} // namespace

// Starts like Desmos: one empty, focused expression.
TEST(app_starts_with_focused_empty_row) {
    App& a = freshApp();
    CHECK_EQ(a.list().rows.size(), size_t(1));
    CHECK_EQ(a.list().focusedRow(), 0);
}

TEST(app_typing_enter_backspace_flow) {
    App& a = freshApp();
    type(a, "y=x^2");
    frame(a);
    CHECK_EQ(latexOfRow(a, 0), std::string("y=x^{2}"));
    CHECK(a.list().rows[0].analysis.graph == GraphKind::ExplicitY);
    a.key(Key::Enter, {});                 // new row below, focused
    CHECK_EQ(a.list().rows.size(), size_t(2));
    CHECK_EQ(a.list().focusedRow(), 1);
    a.key(Key::Backspace, {});             // backspace on an empty row removes it
    CHECK_EQ(a.list().rows.size(), size_t(1));
    CHECK_EQ(a.list().focusedRow(), 0);
    a.key(Key::Up, {});                    // nothing above: stays put
    CHECK_EQ(a.list().focusedRow(), 0);
}

TEST(app_undo_redo) {
    App& a = freshApp();
    type(a, "y=2x");
    a.key(Key::Enter, {});
    type(a, "a=3");
    frame(a);
    CHECK_EQ(a.list().rows.size(), size_t(2));
    a.undo();
    a.undo();
    frame(a);
    CHECK_EQ(a.list().rows.size(), size_t(1));
    a.redo();
    frame(a);
    CHECK_EQ(a.list().rows.size(), size_t(2));
}

TEST(app_new_rows_cycle_colours) {
    App& a = freshApp();
    type(a, "y=x");
    a.key(Key::Enter, {});
    type(a, "y=2x");
    a.key(Key::Enter, {});
    type(a, "y=3x");
    auto& r = a.list().rows;
    CHECK(r[0].color.r == theme::CurveRed.r && r[1].color.b == theme::CurveBlue.b && r[2].color.g == theme::CurveGreen.g);
}

TEST(app_paste_multiline_makes_rows) {
    App& a = freshApp();
    a.pasteText("y=x^2\ny=\\sin x\n");
    frame(a);
    CHECK_EQ(a.list().rows.size(), size_t(2));
    CHECK_EQ(latexOfRow(a, 1), std::string("y=\\sin x"));
}

TEST(app_slider_suggestion_click) {
    App& a = freshApp();
    type(a, "y=mx");
    frame(a);
    CHECK(a.list().rows[0].analysis.kind == RowKind::Error);
    // Click the first "add slider" pill: it sits right of the "add slider:" label in row 1.
    const auto& row = a.list().rows[0];
    float y = a.list().rowTopOnScreen(0) + row.h - theme::SuggestionHeight - 8 + 8;
    float x = a.list().area.x + theme::TabWidth + theme::MathLeftPad +
              a.renderer().atlas().measure(Font::Ui, std::string("add slider:"), theme::SliderLabelSize) + 14;
    a.mouseDown(x, y, 1, {});
    a.mouseUp(x, y);
    frame(a);
    CHECK_EQ(a.list().rows.size(), size_t(2));
    CHECK_EQ(latexOfRow(a, 1), std::string("m=1"));
    CHECK(a.list().rows[0].analysis.kind == RowKind::Graph);
}

TEST(app_save_load_roundtrip) {
    App& a = freshApp();
    a.pasteText("y=x^2\na=2\nx^{2}+y^{2}\\le 4");
    frame(a);
    const char* tmp = std::getenv("TMPDIR");
    std::string path = std::string(tmp ? tmp : "/tmp") + "/graphingcalc_test_roundtrip.json", err;
    CHECK(a.saveFile(path, err));
    App& b = freshApp();
    CHECK(b.loadFile(path, err));
    frame(b);
    CHECK_EQ(b.list().rows.size(), size_t(3));
    CHECK_EQ(latexOfRow(b, 2), std::string("x^{2}+y^{2}\\le 4"));
    CHECK(b.list().rows[1].analysis.kind == RowKind::Slider);
}
