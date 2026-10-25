// Performance gate for the CLAUDE.md targets (M3 MacBook Air baseline). Headless, offscreen.
//   calc_bench          print results
//   calc_bench --check  exit 1 if any target is missed (Stop hook)
#include "app/App.hpp"
#include "render/Renderer.hpp"

#include <Metal/Metal.hpp>
#include <CoreFoundation/CoreFoundation.h>

#include <cmath>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace calc;

namespace {

double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

struct Result {
    const char* name;
    double value, target;
    const char* unit;
    bool ok() const { return value <= target; }
};

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0 : v[v.size() / 2];
}

const char* kFifty[] = {
    "y=\\sin x", "y=\\cos x", "y=x^{2}", "y=x^{3}-2x", "y=\\frac{1}{x}", "y=\\tan x", "y=\\sqrt{x}",
    "y=\\ln x", "y=e^{x}", "y=\\left|x\\right|", "y=\\sin\\left(3x\\right)", "y=\\frac{\\sin x}{x}",
    "y=x\\sin x", "y=\\cos\\left(x^{2}\\right)", "y=\\operatorname{floor}\\left(x\\right)",
    "y=\\frac{x^{2}-1}{x-2}", "y=2^{x}", "y=\\arctan x", "y=\\sinh x", "y=\\cosh\\left(x\\right)-3",
    "y=x^{4}-4x^{2}", "y=\\sqrt{9-x^{2}}", "y=-\\sqrt{9-x^{2}}", "y=\\sin\\left(x\\right)+\\cos\\left(2x\\right)",
    "y=0.1x^{3}", "y=\\log x", "y=\\frac{1}{1+e^{-x}}", "y=\\operatorname{mod}\\left(x,2\\right)",
    "y=\\sin\\left(10x\\right)", "y=x^{\\frac{1}{3}}", "y=3", "y=-2", "x=1", "x=-4",
    "r=2\\sin\\left(3\\theta\\right)", "r=\\theta", "\\left(\\cos t,\\sin t\\right)",
    "\\left(2\\cos t,\\sin\\left(2t\\right)\\right)", "y=\\frac{x}{x^{2}+1}", "y=\\sec x",
    "y=\\csc x", "y=\\cot x", "y=x+\\left[1,2,3\\right]", "y=e^{-x^{2}}", "y=\\cos\\left(5x\\right)e^{-0.2x^{2}}",
    "y=\\left(x-1\\right)\\left(x+2\\right)\\left(x-3\\right)", "y=\\sqrt{\\left|x\\right|}", "y=\\sin\\left(\\frac{1}{x}\\right)",
    "y=1.5\\sin\\left(x+1\\right)", "y=\\frac{\\left|x\\right|}{x}", "x^{2}+y^{2}=16",
};

} // namespace

