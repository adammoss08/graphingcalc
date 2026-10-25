#include "render/Renderer.hpp"
#include "Log.hpp"
#include "graph/ImplicitCpu.hpp"
#include "render/RenderFormats.hpp"
#include "render/Renderer3D.hpp"
#include "render/RingBuffer.hpp"
#include "render/ShaderTypes.hpp"

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <atomic>
#include <chrono>
#include <cmath>

namespace calc {

namespace {

constexpr uint32_t kSegmentCapacity = 1 << 19;
constexpr int kMaxImplicitPieces = 128;

NS::String* str(const char* s) { return NS::String::string(s, NS::UTF8StringEncoding); }

double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

struct Renderer::Impl {
    MTL::CommandQueue* queue = nullptr;
    MTL::Library* lib = nullptr;
    MTL::RenderPipelineState* uiPipe = nullptr;
    MTL::RenderPipelineState* curvePipe = nullptr;
    MTL::RenderPipelineState* implicitPipe = nullptr;
    MTL::RenderPipelineState* fillPipe = nullptr;
    MTL::ComputePipelineState* fieldPipe = nullptr;
    MTL::ComputePipelineState* edgePipe = nullptr;
    MTL::ComputePipelineState* contourPipe = nullptr;
    MTL::ComputePipelineState* argsPipe = nullptr;

    FrameRing ring;
    RingBuffer uiBuf, vertBuf, styleBuf, codeBuf, constBuf, pieceBuf;

    // Persistent GPU-only resources (hazard-tracked, so frames in flight are ordered).
    MTL::Texture* field = nullptr;
    MTL::Texture* hEdge = nullptr;   // refined crossing parameter per horizontal grid edge
    MTL::Texture* vEdge = nullptr;   // ... per vertical grid edge
    MTL::Buffer* segments = nullptr;
    MTL::Buffer* counter = nullptr;
    MTL::Buffer* drawArgs = nullptr;
    MTL::Texture* tileBase = nullptr;
    MTL::Texture* tileId = nullptr;
    MTL::Texture* tileDepth = nullptr;   // memoryless; only 3D mode tests against it

    // Per-frame scratch, reused to avoid allocations.
    std::vector<CurveVertex> verts;
    std::vector<CurveStyle> styles;
    std::vector<uint32_t> code;
    std::vector<float> consts;
    std::vector<ImplicitPiece> pieces;
    std::vector<int> fillSlices;
    std::vector<float> fillOpacities;
    std::vector<MTL::Texture*> images;   // loadImage handles

    std::atomic<double> gpuMs{0};
    MTL::CommandBuffer* lastCb = nullptr;   // retained; deep-zoom CPU uploads wait on it
    std::vector<float> cpuField, cpuH, cpuV;

    ~Impl() {
        if (field) field->release();
        if (hEdge) hEdge->release();
        if (vEdge) vEdge->release();
        if (lastCb) lastCb->release();
        if (segments) segments->release();
        if (counter) counter->release();
        if (drawArgs) drawArgs->release();
        for (auto* t : images) if (t) t->release();
        if (tileBase) tileBase->release();
        if (tileId) tileId->release();
        if (tileDepth) tileDepth->release();
        if (uiPipe) uiPipe->release();
        if (curvePipe) curvePipe->release();
        if (implicitPipe) implicitPipe->release();
        if (fillPipe) fillPipe->release();
        if (fieldPipe) fieldPipe->release();
        if (edgePipe) edgePipe->release();
        if (contourPipe) contourPipe->release();
        if (argsPipe) argsPipe->release();
        if (lib) lib->release();
        if (queue) queue->release();
    }

