// 3D mode: classification of every 3D form, implicit-surface meshes (geometry checked against the
// exact surface), restrictions, parametric surfaces, inequalities, persistence, and the user's
// gyroid graph (tests/fixtures/gyroid.txt) end to end.
#include "Test.hpp"

#include "app/App.hpp"
#include "math/Analyzer.hpp"
#include "math/MathTree.hpp"

#include <Metal/Metal.hpp>

#include <cmath>
#include <fstream>

using namespace calc;

namespace {

constexpr int kW = 1200, kH = 800;

std::vector<RowAnalysis> analyze3d(const std::vector<std::string>& rows) {
    std::vector<MathTree> trees(rows.size());
    std::vector<RowInput> in;
    for (size_t i = 0; i < rows.size(); ++i) {
        trees[i].insertLatex(rows[i]);
        in.push_back({&trees[i]});
    }
    AnalyzerSettings s;
    s.threeD = true;
    return analyzeGraph(in, s);
}

App& fresh3d() {
    static MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    static App* app = nullptr;
    delete app;
    app = new App();
    std::string err;
    if (!app->init(dev, METALLIB_PATH, err)) std::printf("init failed: %s\n", err.c_str());
    app->setSize(kW, kH, 2);
    app->toggle3D();
    return *app;
}

// Renders until GPU meshes are complete (a mesh that outgrew its buffers is rebuilt larger).
void settle(App& a) {
    MTL::Texture* t = a.renderer().makeOffscreenTarget(64, 64);
    a.render(t, nullptr, true);
    for (int i = 0; i < 10 && a.renderer().wantsAnotherFrame(); ++i) a.render(t, nullptr, true);
    t->release();
}

uint32_t pixelAt(App& a, float x, float y) {
    MTL::Texture* t = a.renderer().makeOffscreenTarget(kW * 2, kH * 2);
    a.render(t, nullptr, true);
    for (int i = 0; i < 10 && a.renderer().wantsAnotherFrame(); ++i) a.render(t, nullptr, true);
    uint32_t px = 0;
    t->getBytes(&px, 4, MTL::Region(NS::UInteger(x * 2), NS::UInteger(y * 2), 1, 1), 0);
    t->release();
    return px;
}

struct Mesh {
    std::vector<float> xyz;
    std::vector<uint32_t> tris;
    size_t vertices() const { return xyz.size() / 3; }
    simd_float3 v(uint32_t i) const { return simd_make_float3(xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]); }
    double area() const {
        double s = 0;
        for (size_t k = 0; k + 2 < tris.size(); k += 3)
            s += 0.5 * simd_length(simd_cross(v(tris[k + 1]) - v(tris[k]), v(tris[k + 2]) - v(tris[k])));
        return s;
    }
};

Mesh meshOf(App& a) {
    Mesh m;
    a.renderer().debugMesh(m.xyz, m.tris);
    return m;
}

} // namespace

TEST(three_d_classifies_every_form) {
    auto r = analyze3d({
        "x^2+y^2+z^2=9",                                  // 0 sphere
        "y=x^2/4",                                        // 1 parabolic cylinder
        "x=3",                                            // 2 plane
        "r=2",                                            // 3 cylinder (cylindrical r)
        "\\rho=3",                                        // 4 sphere (spherical ρ)
        "\\rho=2+\\sin(3\\theta)\\sin(4\\phi)",           // 5 spherical harmonic-ish
        "(\\cos(2\\pi u),\\sin(2\\pi u),v)",              // 6 parametric surface
        "(\\cos t,\\sin t,t)",                            // 7 space curve
        "(1,2,3)",                                        // 8 point
        "z<\\sin x",                                      // 9 inequality surface
        "x^2+y^2+z^2<4",                                  // 10 implicit inequality
        "x^2+y^2+z^2=9\\{z>0\\}",                         // 11 restricted implicit
        "z=x^2-y^2",                                      // 12 explicit surface
        "1=2",                                            // 13 nothing
    });
    CHECK(r[0].graph == GraphKind::Implicit3D);
    CHECK(r[1].graph == GraphKind::Implicit3D);
    CHECK(r[2].graph == GraphKind::Implicit3D);
    CHECK(r[3].graph == GraphKind::Implicit3D);
    CHECK(r[4].graph == GraphKind::Implicit3D);
    CHECK(r[5].graph == GraphKind::Implicit3D);
    CHECK(r[6].graph == GraphKind::ParametricSurface);
    CHECK(r[7].graph == GraphKind::Parametric3D);
    CHECK(r[8].graph == GraphKind::Point3D);
    CHECK(r[9].graph == GraphKind::Surface && r[9].rel == Cmp::Lt);
    CHECK(r[10].graph == GraphKind::Implicit3D && r[10].rel == Cmp::Lt);
    CHECK(r[11].graph == GraphKind::Implicit3D);
    CHECK(r[12].graph == GraphKind::Surface && r[12].rel == Cmp::Eq);
    CHECK(r[13].kind == RowKind::None);
    for (int i = 0; i <= 12; ++i) CHECK_EQ(r[i].error, std::string());
    // ρ and φ evaluate as spherical coordinates; z rides in the t slot.
    CHECK_NEAR(evaluate(r[4].pieces[0].f, 1, 2, 2), 0.0, 1e-12);   // |(1,2,2)| = 3
    CHECK_NEAR(evaluate(r[0].pieces[0].f, 0, 0, 3), 0.0, 1e-12);
    // Parametric surface programs take u, v in the x, y slots.
    CHECK_NEAR(evaluate(r[6].pieces[0].h, 0.25, 0.7), 0.7, 1e-12);
}

