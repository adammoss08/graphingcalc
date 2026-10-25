#include "render/Renderer3D.hpp"
#include "render/RenderFormats.hpp"
#include "render/Renderer.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace calc {

namespace {

NS::String* str(const char* s) { return NS::String::string(s, NS::UTF8StringEncoding); }

// World-space direction towards the light: above and slightly behind the camera, so the side of a
// surface facing the viewer is lit and the shading follows the camera as it orbits.
simd_float3 lightDirection(simd_float3 eye) {
    simd_float3 e = simd_length(eye) > 0 ? simd_normalize(eye) : simd_make_float3(0, 0, 1);
    return simd_normalize(e + simd_make_float3(0, 0, 1.4f));
}

Scene3DUniforms sceneUniforms(const Scene3D& scene, const Rect& area, const FrameUniforms& fu, uint32_t gridN) {
    Scene3DUniforms S{};
    S.viewProj = scene.viewProj;
    float vw = fu.viewSize.x, vh = fu.viewSize.y;
    S.clipRemap = {area.w / vw, area.h / vh, (2 * area.x + area.w) / vw - 1, 1 - (2 * area.y + area.h) / vh};
    S.eye = {scene.eye.x, scene.eye.y, scene.eye.z, scene.halfSize};
    simd_float3 l = lightDirection(scene.eye);
    S.light = {l.x, l.y, l.z, scene.gridStep};
    S.gridN = gridN;
    return S;
}

} // namespace

Renderer3D::~Renderer3D() {
    if (m_surfacePipe) m_surfacePipe->release();
    if (m_linePipe) m_linePipe->release();
    if (m_gridPipe) m_gridPipe->release();
    if (m_depthSurface) m_depthSurface->release();
    if (m_depthLines) m_depthLines->release();
    if (m_depthOff) m_depthOff->release();
    if (m_positions) m_positions->release();
    if (m_indices) m_indices->release();
}

bool Renderer3D::init(MTL::Device* device, MTL::Library* lib, std::string& error) {
    m_device = device;
    NS::Error* err = nullptr;
    auto makeRender = [&](const char* vs, const char* fs) -> MTL::RenderPipelineState* {
        auto* d = MTL::RenderPipelineDescriptor::alloc()->init();
        auto* v = lib->newFunction(str(vs));
        auto* f = lib->newFunction(str(fs));
        d->setVertexFunction(v);
        d->setFragmentFunction(f);
        setAttachments(d, false, true);
        auto* p = v && f ? device->newRenderPipelineState(d, &err) : nullptr;
        if (!p) error = std::string(vs) + ": " + (err ? err->localizedDescription()->utf8String() : "missing function");
        if (v) v->release();
        if (f) f->release();
        d->release();
        return p;
    };
    m_surfacePipe = makeRender("surfaceVertex", "surfaceFragment");
    m_linePipe = makeRender("line3dVertex", "line3dFragment");
    auto* gf = lib->newFunction(str("surfaceGrid"));
    m_gridPipe = gf ? device->newComputePipelineState(gf, &err) : nullptr;
    if (gf) gf->release();
    if (!m_gridPipe) error = std::string("surfaceGrid: ") + (err ? err->localizedDescription()->utf8String() : "missing function");
    if (!m_surfacePipe || !m_linePipe || !m_gridPipe) return false;

    auto makeDepth = [&](MTL::CompareFunction cmp, bool write) {
        auto* d = MTL::DepthStencilDescriptor::alloc()->init();
        d->setDepthCompareFunction(cmp);
        d->setDepthWriteEnabled(write);
        auto* s = device->newDepthStencilState(d);
        d->release();
        return s;
    };
    m_depthSurface = makeDepth(MTL::CompareFunctionLess, true);
    m_depthLines = makeDepth(MTL::CompareFunctionLessEqual, false);
    m_depthOff = makeDepth(MTL::CompareFunctionAlways, false);

    // Static index buffer: two triangles per grid cell, indices local to one surface (baseVertex
    // selects the surface).
    m_gridN = theme::Surface3DGridN;
    uint32_t n = m_gridN;
    std::vector<uint32_t> idx;
    idx.reserve(size_t(n - 1) * (n - 1) * 6);
    for (uint32_t j = 0; j + 1 < n; ++j)
        for (uint32_t i = 0; i + 1 < n; ++i) {
            uint32_t a = j * n + i, b = a + 1, c = a + n, d = c + 1;
            idx.insert(idx.end(), {a, b, d, a, d, c});
        }
    m_indexCount = uint32_t(idx.size());
    m_indices = device->newBuffer(idx.data(), idx.size() * 4, MTL::ResourceStorageModeShared);
    m_indices->setLabel(str("surface indices"));

    m_code.init(device, "surface code");
    m_consts.init(device, "surface consts");
    m_pieces.init(device, "surface pieces");
    m_lines.init(device, "3d lines");
    return true;
}