    void ensureTile(MTL::Device* dev, NS::UInteger w, NS::UInteger h) {
        if (tileBase && tileBase->width() == w && tileBase->height() == h) return;
        if (tileBase) tileBase->release();
        if (tileId) tileId->release();
        if (tileDepth) tileDepth->release();
        auto* d = MTL::TextureDescriptor::texture2DDescriptor(kBaseFormat, w, h, false);
        d->setStorageMode(MTL::StorageModeMemoryless);
        d->setUsage(MTL::TextureUsageRenderTarget);
        tileBase = dev->newTexture(d);
        d->setPixelFormat(kIdFormat);
        tileId = dev->newTexture(d);
        d->setPixelFormat(kDepthFormat);
        tileDepth = dev->newTexture(d);
    }

    void ensureField(MTL::Device* dev, uint32_t w, uint32_t h, uint32_t n) {
        if (field && field->width() >= w && field->height() >= h && field->arrayLength() >= n) return;
        uint32_t slices = std::max<uint32_t>(n, field ? uint32_t(field->arrayLength()) : 1);
        if (field) field->release();
        if (hEdge) hEdge->release();
        if (vEdge) vEdge->release();
        auto* d = MTL::TextureDescriptor::alloc()->init();
        d->setTextureType(MTL::TextureType2DArray);
        d->setPixelFormat(MTL::PixelFormatR32Float);
        d->setWidth(w);
        d->setHeight(h);
        d->setArrayLength(slices);
        d->setStorageMode(MTL::StorageModeShared);  // CPU-writable for the deep-zoom double path
        d->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite);
        field = dev->newTexture(d);
        field->setLabel(str("implicit field"));
        hEdge = dev->newTexture(d);
        vEdge = dev->newTexture(d);
        d->release();
    }
};

Renderer::Renderer() : m(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;

bool Renderer::init(MTL::Device* device, const std::string& metallibPath, std::string& error) {
    m_device = device;
    m->queue = device->newCommandQueue();
    NS::Error* err = nullptr;
    m->lib = device->newLibrary(str(metallibPath.c_str()), &err);
    if (!m->lib) {
        error = "Couldn't load " + metallibPath + (err ? std::string(": ") + err->localizedDescription()->utf8String() : "");
        return false;
    }
    auto fn = [&](const char* name) { return m->lib->newFunction(str(name)); };
    auto makeRender = [&](const char* vs, const char* fs, bool tile, bool blend) -> MTL::RenderPipelineState* {
        auto* d = MTL::RenderPipelineDescriptor::alloc()->init();
        auto* v = fn(vs);
        auto* f = fn(fs);
        d->setVertexFunction(v);
        d->setFragmentFunction(f);
        setAttachments(d, tile, blend);
        auto* p = device->newRenderPipelineState(d, &err);
        if (!p) error = std::string(vs) + ": " + (err ? err->localizedDescription()->utf8String() : "?");
        v->release();
        f->release();
        d->release();
        return p;
    };
    auto makeCompute = [&](const char* name) -> MTL::ComputePipelineState* {
        auto* f = fn(name);
        auto* p = device->newComputePipelineState(f, &err);
        if (!p) error = std::string(name) + ": " + (err ? err->localizedDescription()->utf8String() : "?");
        f->release();
        return p;
    };
    m->uiPipe = makeRender("uiVertex", "uiFragment", false, true);
    m->curvePipe = makeRender("curveVertex", "curveFragment", true, false);
    m->implicitPipe = makeRender("implicitSegmentVertex", "curveFragment", true, false);
    m->fillPipe = makeRender("fillVertex", "fillFragment", false, true);
    m->fieldPipe = makeCompute("implicitField");
    m->edgePipe = makeCompute("implicitEdges");
    m->contourPipe = makeCompute("implicitContour");
    m->argsPipe = makeCompute("implicitDrawArgs");
    if (!m->uiPipe || !m->curvePipe || !m->implicitPipe || !m->fillPipe || !m->fieldPipe || !m->edgePipe ||
        !m->contourPipe || !m->argsPipe)
        return false;

    m->uiBuf.init(device, "ui instances");
    m->vertBuf.init(device, "curve vertices");
    m->styleBuf.init(device, "curve styles");
    m->codeBuf.init(device, "implicit code");
    m->constBuf.init(device, "implicit consts");
    m->pieceBuf.init(device, "implicit pieces");
    m->segments = device->newBuffer(sizeof(ImplicitSegment) * kSegmentCapacity, MTL::ResourceStorageModePrivate);
    m->counter = device->newBuffer(16, MTL::ResourceStorageModePrivate);
    m->drawArgs = device->newBuffer(16, MTL::ResourceStorageModePrivate);

    m_3d = std::make_unique<Renderer3D>();
    if (!m_3d->init(device, m->lib, error)) return false;

    m_atlas = std::make_unique<TextAtlas>();
    m_atlas->init(device);
    return true;
}

MTL::Texture* Renderer::makeOffscreenTarget(int w, int h) {
    auto* d = MTL::TextureDescriptor::texture2DDescriptor(kColorFormat, w, h, false);
    d->setStorageMode(MTL::StorageModeShared);
    d->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    return m_device->newTexture(d);
}

void Renderer::render(const FrameDesc& f, MTL::Texture* target, CA::MetalDrawable* drawable, bool wait) {
    double t0 = nowMs();
    Impl& I = *m;
    int slot = I.ring.begin();
    auto* pool = NS::AutoreleasePool::alloc()->init();

    FrameUniforms fu{};
    fu.viewSize = {f.viewW, f.viewH};
    fu.scale = f.scale;

    // ---- gather curve geometry ----
    I.verts.clear();
    I.styles.clear();
    I.code.clear();
    I.consts.clear();
    I.pieces.clear();
    I.fillSlices.clear();
    I.fillOpacities.clear();
    const GraphFrame* g = f.graph;
    simd_float4 graphClip = {0, 0, 0, 0};
    if (g) {
        graphClip = {g->area.x, g->area.y, g->area.right(), g->area.bottom()};
        for (const CurveDraw& c : g->curves) {
            uint32_t idx = uint32_t(I.styles.size());
            CurveStyle st{};
            st.color = {c.color.r, c.color.g, c.color.b, c.opacity};
            st.clip = graphClip;
            st.translate = {float(g->area.x + g->vp.sx(c.points->anchorX)),
                            float(g->area.y + g->vp.sy(c.points->anchorY))};
            st.width = c.width;
            st.dash = c.dash;
            st.id = idx + 1;
            st.zoom = c.zoom;
            I.styles.push_back(st);
            const auto& xy = c.points->xy;
            float cum = 0;
            simd_float2 prev = {NAN, NAN};
            for (size_t k = 0; k + 1 < xy.size(); k += 2) {
                simd_float2 p = {xy[k], xy[k + 1]};
                if (std::isnan(p.x)) cum = 0;
                else if (!std::isnan(prev.x)) cum += simd_distance(p, prev);
                prev = p;
                I.verts.push_back({p, cum, idx});
            }
        }
        for (const ImplicitDraw& d : g->implicits) {
            if (int(I.pieces.size()) >= kMaxImplicitPieces) break;
            uint32_t idx = uint32_t(I.styles.size());
            CurveStyle st{};
            st.color = {d.color.r, d.color.g, d.color.b, d.opacity};
            st.clip = graphClip;
            st.translate = {g->area.x, g->area.y};
            st.zoom = 1;
            st.width = d.width;
            st.dash = d.dash;
            st.id = idx + 1;
            I.styles.push_back(st);
            ImplicitPiece pc{};
            pc.codeOffset = uint32_t(I.code.size());
            pc.codeLength = uint32_t(d.program->code.size());
            pc.constOffset = uint32_t(I.consts.size());
            pc.styleIndex = idx;
            pc.wantContour = d.contour ? 1 : 0;
            I.code.insert(I.code.end(), d.program->code.begin(), d.program->code.end());
            for (double v : d.program->consts) I.consts.push_back(float(v));
            if (d.fill) {
                I.fillSlices.push_back(int(I.pieces.size()));
                I.fillOpacities.push_back(d.fillOpacity);
            }
            I.pieces.push_back(pc);
        }
    }

    MTL::CommandBuffer* cb = I.queue->commandBuffer();
    cb->setLabel(str("frame"));

    // ---- implicit curves: GPU field + contour ----
    ImplicitUniforms iu{};
    bool haveImplicit = g && !I.pieces.empty();
    if (haveImplicit) {
        float cell = theme::ImplicitCellPts;
        iu.origin = {g->area.x, g->area.y};
        iu.graphSize = {g->area.w, g->area.h};
        iu.cellPts = cell;
        iu.scale = float(g->vp.scale);
        iu.centerX = float(g->vp.cx);
        iu.centerY = float(g->vp.cy);
        iu.gridW = uint32_t(std::ceil(g->area.w / cell)) + 2;
        iu.gridH = uint32_t(std::ceil(g->area.h / cell)) + 2;
        iu.pieceCount = uint32_t(I.pieces.size());
        iu.segmentCapacity = kSegmentCapacity;
        I.ensureField(m_device, iu.gridW, iu.gridH, iu.pieceCount);

        MTL::Buffer* codeB = I.codeBuf.upload(slot, I.code.data(), I.code.size() * 4);
        MTL::Buffer* constB = I.constBuf.upload(slot, I.consts.data(), I.consts.size() * 4);
        MTL::Buffer* pieceB = I.pieceBuf.upload(slot, I.pieces.data(), I.pieces.size() * sizeof(ImplicitPiece));

        auto* blit = cb->blitCommandEncoder();
        blit->fillBuffer(I.counter, NS::Range(0, 4), 0);
        blit->endEncoding();

        // Deep zoom: float can't resolve a cell here, so evaluate the field and edge crossings on
        // the CPU in double and upload them; the GPU still does marching squares and drawing.
        ImplicitGrid grid{g->vp.cx, g->vp.cy, g->vp.scale, g->area.w, g->area.h, cell, int(iu.gridW), int(iu.gridH)};
        bool cpuField = implicitNeedsDouble(grid);
        if (cpuField) {
            if (I.lastCb) I.lastCb->waitUntilCompleted();  // previous frames may still read the textures
            size_t pi = 0;
            for (const ImplicitDraw& d : g->implicits) {
                if (pi >= I.pieces.size()) break;
                implicitFieldCpu(*d.program, grid, I.cpuField, I.cpuH, I.cpuV);
                MTL::Region rg(0, 0, iu.gridW, iu.gridH);
                NS::UInteger row = iu.gridW * sizeof(float), img = row * iu.gridH;
                I.field->replaceRegion(rg, 0, NS::UInteger(pi), I.cpuField.data(), row, img);
                I.hEdge->replaceRegion(rg, 0, NS::UInteger(pi), I.cpuH.data(), row, img);
                I.vEdge->replaceRegion(rg, 0, NS::UInteger(pi), I.cpuV.data(), row, img);
                ++pi;
            }
        }
        auto* ce = cb->computeCommandEncoder();
        ce->setBuffer(codeB, 0, 0);
        ce->setBuffer(constB, 0, 1);
        ce->setBuffer(pieceB, 0, 2);
        ce->setBytes(&iu, sizeof iu, 3);
        ce->setTexture(I.field, 0);
        ce->setTexture(I.hEdge, 1);
        ce->setTexture(I.vEdge, 2);
        if (!cpuField) {
            ce->setComputePipelineState(I.fieldPipe);
            ce->dispatchThreads(MTL::Size(iu.gridW, iu.gridH, iu.pieceCount), MTL::Size(16, 16, 1));
            ce->memoryBarrier(MTL::BarrierScopeTextures);
            ce->setComputePipelineState(I.edgePipe);
            ce->dispatchThreads(MTL::Size(iu.gridW, iu.gridH, iu.pieceCount), MTL::Size(16, 16, 1));
            ce->memoryBarrier(MTL::BarrierScopeTextures);
        }
        ce->setComputePipelineState(I.contourPipe);
        ce->setBuffer(I.segments, 0, 4);
        ce->setBuffer(I.counter, 0, 5);
        ce->dispatchThreads(MTL::Size(iu.gridW - 1, iu.gridH - 1, iu.pieceCount), MTL::Size(16, 16, 1));
        ce->memoryBarrier(MTL::BarrierScopeBuffers);
        ce->setComputePipelineState(I.argsPipe);
        ce->setBuffer(I.counter, 0, 0);
        ce->setBuffer(I.drawArgs, 0, 1);
        ce->setBytes(&iu, sizeof iu, 2);
        ce->dispatchThreads(MTL::Size(1, 1, 1), MTL::Size(1, 1, 1));
        ce->endEncoding();
    }

    bool threeD = g && g->threeD;
    if (threeD) m_3d->encodeCompute(cb, slot, g->scene);

    // ---- render pass ----
    I.ensureTile(m_device, target->width(), target->height());
    auto* rpd = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* a0 = rpd->colorAttachments()->object(0);
    a0->setTexture(target);
    a0->setLoadAction(MTL::LoadActionClear);
    a0->setClearColor(MTL::ClearColor(theme::GraphBg.r, theme::GraphBg.g, theme::GraphBg.b, 1));  // the graph paper
    a0->setStoreAction(MTL::StoreActionStore);
    auto* a1 = rpd->colorAttachments()->object(1);
    a1->setTexture(I.tileBase);
    a1->setLoadAction(MTL::LoadActionClear);
    a1->setClearColor(MTL::ClearColor(0, 0, 0, 0));
    a1->setStoreAction(MTL::StoreActionDontCare);
    auto* a2 = rpd->colorAttachments()->object(2);
    a2->setTexture(I.tileId);
    a2->setLoadAction(MTL::LoadActionClear);
    a2->setClearColor(MTL::ClearColor(0, 0, 0, 0));
    a2->setStoreAction(MTL::StoreActionDontCare);
    auto* ad = rpd->depthAttachment();
    ad->setTexture(I.tileDepth);
    ad->setLoadAction(MTL::LoadActionClear);
    ad->setClearDepth(1.0);
    ad->setStoreAction(MTL::StoreActionDontCare);

    auto* re = cb->renderCommandEncoder(rpd);
    const auto& ui = f.ui->items();
    MTL::Buffer* uiB = I.uiBuf.upload(slot, ui.data(), ui.size() * sizeof(UiInstance));
    auto drawUi = [&](size_t from, size_t to) {
        if (to <= from) return;
        re->setRenderPipelineState(I.uiPipe);
        re->setVertexBuffer(uiB, 0, 0);
        re->setVertexBytes(&fu, sizeof fu, 1);
        re->setFragmentBuffer(uiB, 0, 0);
        re->setFragmentBytes(&fu, sizeof fu, 1);
        re->setFragmentTexture(m_atlas->texture(), 0);
        re->setFragmentTexture(m_atlas->texture(), 1);  // unused by non-image kinds
        re->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(4),
                           NS::UInteger(to - from), NS::UInteger(from));
    };
    drawUi(0, f.underEnd);
    if (threeD) m_3d->draw(re, slot, g->scene, g->area, fu, target->width(), target->height());

    if (g) {
        // Images sit on the graph paper beneath fills and curves (as in Desmos).
        for (const ImageDraw& im : g->images) {
            if (im.texture < 0 || im.texture >= int(I.images.size())) continue;
            UiInstance u{};
            u.rect = {im.rect.x, im.rect.y, im.rect.w, im.rect.h};
            u.color = {1, 1, 1, im.opacity};
            u.params = {0, 0, float(UI_KIND_IMAGE), 0};
            u.uv = {0, 0, 1, 1};
            u.clip = graphClip;
            re->setRenderPipelineState(I.uiPipe);
            re->setVertexBytes(&u, sizeof u, 0);
            re->setVertexBytes(&fu, sizeof fu, 1);
            re->setFragmentBytes(&u, sizeof u, 0);
            re->setFragmentBytes(&fu, sizeof fu, 1);
            re->setFragmentTexture(m_atlas->texture(), 0);
            re->setFragmentTexture(I.images[im.texture], 1);
            re->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(4));
        }
        // Inequality fills
        for (size_t fi = 0; fi < I.fillSlices.size(); ++fi) {
            int sliceIdx = I.fillSlices[fi];
            const ImplicitPiece& pc = I.pieces[sliceIdx];
            const CurveStyle& st = I.styles[pc.styleIndex];
            FillUniforms fl{};
            fl.color = {st.color.x, st.color.y, st.color.z, I.fillOpacities[fi]};
            fl.clip = graphClip;
            fl.slice = uint32_t(sliceIdx);
            fl.gridW = iu.gridW;
            fl.gridH = iu.gridH;
            fl.cellPts = iu.cellPts;
            fl.origin = iu.origin;
            re->setRenderPipelineState(I.fillPipe);
            re->setVertexBytes(&fl, sizeof fl, 0);
            re->setVertexBytes(&fu, sizeof fu, 1);
            re->setFragmentBytes(&fl, sizeof fl, 0);
            re->setFragmentBytes(&fu, sizeof fu, 1);
            re->setFragmentTexture(I.field, 0);
            re->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(4));
        }
        MTL::Buffer* styleB = I.styleBuf.upload(slot, I.styles.data(), I.styles.size() * sizeof(CurveStyle));
        if (I.verts.size() >= 2) {
            MTL::Buffer* vb = I.vertBuf.upload(slot, I.verts.data(), I.verts.size() * sizeof(CurveVertex));
            re->setRenderPipelineState(I.curvePipe);
            re->setVertexBuffer(vb, 0, 0);
            re->setVertexBuffer(styleB, 0, 1);
            re->setVertexBytes(&fu, sizeof fu, 2);
            re->setFragmentBuffer(styleB, 0, 1);
            re->setFragmentBytes(&fu, sizeof fu, 2);
            re->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(4),
                               NS::UInteger(I.verts.size() - 1));
        }
        if (haveImplicit) {
            re->setRenderPipelineState(I.implicitPipe);
            re->setVertexBuffer(I.segments, 0, 0);
            re->setVertexBuffer(styleB, 0, 1);
            re->setVertexBytes(&fu, sizeof fu, 2);
            re->setFragmentBuffer(styleB, 0, 1);
            re->setFragmentBytes(&fu, sizeof fu, 2);
            re->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, I.drawArgs, NS::UInteger(0));
        }
    }
    drawUi(f.underEnd, f.overEnd);
    size_t listEnd = std::max(f.overEnd, std::min(f.listEnd, ui.size()));
    drawUi(f.overEnd, listEnd);
    simd_float4 listClip = {f.listClip.x, f.listClip.y, f.listClip.right(), f.listClip.bottom()};
    for (const ImageDraw& im : f.listImages) {
        if (im.texture < 0 || im.texture >= int(I.images.size())) continue;
        UiInstance u{};
        u.rect = {im.rect.x, im.rect.y, im.rect.w, im.rect.h};
        u.color = {1, 1, 1, im.opacity};
        u.params = {0, 0, float(UI_KIND_IMAGE), 0};
        u.uv = {0, 0, 1, 1};
        u.clip = listClip;
        re->setRenderPipelineState(I.uiPipe);
        re->setVertexBytes(&u, sizeof u, 0);
        re->setVertexBytes(&fu, sizeof fu, 1);
        re->setFragmentBytes(&u, sizeof u, 0);
        re->setFragmentBytes(&fu, sizeof fu, 1);
        re->setFragmentTexture(m_atlas->texture(), 0);
        re->setFragmentTexture(I.images[im.texture], 1);
        re->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(4));
    }
    drawUi(listEnd, ui.size());
    re->endEncoding();

    if (drawable) cb->presentDrawable(drawable);
    Impl* impl = m.get();
    cb->addCompletedHandler([impl](MTL::CommandBuffer* done) {
        double ms = (done->GPUEndTime() - done->GPUStartTime()) * 1000.0;
        impl->gpuMs.store(ms);
        if (done->status() == MTL::CommandBufferStatusError) {
            // A GPU fault (timeout, page fault, invalid resource) would otherwise just drop the frame.
            static std::atomic<int> reported{0};
            if (reported.fetch_add(1) < 20) {
                NS::Error* e = done->error();
                log::error("GPU command buffer failed (code %ld): %s", e ? long(e->code()) : 0L,
                           e && e->localizedDescription() ? e->localizedDescription()->utf8String() : "unknown");
            }
        } else if (ms > 16.6) {
            log::info("slow GPU frame: %.2f ms", ms);
        }
        impl->ring.end();
    });
    cb->commit();
    if (I.lastCb) I.lastCb->release();
    I.lastCb = cb->retain();
    m_stats.cpuEncodeMs = nowMs() - t0;
    m_stats.curveVertices = I.verts.size();
    m_stats.uiInstances = ui.size();
    if (wait) cb->waitUntilCompleted();
    m_stats.gpuMs = I.gpuMs.load();
    pool->release();
}