TEST(three_d_parameters_and_errors) {
    auto r = analyze3d({"u=3", "(u,u,1)", "(t,u,v)", "(u,v)", "(\\cos t,\\sin t,t)", "x^2+y^2+z^2+w=1"});
    CHECK(r[1].graph == GraphKind::Point3D);           // a defined u is a variable, not a parameter
    CHECK_NEAR(r[1].pieces[0].px, 3.0, 0);
    CHECK(r[2].kind == RowKind::Error);                 // t with u/v: ambiguous
    CHECK(r[3].kind == RowKind::Error);                 // surfaces need 3 coordinates
    CHECK(r[5].kind == RowKind::Error);                 // w undefined
    CHECK(r[5].error.find("w") != std::string::npos);
}

TEST(three_d_space_curve_uses_domain_fields) {
    MathTree t, lo, hi;
    t.insertLatex("(\\cos t,\\sin t,t)");
    lo.insertLatex("-2");
    hi.insertLatex("5");
    RowInput ri{&t};
    ri.domainMin = &lo;
    ri.domainMax = &hi;
    AnalyzerSettings s;
    s.threeD = true;
    auto r = analyzeGraph({ri}, s);
    CHECK(r[0].graph == GraphKind::Parametric3D);
    CHECK_NEAR(r[0].tMin, -2.0, 0);
    CHECK_NEAR(r[0].tMax, 5.0, 0);
}

// The surface-nets mesh of a sphere: every vertex on the sphere (within a grid cell), triangle
// indices valid, total area ~ 4πr².
TEST(three_d_implicit_sphere_mesh_is_accurate) {
    App& a = fresh3d();
    a.pasteText("x^2+y^2+z^2=25");
    settle(a);
    Mesh m = meshOf(a);
    CHECK(m.vertices() > 2000);
    CHECK(m.tris.size() > 3 * m.vertices());
    double cell = 2 * 10.0 / (theme::Implicit3DGridN - 1);
    double worst = 0;
    for (size_t i = 0; i < m.vertices(); ++i) worst = std::max(worst, std::fabs(double(simd_length(m.v(uint32_t(i)))) - 5));
    CHECK(worst < cell);
    bool indicesOk = true;
    for (uint32_t k : m.tris) indicesOk &= k < m.vertices();
    CHECK(indicesOk);
    double area = m.area(), exact = 4 * M_PI * 25;
    CHECK(std::fabs(area - exact) / exact < 0.03);
    CHECK_EQ(a.renderer().stats().gpuErrors, 0u);
    CHECK(!a.renderer().stats().meshOverflowed);
}

TEST(three_d_restriction_clips_implicit) {
    App& a = fresh3d();
    a.pasteText("x^2+y^2+z^2=25\\{z>0\\}");
    settle(a);
    Mesh m = meshOf(a);
    CHECK(m.vertices() > 1000);
    float minZ = 1e9f;
    for (size_t i = 0; i < m.vertices(); ++i) minZ = std::min(minZ, m.v(uint32_t(i)).z);
    CHECK(minZ > -0.2f);   // nothing below the restriction (allowing one cell)
    double area = m.area(), half = 2 * M_PI * 25;
    CHECK(std::fabs(area - half) / half < 0.05);
}

TEST(three_d_undefined_and_empty_implicits_draw_nothing) {
    App& a = fresh3d();
    a.pasteText("x^2+y^2+z^2=-1\n\\sqrt{-1-x^2}=z");
    settle(a);
    CHECK(a.list().rows[0].analysis.graph == GraphKind::Implicit3D);
    Mesh m = meshOf(a);
    CHECK_EQ(m.vertices(), size_t(0));
    CHECK_EQ(a.renderer().stats().gpuErrors, 0u);
}

TEST(three_d_slider_change_rebuilds_mesh) {
    App& a = fresh3d();
    a.pasteText("R=3\nx^2+y^2+z^2=R^2");
    settle(a);
    double area3 = meshOf(a).area();
    a.focusRow(0);                       // edit "R=3" -> "R=6" as a user would
    a.key(Key::End, {});
    a.key(Key::Backspace, {});
    a.key(Key::Char, {}, U"6");
    settle(a);
    double area6 = meshOf(a).area();
    CHECK(std::fabs(area3 - 4 * M_PI * 9) / (4 * M_PI * 9) < 0.05);
    CHECK(std::fabs(area6 - 4 * M_PI * 36) / (4 * M_PI * 36) < 0.05);
}