int main(int argc, char** argv) {
    bool check = argc > 1 && std::strcmp(argv[1], "--check") == 0;
    MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    std::vector<Result> results;
    const float W = 1470, H = 900;  // a full-screen window on a 13" Air (points)

    // 1) Keystroke -> updated graph (analysis + sampling + encode + GPU), 10 rows on screen.
    {
        App app;
        std::string err;
        if (!app.init(dev, METALLIB_PATH, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 2; }
        app.setSize(W, H, 2);
        for (int i = 0; i < 10; ++i) app.addExpression(kFifty[i]);
        MTL::Texture* tex = app.renderer().makeOffscreenTarget(int(W * 2), int(H * 2));
        app.render(tex, nullptr, true);
        app.focusRow(2);
        std::vector<double> times;
        const char* keys = "+\\sin x";
        for (int rep = 0; rep < 12; ++rep) {
            for (const char* k = "+2x"; *k; ++k) {
                double t0 = nowMs();
                app.key(Key::Char, {}, std::u32string(1, char32_t(*k)));
                app.render(tex, nullptr, true);
                times.push_back(nowMs() - t0);
            }
            for (int b = 0; b < 3; ++b) {
                double t0 = nowMs();
                app.key(Key::Backspace, {});
                app.render(tex, nullptr, true);
                times.push_back(nowMs() - t0);
            }
        }
        (void)keys;
        results.push_back({"keystroke_to_graph_ms (p50)", median(times), 16.6, "ms"});
        std::sort(times.begin(), times.end());
        results.push_back({"keystroke_to_graph_ms (p95)", times[size_t(times.size() * 0.95)], 16.6, "ms"});
        tex->release();
    }

    // 2) Pan with 50+ expressions: CPU work per frame.
    {
        App app;
        std::string err;
        app.init(dev, METALLIB_PATH, err);
        app.setSize(W, H, 2);
        for (auto* e : kFifty) app.addExpression(e);
        MTL::Texture* tex = app.renderer().makeOffscreenTarget(int(W * 2), int(H * 2));
        app.render(tex, nullptr, true);
        std::vector<double> cpu, gpu;
        float x = 900, y = 450;
        app.mouseDown(x, y, 1, {});
        for (int f = 0; f < 120; ++f) {
            x += 7;  // ~420pt/s at 60 fps
            y += 2;
            app.mouseDragged(x, y);
            app.render(tex, nullptr, f % 10 == 9);
            cpu.push_back(app.lastFrameCpuMs());
            if (f % 10 == 9) gpu.push_back(app.renderer().stats().gpuMs);
        }
        app.mouseUp(x, y);
        results.push_back({"pan_50_exprs_cpu_ms (p50)", median(cpu), 4.0, "ms"});
        std::sort(cpu.begin(), cpu.end());
        results.push_back({"pan_50_exprs_cpu_ms (p95)", cpu[size_t(cpu.size() * 0.95)], 8.0, "ms"});
        results.push_back({"pan_50_exprs_gpu_ms (p50)", median(gpu), 16.6, "ms"});

        // zooming forces a resample of every curve each frame (worst case)
        std::vector<double> zoom;
        for (int f = 0; f < 40; ++f) {
            app.scroll(900, 450, 0, f < 20 ? 6.f : -6.f, true);
            app.render(tex, nullptr, false);
            zoom.push_back(app.lastFrameCpuMs());
        }
        results.push_back({"zoom_50_exprs_cpu_ms (p50)", median(zoom), 8.0, "ms"});
        tex->release();
    }

    // 3) Full-window implicit curve: GPU time (frame with the implicit minus an empty frame).
    {
        auto gpuFor = [&](const char* latex) {
            App app;
            std::string err;
            app.init(dev, METALLIB_PATH, err);
            app.setSize(W, H, 2);
            if (latex) app.addExpression(latex);
            MTL::Texture* tex = app.renderer().makeOffscreenTarget(int(W * 2), int(H * 2));
            std::vector<double> g;
            for (int i = 0; i < 15; ++i) {
                app.setNeedsRedraw();
                app.render(tex, nullptr, true);
                if (i >= 3) g.push_back(app.renderer().stats().gpuMs);
            }
            tex->release();
            return median(g);
        };
        double base = gpuFor(nullptr);
        double circle = gpuFor("x^{2}+y^{2}=25");
        double hard = gpuFor("\\sin\\left(x^{2}+y\\right)=\\cos\\left(xy\\right)");
        results.push_back({"implicit_circle_gpu_ms", std::max(0.0, circle - base), 2.0, "ms"});
        results.push_back({"implicit_trig_gpu_ms", std::max(0.0, hard - base), 2.0, "ms"});
        results.push_back({"empty_frame_gpu_ms", base, 2.0, "ms"});
    }

    // 4) Heavy curve (a 512-term Fourier drawing, like a user's test graph): full resample cost, and
    //    zoom frames with background refinement (the interactive app's mode).
    {
        App app;
        std::string err;
        app.init(dev, METALLIB_PATH, err);
        app.setSize(W, H, 2);
        auto list = [](const char* name, double seed) {
            std::string s = std::string(name) + "=\\left[";
            for (int k = 1; k <= 512; ++k) {
                char buf[32];
                std::snprintf(buf, sizeof buf, "%s%.4f", k > 1 ? "," : "", std::sin(k * seed) * 3 / k);
                s += buf;
            }
            return s + "\\right]";
        };
        app.pasteText(list("a", 1.3) + "\n" + list("b", 2.1) + "\n" + list("c", 0.7) + "\n" + list("d", 1.9) + "\n" +
                      "\\left(\\sum_{k=1}^{512}\\left(a\\left[k\\right]\\cos\\left(kt\\right)+b\\left[k\\right]\\sin\\left(kt\\right)\\right),"
                      "\\sum_{k=1}^{512}\\left(c\\left[k\\right]\\cos\\left(kt\\right)+d\\left[k\\right]\\sin\\left(kt\\right)\\right)\\right)");
        app.list().rows.back().domainMax.set("2\\pi");
        app.list().blur();
        MTL::Texture* tex = app.renderer().makeOffscreenTarget(int(W * 2), int(H * 2));
        app.render(tex, nullptr, true);
        std::vector<double> resample;
        for (int f = 0; f < 4; ++f) {
            app.scroll(1000, 450, 0, 4, true);
            app.render(tex, nullptr, true);
            resample.push_back(app.graph().lastSampleMs());
        }
        results.push_back({"heavy_curve_resample_ms (p50)", median(resample), 60.0, "ms"});
        app.graph().asyncSampling = true;
        std::vector<double> zoom;
        for (int f = 0; f < 40; ++f) {
            app.scroll(1000, 450, 0, 4, true);
            app.render(tex, nullptr, false);
            zoom.push_back(app.lastFrameCpuMs());
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true);  // let background results land
        }
        results.push_back({"heavy_curve_zoom_frame_ms (p50)", median(zoom), 4.0, "ms"});
        tex->release();
    }

    bool allOk = true;
    for (auto& r : results) {
        std::printf("%-32s %8.3f %s  (target <= %.1f)  %s\n", r.name, r.value, r.unit, r.target, r.ok() ? "OK" : "MISS");
        allOk &= r.ok();
    }
    dev->release();
    return check && !allOk ? 1 : 0;
}
