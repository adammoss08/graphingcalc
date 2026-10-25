#pragma once
// Frame orchestration. One command buffer per frame:
//   [compute] implicit field eval -> marching squares -> indirect args   (only when needed)
//   [render ] UI below graph content (grid, axes, labels) -> inequality fills -> curves (CPU +
//             GPU-implicit) -> UI above graph content (points, POIs, labels) -> panel chrome
#include "graph/Sampler.hpp"
#include "graph/Viewport.hpp"
#include "math/Vm.hpp"
#include "render/Batch2D.hpp"
#include "ui/Theme.hpp"

#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <memory>
#include <string>
#include <vector>

namespace calc {

struct CurveDraw {
    const CurvePoints* points;
    Color color;
    float width = theme::CurveWidth;
    float opacity = theme::CurveOpacity;
    float dash = 0;           // 0 solid; > 0 dash length (gap = 7/11 of it); < 0 dotted with this period
    float zoom = 1;           // view scale / points->scale (stale samples drawn rescaled)
};

struct ImplicitDraw {
    const Program* program;   // F(x, y); contour F = 0, region F > 0
    Color color;
    float width = theme::CurveWidth;
    float opacity = theme::CurveOpacity;
    float dash = 0;           // as CurveDraw
    bool contour = true;      // draw F = 0
    bool fill = false;        // shade F > 0
    float fillOpacity = theme::FillOpacity;
};

struct ImageDraw {
    int texture;              // Renderer::loadImage handle
    Rect rect;                // screen points
    float opacity;
};

struct SurfaceDraw3D {
    const Program* program;   // z = f(x, y)
    Color color;
    float opacity = 1;
};

// Everything 3D mode draws in the graph area.
struct Scene3D {
    simd_float4x4 viewProj;
    simd_float3 eye;          // camera position, world units
    float halfSize = 10;      // L: the visible box is [-L, L]^3
    float gridStep = 2;       // grid spacing (floor grid and surface lines)
    std::vector<SurfaceDraw3D> surfaces;
    std::vector<Line3D> lines;   // axes, grids, space curves, points (a == b)
};

struct GraphFrame {
    Rect area;                // graph area on screen (points)
    Viewport vp;
    std::vector<CurveDraw> curves;
    std::vector<ImplicitDraw> implicits;
    std::vector<ImageDraw> images;
    bool threeD = false;
    Scene3D scene;
};

struct FrameDesc {
    const Batch2D* ui = nullptr;
    size_t underEnd = 0;      // ui[0, underEnd) draws beneath curves
    size_t overEnd = 0;       // ui[underEnd, overEnd) above curves, clipped to the graph
    size_t listEnd = 0;       // ui[overEnd, listEnd) the expression list; then list images; rest is chrome
    std::vector<ImageDraw> listImages;   // image-row thumbnails
    Rect listClip;
    const GraphFrame* graph = nullptr;
    float viewW = 0, viewH = 0;  // points
    float scale = 2;             // device pixels per point
};

struct RenderStats {
    double cpuEncodeMs = 0;
    double gpuMs = 0;            // last completed frame
    size_t curveVertices = 0;
    size_t uiInstances = 0;
};

class Renderer3D;

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init(MTL::Device* device, const std::string& metallibPath, std::string& error);
    MTL::Device* device() const { return m_device; }
    TextAtlas& atlas() { return *m_atlas; }

    // Encodes and commits a frame into `target`. Presents `drawable` if given. If `wait`, blocks
    // until the GPU has finished (screenshots, benchmarks).
    void render(const FrameDesc& f, MTL::Texture* target, CA::MetalDrawable* drawable, bool wait = false);

    // Offscreen target for screenshots/benchmarks (shared storage so it can be read back).
    MTL::Texture* makeOffscreenTarget(int widthPx, int heightPx);
    static bool savePng(MTL::Texture* tex, const std::string& path);

    // Loads an image file (PNG/JPEG/...) into a texture. Returns a handle or -1; aspect = h / w.
    int loadImage(const std::string& path, float& aspect);

    const RenderStats& stats() const { return m_stats; }

private:
    struct Impl;
    std::unique_ptr<Impl> m;
    MTL::Device* m_device = nullptr;
    std::unique_ptr<TextAtlas> m_atlas;
    std::unique_ptr<Renderer3D> m_3d;
    RenderStats m_stats;
};

} // namespace calc
