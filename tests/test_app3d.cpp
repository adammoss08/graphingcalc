#include "Test.hpp"

#include "app/App.hpp"

#include <Metal/Metal.hpp>

using namespace calc;

namespace {

constexpr int kW = 1200, kH = 800;

App& freshApp3d() {
    static MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    static App* app = nullptr;
    delete app;
    app = new App();
    std::string err;
    if (!app->init(dev, METALLIB_PATH, err)) std::printf("init failed: %s\n", err.c_str());
    app->setSize(kW, kH, 2);
    return *app;
}

// Renders a full-size frame and returns the BGRA pixel at (x, y) points.
uint32_t pixelAt(App& a, float x, float y) {
    MTL::Texture* t = a.renderer().makeOffscreenTarget(kW * 2, kH * 2);
    a.render(t, nullptr, true);
    uint32_t px = 0;
    t->getBytes(&px, 4, MTL::Region(NS::UInteger(x * 2), NS::UInteger(y * 2), 1, 1), 0);
    t->release();
    return px;
}

void click(App& a, float x, float y) {
    a.mouseDown(x, y, 1, {});
    a.mouseUp(x, y);
}

// Centre of the 2D/3D button (below the zoom buttons; see GraphView::buttonRect).
void modeButton(App& a, float& x, float& y) {
    const calc::Rect& r = a.graph().area;
    using namespace theme;
    x = r.right() - ZoomButtonInset - ZoomButtonSize * 0.5f;
    y = r.y + ZoomButtonInset + 4 * ZoomButtonSize + 2 * ZoomButtonGap + ModeButtonGap + ZoomButtonSize * 0.5f;
}

} // namespace

TEST(app3d_mode_button_toggles_and_reclassifies) {
    App& a = freshApp3d();
    a.pasteText("z=x^2+y^2\n(1,2,3)");
    pixelAt(a, 0, 0);
    CHECK(!a.is3D());
    CHECK(a.list().rows[0].analysis.kind == RowKind::Error);   // z isn't a coordinate in 2D
    float bx, by;
    modeButton(a, bx, by);
    click(a, bx, by);
    pixelAt(a, 0, 0);
    CHECK(a.is3D());
    CHECK(a.list().rows[0].analysis.graph == GraphKind::Surface);
    CHECK(a.list().rows[1].analysis.graph == GraphKind::Point3D);
    click(a, bx, by);
    pixelAt(a, 0, 0);
    CHECK(!a.is3D());
}

TEST(app3d_orbit_zoom_home) {
    App& a = freshApp3d();
    a.toggle3D();
    const calc::Rect& r = a.graph().area;
    float cx = r.x + r.w * 0.4f, cy = r.y + r.h * 0.5f;
    Camera3D before = a.graph().cam;
    a.mouseDown(cx, cy, 1, {});
    a.mouseDragged(cx + 50, cy + 20);
    a.mouseUp(cx + 50, cy + 20);
    CHECK(a.graph().cam.azimuth < before.azimuth);
    CHECK(a.graph().cam.elevation > before.elevation);
    CHECK(a.graph().vp.cx == Viewport::defaultFor(r.w, r.h).cx);   // orbiting doesn't pan the 2D view
    a.zoomIn();
    CHECK_NEAR(a.graph().cam.halfSize, before.halfSize / theme::ZoomButtonFactor, 1e-9);
    a.resetZoom();
    CHECK(a.graph().cam.isDefault());
}

// The GPU path end to end: z = 0 covers the middle of the graph area in the surface colour.
TEST(app3d_surface_renders_on_gpu) {
    App& a = freshApp3d();
    a.toggle3D();
    a.pasteText("z=0");
    pixelAt(a, 0, 0);
    CHECK(a.list().rows[0].analysis.graph == GraphKind::Surface);
    const calc::Rect& r = a.graph().area;
    uint32_t px = pixelAt(a, r.x + r.w * 0.62f, r.y + r.h * 0.68f);   // clear of axes and tick labels
    int b = px & 0xff, g = (px >> 8) & 0xff, rr = (px >> 16) & 0xff;
    CHECK(rr > 120 && rr > g + 50 && rr > b + 50);   // red-ish (first colour in the cycle), lit
}

TEST(app3d_save_load_keeps_mode) {
    App& a = freshApp3d();
    a.toggle3D();
    a.pasteText("z=\\sin x");
    pixelAt(a, 0, 0);
    const char* tmp = std::getenv("TMPDIR");
    std::string path = std::string(tmp ? tmp : "/tmp") + "/graphingcalc_test_3d.json", err;
    CHECK(a.saveFile(path, err));
    App& b = freshApp3d();
    CHECK(b.loadFile(path, err));
    pixelAt(b, 0, 0);
    CHECK(b.is3D());
    CHECK(b.list().rows[0].analysis.graph == GraphKind::Surface);
}