int Renderer::loadImage(const std::string& path, float& aspect) {
    CFStringRef cfPath = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, cfPath, kCFURLPOSIXPathStyle, false);
    CFRelease(cfPath);
    CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (!src) return -1;
    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (!img) return -1;
    size_t w = CGImageGetWidth(img), h = CGImageGetHeight(img);
    // Downscale very large images: the graph never needs more than ~2k pixels.
    size_t maxDim = 2048, tw = w, th = h;
    if (std::max(w, h) > maxDim) {
        double k = double(maxDim) / double(std::max(w, h));
        tw = std::max<size_t>(1, size_t(w * k));
        th = std::max<size_t>(1, size_t(h * k));
    }
    std::vector<uint8_t> px(tw * th * 4);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(px.data(), tw, th, 8, tw * 4, cs,
                                             uint32_t(kCGImageAlphaPremultipliedLast) | uint32_t(kCGBitmapByteOrder32Big));
    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    CGContextDrawImage(ctx, CGRectMake(0, 0, tw, th), img);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    CGImageRelease(img);
    auto* d = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, tw, th, false);
    d->setStorageMode(MTL::StorageModeShared);
    d->setUsage(MTL::TextureUsageShaderRead);
    MTL::Texture* tex = m_device->newTexture(d);
    tex->replaceRegion(MTL::Region(0, 0, tw, th), 0, px.data(), tw * 4);
    aspect = float(h) / float(w);
    m->images.push_back(tex);
    return int(m->images.size()) - 1;
}

bool Renderer::savePng(MTL::Texture* tex, const std::string& path) {
    size_t w = tex->width(), h = tex->height();
    std::vector<uint8_t> px(w * h * 4);
    tex->getBytes(px.data(), w * 4, MTL::Region(0, 0, w, h), 0);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(px.data(), w, h, 8, w * 4, cs,
                                             uint32_t(kCGImageAlphaPremultipliedFirst) | uint32_t(kCGBitmapByteOrder32Little));
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    CFStringRef cfPath = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, cfPath, kCFURLPOSIXPathStyle, false);
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
    bool ok = false;
    if (dest) {
        CGImageDestinationAddImage(dest, img, nullptr);
        ok = CGImageDestinationFinalize(dest);
        CFRelease(dest);
    }
    CFRelease(url);
    CFRelease(cfPath);
    CGImageRelease(img);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    return ok;
}

} // namespace calc