void Renderer3D::encodeCompute(MTL::CommandBuffer* cb, int slot, const Scene3D& scene) {
    m_surfaceCount = uint32_t(std::min<size_t>(scene.surfaces.size(), size_t(theme::Max3DSurfaces)));
    if (m_surfaceCount == 0) return;
    uint64_t key = 0xcbf29ce484222325ull;
    for (uint32_t s = 0; s < m_surfaceCount; ++s) key = (key ^ scene.surfaces[s].program->hash()) * 0x100000001b3ull;
    uint32_t bits;
    std::memcpy(&bits, &scene.halfSize, 4);
    key = (key ^ bits) * 0x100000001b3ull;
    key = (key ^ m_surfaceCount) * 0x100000001b3ull;
    if (m_positions && key == m_gridKey) return;
    m_gridKey = key;
    uint32_t n = m_gridN;
    size_t need = size_t(m_surfaceCount) * n * n * sizeof(simd_float4);
    if (!m_positions || m_positions->length() < need) {
        // Command buffers in flight retain the old buffer, so releasing it here is safe.
        if (m_positions) m_positions->release();
        m_positions = m_device->newBuffer(need, MTL::ResourceStorageModePrivate);
        m_positions->setLabel(str("surface positions"));
    }
    m_codeScratch.clear();
    m_constScratch.clear();
    m_pieceScratch.clear();
    for (uint32_t s = 0; s < m_surfaceCount; ++s) {
        const Program& p = *scene.surfaces[s].program;
        ImplicitPiece pc{};
        pc.codeOffset = uint32_t(m_codeScratch.size());
        pc.codeLength = uint32_t(p.code.size());
        pc.constOffset = uint32_t(m_constScratch.size());
        pc.styleIndex = s;
        m_codeScratch.insert(m_codeScratch.end(), p.code.begin(), p.code.end());
        for (double v : p.consts) m_constScratch.push_back(float(v));
        m_pieceScratch.push_back(pc);
    }
    if (m_constScratch.empty()) m_constScratch.push_back(0);
    SurfaceGridUniforms gu{};
    gu.halfSize = scene.halfSize;
    gu.gridN = n;
    gu.count = m_surfaceCount;
    auto* ce = cb->computeCommandEncoder();
    ce->setLabel(str("surface grid"));
    ce->setComputePipelineState(m_gridPipe);
    ce->setBuffer(m_code.upload(slot, m_codeScratch.data(), m_codeScratch.size() * 4), 0, 0);
    ce->setBuffer(m_consts.upload(slot, m_constScratch.data(), m_constScratch.size() * 4), 0, 1);
    ce->setBuffer(m_pieces.upload(slot, m_pieceScratch.data(), m_pieceScratch.size() * sizeof(ImplicitPiece)), 0, 2);
    ce->setBytes(&gu, sizeof gu, 3);
    ce->setBuffer(m_positions, 0, 4);
    ce->dispatchThreads(MTL::Size(n, n, m_surfaceCount), MTL::Size(16, 16, 1));
    ce->endEncoding();
}

void Renderer3D::draw(MTL::RenderCommandEncoder* re, int slot, const Scene3D& scene, const Rect& area,
                      const FrameUniforms& fu, NS::UInteger targetW, NS::UInteger targetH) {
    float s = fu.scale;
    NS::UInteger x0 = NS::UInteger(std::clamp(area.x * s, 0.0f, float(targetW)));
    NS::UInteger y0 = NS::UInteger(std::clamp(area.y * s, 0.0f, float(targetH)));
    NS::UInteger x1 = NS::UInteger(std::clamp(area.right() * s, 0.0f, float(targetW)));
    NS::UInteger y1 = NS::UInteger(std::clamp(area.bottom() * s, 0.0f, float(targetH)));
    if (x1 <= x0 || y1 <= y0) return;
    re->setScissorRect(MTL::ScissorRect{x0, y0, x1 - x0, y1 - y0});
    Scene3DUniforms S = sceneUniforms(scene, area, fu, m_gridN);

    if (m_surfaceCount > 0 && m_positions) {
        re->setRenderPipelineState(m_surfacePipe);
        re->setDepthStencilState(m_depthSurface);
        re->setCullMode(MTL::CullModeNone);
        re->setVertexBuffer(m_positions, 0, 0);
        re->setVertexBytes(&S, sizeof S, 1);
        re->setFragmentBytes(&S, sizeof S, 1);
        uint32_t nn = m_gridN * m_gridN;
        for (uint32_t i = 0; i < m_surfaceCount; ++i) {
            const SurfaceDraw3D& sd = scene.surfaces[i];
            SurfaceStyle st{};
            st.color = {sd.color.r, sd.color.g, sd.color.b, sd.opacity};
            st.base = i * nn;
            re->setVertexBytes(&st, sizeof st, 2);
            re->setFragmentBytes(&st, sizeof st, 2);
            re->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(m_indexCount), MTL::IndexTypeUInt32,
                                      m_indices, 0, 1, NS::Integer(st.base), 0);
        }
    }
    if (!scene.lines.empty()) {
        MTL::Buffer* lb = m_lines.upload(slot, scene.lines.data(), scene.lines.size() * sizeof(Line3D));
        re->setRenderPipelineState(m_linePipe);
        re->setDepthStencilState(m_depthLines);
        re->setVertexBuffer(lb, 0, 0);
        re->setVertexBytes(&S, sizeof S, 1);
        re->setVertexBytes(&fu, sizeof fu, 2);
        re->setFragmentBytes(&fu, sizeof fu, 2);
        re->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(4),
                           NS::UInteger(scene.lines.size()));
    }
    re->setDepthStencilState(m_depthOff);
    re->setScissorRect(MTL::ScissorRect{0, 0, targetW, targetH});
}

} // namespace calc