// The user's graph (three triple-gyroid level sets clipped to a ball, n = 18) meshes completely,
// stays inside the ball, and leaves no GPU errors.
TEST(three_d_gyroid_fixture_end_to_end) {
    std::ifstream f(FIXTURES_DIR "/gyroid.txt");
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(!text.empty());
    App& a = fresh3d();
    a.pasteText(text);
    settle(a);
    const auto& rows = a.list().rows;
    CHECK(rows.size() >= 5);
    for (size_t i = 2; i < 5 && i < rows.size(); ++i) {
        CHECK(rows[i].analysis.graph == GraphKind::Implicit3D);
        CHECK_EQ(rows[i].analysis.error, std::string());
    }
    const RenderStats& s = a.renderer().stats();
    CHECK_EQ(s.gpuErrors, 0u);
    CHECK(!s.meshOverflowed);
    CHECK(s.meshTriangles > 1000000);
    Mesh m = meshOf(a);
    double cell = 2 * 6.0 / (theme::Implicit3DGridN - 1);
    double worst = 0;
    for (size_t i = 0; i < m.vertices(); ++i) worst = std::max(worst, double(simd_length(m.v(uint32_t(i)))));
    CHECK(worst < 5 + 2 * cell);
}

TEST(three_d_parametric_and_inequality_render) {
    App& a = fresh3d();   // default camera: azimuth -0.95, elevation 0.42
    a.pasteText("(4\\cos(2\\pi u),4\\sin(2\\pi u),3v-1.5)");   // cylinder of radius 4 (red)
    settle(a);   // analysis runs at render time
    CHECK(a.list().rows[0].analysis.graph == GraphKind::ParametricSurface);
    auto screenOf = [](App& app, double x, double y, double z, float& sx, float& sy) {
        const calc::Rect& r = app.graph().area;
        float d;
        app.graph().cam.project(x, y, z, r.w, r.h, sx, sy, d);
        sx += r.x;
        sy += r.y;
    };
    float sx, sy;
    double az = a.graph().cam.azimuth;
    screenOf(a, 4 * std::cos(az), 4 * std::sin(az), 0.7, sx, sy);   // the wall facing the camera
    uint32_t wall = pixelAt(a, sx, sy);
    CHECK(((wall >> 16) & 0xff) > ((wall >> 8) & 0xff) + 50);
    screenOf(a, -9, 9, -1.5, sx, sy);   // far floor corner behind it: no surface, light paper
    uint32_t paper = pixelAt(a, sx, sy);
    CHECK(((paper >> 8) & 0xff) > 180);
    CHECK_EQ(a.renderer().stats().gpuErrors, 0u);
    // Opaque vs translucent z = 0: the translucent one is lighter (white paper shows through).
    App& b = fresh3d();
    b.setCamera3D(-0.95, 1.4, 10);
    b.pasteText("z=0");
    float px, py;
    screenOf(b, 1.3, 1.1, 0, px, py);   // between floor grid lines
    uint32_t opaque = pixelAt(b, px, py);
    App& c = fresh3d();
    c.setCamera3D(-0.95, 1.4, 10);
    c.pasteText("z<0");
    uint32_t translucent = pixelAt(c, px, py);
    int og = (opaque >> 8) & 0xff, tg = (translucent >> 8) & 0xff, orr = (opaque >> 16) & 0xff;
    CHECK(orr > og + 50);   // opaque red surface
    CHECK(tg > og + 40);    // translucent: much lighter green channel
}

TEST(three_d_camera_saves_and_loads) {
    App& a = fresh3d();
    a.setCamera3D(0.5, 0.9, 4);
    a.pasteText("x^2+y^2+z^2=9");
    settle(a);
    const char* tmp = std::getenv("TMPDIR");
    std::string path = std::string(tmp ? tmp : "/tmp") + "/graphingcalc_test_cam3d.json", err;
    CHECK(a.saveFile(path, err));
    App& b = fresh3d();
    CHECK(b.loadFile(path, err));
    CHECK_NEAR(b.graph().cam.azimuth, 0.5, 1e-9);
    CHECK_NEAR(b.graph().cam.elevation, 0.9, 1e-9);
    CHECK_NEAR(b.graph().cam.halfSize, 4.0, 1e-9);
    CHECK(b.is3D());
}

// Tearing an app down while heavy GPU frames are still in flight must wait for them: their
// completion handlers use the renderer (regression: crashed under Metal validation in 1.3.0 dev).
TEST(three_d_teardown_with_frames_in_flight) {
    for (int round = 0; round < 3; ++round) {
        App& a = fresh3d();
        a.pasteText("n=18\n\\sin(nx)\\cos(ny)+\\sin(ny)\\cos(nz)+\\sin(nz)\\cos(nx)=0");
        MTL::Texture* t = a.renderer().makeOffscreenTarget(256, 256);
        for (int i = 0; i < 3; ++i) a.render(t, nullptr, false);   // don't wait
        t->release();
    }   // the next fresh3d() deletes the app with frames in flight
    App& last = fresh3d();
    CHECK(last.is3D());
}
