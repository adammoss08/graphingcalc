#include "render/Renderer3D.hpp"
#include "Log.hpp"
#include "render/RenderFormats.hpp"
#include "render/Renderer.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace calc {

namespace {

NS::String* str(const char* s) { return NS::String::string(s, NS::UTF8StringEncoding); }

constexpr uint32_t kMinVertexCap = 1u << 18;      // 256k vertices (8 MB)
constexpr uint32_t kMinIndexCap = 6u << 18;       // 1.5M indices (6 MB)
constexpr uint32_t kMaxVertexCap = 6u << 20;      // 6M vertices (192 MB): beyond this, truncate
constexpr uint32_t kMaxIndexCap = 36u << 20;      // 36M indices (144 MB) per list
constexpr size_t kArgsStride = 5 * sizeof(uint32_t);

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
    S.paramLines = uint32_t(theme::ParamSurfaceLines);
    return S;
}

void mixKey(uint64_t& k, uint64_t v) { k = (k ^ v) * 0x100000001b3ull; }
void mixKeyF(uint64_t& k, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    mixKey(k, u);
}

uint32_t grown(uint32_t need, uint32_t cap, uint32_t maxCap) {
    if (need <= cap) return cap;
    uint64_t c = cap;
    while (c < need + need / 4) c *= 2;
    return uint32_t(std::min<uint64_t>(c, maxCap));
}

// Appends a program's code/consts; returns {code offset, length, const offset}.
simd_uint3 packProgram(const Program& p, std::vector<uint32_t>& code, std::vector<float>& consts) {
    simd_uint3 r = {uint32_t(code.size()), uint32_t(p.code.size()), uint32_t(consts.size())};
    code.insert(code.end(), p.code.begin(), p.code.end());
    for (double v : p.consts) consts.push_back(float(v));
    return r;
}

} // namespace

Renderer3D::~Renderer3D() {
    for (auto* o : {(NS::Object*)m_surfacePipe, (NS::Object*)m_meshPipe, (NS::Object*)m_meshOpaquePipe, (NS::Object*)m_linePipe,
                    (NS::Object*)m_gridPipe, (NS::Object*)m_fieldPipe, (NS::Object*)m_cellsPipe,
                    (NS::Object*)m_quadsPipe, (NS::Object*)m_argsPipe, (NS::Object*)m_depthSurface,
                    (NS::Object*)m_depthTranslucent, (NS::Object*)m_depthLines, (NS::Object*)m_depthOff,
                    (NS::Object*)m_positions, (NS::Object*)m_indices, (NS::Object*)m_field,
                    (NS::Object*)m_cellIndex, (NS::Object*)m_meshVerts, (NS::Object*)m_meshIdx[0],
                    (NS::Object*)m_meshIdx[1], (NS::Object*)m_meshArgs})
        if (o) o->release();
    for (auto* c : m_counters) if (c) c->release();
}

bool Renderer3D::init(MTL::Device* device, MTL::Library* lib, std::string& error) {
    m_device = device;
    NS::Error* err = nullptr;
    auto makeRender = [&](const char* vs, const char* fs, bool blend = true) -> MTL::RenderPipelineState* {
        auto* d = MTL::RenderPipelineDescriptor::alloc()->init();
        auto* v = lib->newFunction(str(vs));
        auto* f = lib->newFunction(str(fs));
        d->setVertexFunction(v);
        d->setFragmentFunction(f);
        setAttachments(d, false, blend);
        auto* p = v && f ? device->newRenderPipelineState(d, &err) : nullptr;
        if (!p) error = std::string(vs) + ": " + (err ? err->localizedDescription()->utf8String() : "missing function");
        if (v) v->release();
        if (f) f->release();
        d->release();
        return p;
    };
    auto makeCompute = [&](const char* name) -> MTL::ComputePipelineState* {
        auto* f = lib->newFunction(str(name));
        auto* p = f ? device->newComputePipelineState(f, &err) : nullptr;
        if (f) f->release();
        if (!p) error = std::string(name) + ": " + (err ? err->localizedDescription()->utf8String() : "missing function");
        return p;
    };
    m_surfacePipe = makeRender("surfaceVertex", "surfaceFragment");
    m_meshPipe = makeRender("meshVertex", "meshFragment");
    m_meshOpaquePipe = makeRender("meshVertex", "meshFragmentOpaque", false);
    m_linePipe = makeRender("line3dVertex", "line3dFragment");
    m_gridPipe = makeCompute("surfaceGrid");
    m_fieldPipe = makeCompute("implicit3dField");
    m_cellsPipe = makeCompute("implicit3dCells");
    m_quadsPipe = makeCompute("implicit3dQuads");
    m_argsPipe = makeCompute("implicit3dArgs");
    if (!m_surfacePipe || !m_meshPipe || !m_meshOpaquePipe || !m_linePipe || !m_gridPipe || !m_fieldPipe || !m_cellsPipe ||
        !m_quadsPipe || !m_argsPipe)
        return false;

    auto makeDepth = [&](MTL::CompareFunction cmp, bool write) {
        auto* d = MTL::DepthStencilDescriptor::alloc()->init();
        d->setDepthCompareFunction(cmp);
        d->setDepthWriteEnabled(write);
        auto* s = device->newDepthStencilState(d);
        d->release();
        return s;
    };
    m_depthSurface = makeDepth(MTL::CompareFunctionLess, true);
    m_depthTranslucent = makeDepth(MTL::CompareFunctionLess, false);
    m_depthLines = makeDepth(MTL::CompareFunctionLessEqual, false);
    m_depthOff = makeDepth(MTL::CompareFunctionAlways, false);

    // Static index buffer for grid surfaces: two triangles per cell, indices local to one surface
    // (baseVertex selects the surface).
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

    // Implicit meshes: field/cell grids sized for Implicit3DGridN, mesh buffers start small.
    m_fieldN = theme::Implicit3DGridN;
    size_t n3 = size_t(m_fieldN) * m_fieldN * m_fieldN, m3 = size_t(m_fieldN - 1) * (m_fieldN - 1) * (m_fieldN - 1);
    m_field = device->newBuffer(n3 * sizeof(float), MTL::ResourceStorageModePrivate);
    m_field->setLabel(str("implicit3d field"));
    m_cellIndex = device->newBuffer(m3 * sizeof(uint32_t), MTL::ResourceStorageModePrivate);
    m_cellIndex->setLabel(str("implicit3d cells"));
    m_meshArgs = device->newBuffer(2 * kArgsStride, MTL::ResourceStorageModePrivate);
    for (auto& c : m_counters) c = device->newBuffer(16, MTL::ResourceStorageModeShared);
    m_wantVertexCap = kMinVertexCap;
    m_wantIndexCap0 = kMinIndexCap;
    m_wantIndexCap1 = kMinIndexCap / 6;
    growMesh();

    m_code.init(device, "surface code");
    m_consts.init(device, "surface consts");
    m_pieces.init(device, "surface programs");
    m_lines.init(device, "3d lines");
    m_meshStyles.init(device, "mesh styles");
    m_meshCode.init(device, "implicit3d code");
    m_meshConsts.init(device, "implicit3d consts");
    return true;
}

void Renderer3D::growMesh() {
    auto realloc = [&](MTL::Buffer*& b, uint32_t& cap, uint32_t want, size_t elem, const char* label) {
        if (b && cap >= want) return;
        if (b) b->release();   // frames in flight retain the old buffer
        cap = want;
        b = m_device->newBuffer(size_t(cap) * elem, MTL::ResourceStorageModeShared);
        b->setLabel(str(label));
    };
    realloc(m_meshVerts, m_vertexCap, m_wantVertexCap.load(), sizeof(MeshVertex3D), "implicit3d vertices");
    realloc(m_meshIdx[0], m_indexCap[0], m_wantIndexCap0.load(), sizeof(uint32_t), "implicit3d opaque indices");
    realloc(m_meshIdx[1], m_indexCap[1], m_wantIndexCap1.load(), sizeof(uint32_t), "implicit3d translucent indices");
}

void Renderer3D::encodeCompute(MTL::CommandBuffer* cb, int slot, const Scene3D& scene) {
    encodeImplicit(cb, slot, scene);

    m_surfaceCount = uint32_t(std::min<size_t>(scene.surfaces.size(), size_t(theme::Max3DSurfaces)));
    if (m_surfaceCount == 0) return;
    uint64_t key = 0xcbf29ce484222325ull;
    for (uint32_t s = 0; s < m_surfaceCount; ++s) {
        const SurfaceDraw3D& sd = scene.surfaces[s];
        mixKey(key, sd.program->hash());
        mixKey(key, sd.g ? sd.g->hash() : 1);
        mixKey(key, sd.h ? sd.h->hash() : 2);
        for (float f : {sd.u0, sd.u1, sd.v0, sd.v1}) mixKeyF(key, f);
    }
    mixKeyF(key, scene.halfSize);
    mixKey(key, m_surfaceCount);
    if (m_positions && key == m_gridKey) return;
    m_gridKey = key;
    uint32_t n = m_gridN;
    size_t need = size_t(m_surfaceCount) * n * n * sizeof(simd_float4);
    if (!m_positions || m_positions->length() < need) {
        if (m_positions) m_positions->release();   // command buffers in flight retain the old one
        m_positions = m_device->newBuffer(need, MTL::ResourceStorageModePrivate);
        m_positions->setLabel(str("surface positions"));
    }
    m_codeScratch.clear();
    m_constScratch.clear();
    m_progScratch.clear();
    for (uint32_t s = 0; s < m_surfaceCount; ++s) {
        const SurfaceDraw3D& sd = scene.surfaces[s];
        SurfaceProgram3D sp{};
        bool param = sd.g && sd.h;
        const Program* ps[3] = {sd.program, param ? sd.g : sd.program, param ? sd.h : sd.program};
        simd_uint3 r[3];
        for (int k = 0; k < 3; ++k) r[k] = packProgram(*ps[k], m_codeScratch, m_constScratch);
        sp.code = {r[0].x, r[1].x, r[2].x, param ? uint32_t(SURFACE_PARAMETRIC) : uint32_t(SURFACE_EXPLICIT)};
        sp.length = {r[0].y, r[1].y, r[2].y, 0};
        sp.consts = {r[0].z, r[1].z, r[2].z, 0};
        sp.domain = {sd.u0, sd.u1, sd.v0, sd.v1};
        m_progScratch.push_back(sp);
    }
    if (m_constScratch.empty()) m_constScratch.push_back(0);
    if (m_codeScratch.empty()) m_codeScratch.push_back(0);
    SurfaceGridUniforms gu{};
    gu.halfSize = scene.halfSize;
    gu.gridN = n;
    gu.count = m_surfaceCount;
    auto* ce = cb->computeCommandEncoder();
    ce->setLabel(str("surface grid"));
    ce->setComputePipelineState(m_gridPipe);
    ce->setBuffer(m_code.upload(slot, m_codeScratch.data(), m_codeScratch.size() * 4), 0, 0);
    ce->setBuffer(m_consts.upload(slot, m_constScratch.data(), m_constScratch.size() * 4), 0, 1);
    ce->setBuffer(m_pieces.upload(slot, m_progScratch.data(), m_progScratch.size() * sizeof(SurfaceProgram3D)), 0, 2);
    ce->setBytes(&gu, sizeof gu, 3);
    ce->setBuffer(m_positions, 0, 4);
    ce->dispatchThreads(MTL::Size(n, n, m_surfaceCount), MTL::Size(16, 16, 1));
    ce->endEncoding();
}

void Renderer3D::encodeImplicit(MTL::CommandBuffer* cb, int slot, const Scene3D& scene) {
    size_t count = std::min<size_t>(scene.implicits.size(), size_t(theme::Max3DSurfaces));
    if (count == 0) {
        m_haveMesh = false;
        m_meshKey = 0;
        return;
    }
    uint32_t n = m_fieldN;
    uint64_t key = 0x84222325cbf29ce4ull;
    for (size_t i = 0; i < count; ++i) {
        const ImplicitDraw3D& d = scene.implicits[i];
        mixKey(key, d.program->hash());
        mixKey(key, d.opacity < 1 ? 1 : 0);
        for (int k = 0; k < 3; ++k) { mixKeyF(key, d.boxMin[k]); mixKeyF(key, d.boxMax[k]); }
    }
    mixKey(key, count);
    bool overflowRetry = m_needRebuild.exchange(false);
    if (overflowRetry) growMesh();
    if (m_haveMesh && key == m_meshKey && !overflowRetry) return;
    m_meshKey = key;
    m_haveMesh = true;

    // Code and consts for every implicit go in one upload; each dispatch points at its slice.
    auto& code = m_meshCodeScratch;
    auto& consts = m_meshConstScratch;
    code.clear();
    consts.clear();
    std::vector<simd_uint3> where;
    for (size_t i = 0; i < count; ++i) where.push_back(packProgram(*scene.implicits[i].program, code, consts));
    if (code.empty()) code.push_back(0);
    if (consts.empty()) consts.push_back(0);

    auto* blit = cb->blitCommandEncoder();
    blit->fillBuffer(m_counters[slot], NS::Range(0, 16), 0);
    blit->endEncoding();

    MTL::Buffer* codeBuf = m_meshCode.upload(slot, code.data(), code.size() * 4);
    MTL::Buffer* constBuf = m_meshConsts.upload(slot, consts.data(), consts.size() * 4);

    auto* ce = cb->computeCommandEncoder();
    ce->setLabel(str("implicit3d mesh"));
    ce->setBuffer(codeBuf, 0, 0);
    ce->setBuffer(constBuf, 0, 1);
    ce->setBuffer(m_field, 0, 3);
    ce->setBuffer(m_cellIndex, 0, 4);
    ce->setBuffer(m_counters[slot], 0, 6);
    for (size_t i = 0; i < count; ++i) {
        const ImplicitDraw3D& d = scene.implicits[i];
        Implicit3DUniforms U{};
        U.boxMin = {d.boxMin.x, d.boxMin.y, d.boxMin.z, 0};
        U.boxMax = {d.boxMax.x, d.boxMax.y, d.boxMax.z, 0};
        U.n = n;
        U.surface = uint32_t(i);
        U.codeOffset = where[i].x;
        U.codeLength = where[i].y;
        U.constOffset = where[i].z;
        U.translucent = d.opacity < 1 ? 1 : 0;
        U.vertexCapacity = m_vertexCap;
        U.indexCapacity = m_indexCap[U.translucent];
        ce->setBytes(&U, sizeof U, 2);
        ce->setComputePipelineState(m_fieldPipe);
        ce->dispatchThreads(MTL::Size(n, n, n), MTL::Size(8, 8, 4));
        ce->memoryBarrier(MTL::BarrierScopeBuffers);
        ce->setComputePipelineState(m_cellsPipe);
        ce->setBuffer(m_meshVerts, 0, 5);
        ce->dispatchThreads(MTL::Size(n - 1, n - 1, n - 1), MTL::Size(8, 8, 4));
        ce->memoryBarrier(MTL::BarrierScopeBuffers);
        ce->setComputePipelineState(m_quadsPipe);
        ce->setBuffer(m_meshIdx[U.translucent], 0, 5);
        ce->dispatchThreads(MTL::Size(n, n, n), MTL::Size(8, 8, 4));
        ce->memoryBarrier(MTL::BarrierScopeBuffers);
    }
    simd_uint2 caps = {m_indexCap[0], m_indexCap[1]};
    ce->setComputePipelineState(m_argsPipe);
    ce->setBuffer(m_counters[slot], 0, 0);
    ce->setBuffer(m_meshArgs, 0, 1);
    ce->setBytes(&caps, sizeof caps, 2);
    ce->dispatchThreads(MTL::Size(1, 1, 1), MTL::Size(1, 1, 1));
    ce->endEncoding();
    m_slotRebuilt[slot] = true;
}

bool Renderer3D::completed(int slot) {
    if (!m_slotRebuilt[slot].exchange(false)) return false;
    const uint32_t* c = static_cast<const uint32_t*>(m_counters[slot]->contents());
    uint32_t v = c[MESH_COUNT_VERTICES], io = c[MESH_COUNT_OPAQUE], it = c[MESH_COUNT_TRANSLUCENT];
    uint32_t vcap = m_vertexCap, ocap = m_indexCap[0], tcap = m_indexCap[1];
    bool overflow = v > vcap || io > ocap || it > tcap;
    m_statVertices = std::min(v, vcap);
    m_statTriangles = (std::min(io, ocap / 6 * 6) + std::min(it, tcap / 6 * 6)) / 3;
    m_statOpaqueIndices = std::min(io, ocap / 6 * 6);
    m_statOverflow = overflow;
    if (!overflow) return false;
    // When vertices overflowed, the index counts are short too (cells without a vertex emit no
    // quads): size the lists for ~8 indices per vertex (surface nets: ~2 quads/vertex) in one step.
    if (v > vcap) {
        uint64_t est = uint64_t(v) * 8;
        uint32_t share = io + it > 0 ? uint32_t(est * io / (io + it)) : uint32_t(est);
        io = std::max(io, std::min<uint32_t>(share, kMaxIndexCap));
        if (it > 0) it = std::max(it, std::min<uint32_t>(uint32_t(est - share), kMaxIndexCap));
    }
    uint32_t nv = grown(v, vcap, kMaxVertexCap), no = grown(io, ocap, kMaxIndexCap), nt = grown(it, tcap, kMaxIndexCap);
    if (nv == vcap && no == ocap && nt == tcap) {
        if (!m_reportedLimit) {
            m_reportedLimit = true;
            log::error("3D implicit mesh exceeds the buffer limit (%u vertices, %u indices): drawn truncated", v, io + it);
        }
        return false;
    }
    log::info("3D implicit mesh grew: %u vertices, %u + %u indices", v, io, it);
    m_wantVertexCap = nv;
    m_wantIndexCap0 = no;
    m_wantIndexCap1 = nt;
    m_needRebuild = true;
    return true;
}

void Renderer3D::meshStats(uint32_t& vertices, uint32_t& triangles, bool& overflowed) const {
    vertices = m_statVertices;
    triangles = m_statTriangles;
    overflowed = m_statOverflow;
}

void Renderer3D::debugMesh(std::vector<float>& xyz, std::vector<uint32_t>& tris) const {
    xyz.clear();
    tris.clear();
    if (!m_haveMesh) return;
    const auto* v = static_cast<const MeshVertex3D*>(m_meshVerts->contents());
    for (uint32_t i = 0; i < m_statVertices; ++i) xyz.insert(xyz.end(), {v[i].pos.x, v[i].pos.y, v[i].pos.z});
    // Opaque list only (tests use opaque surfaces), as counted by the last completed rebuild.
    const auto* idx = static_cast<const uint32_t*>(m_meshIdx[0]->contents());
    tris.assign(idx, idx + m_statOpaqueIndices.load());
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
    re->setCullMode(MTL::CullModeNone);
    Scene3DUniforms S = sceneUniforms(scene, area, fu, m_gridN);

    auto drawGrid = [&](bool translucent) {
        if (m_surfaceCount == 0 || !m_positions) return;
        bool any = false;
        uint32_t nn = m_gridN * m_gridN;
        for (uint32_t i = 0; i < m_surfaceCount; ++i) {
            const SurfaceDraw3D& sd = scene.surfaces[i];
            if ((sd.opacity < 1) != translucent) continue;
            if (!any) {
                re->setRenderPipelineState(m_surfacePipe);
                re->setDepthStencilState(translucent ? m_depthTranslucent : m_depthSurface);
                re->setVertexBuffer(m_positions, 0, 0);
                re->setVertexBytes(&S, sizeof S, 1);
                re->setFragmentBytes(&S, sizeof S, 1);
                any = true;
            }
            SurfaceStyle st{};
            st.color = {sd.color.r, sd.color.g, sd.color.b, sd.opacity};
            st.base = i * nn;
            st.kind = sd.g && sd.h ? SURFACE_PARAMETRIC : SURFACE_EXPLICIT;
            re->setVertexBytes(&st, sizeof st, 2);
            re->setFragmentBytes(&st, sizeof st, 2);
            re->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(m_indexCount), MTL::IndexTypeUInt32,
                                      m_indices, 0, 1, NS::Integer(st.base), 0);
        }
    };
    MTL::Buffer* styleB = nullptr;
    auto drawMesh = [&](bool translucent) {
        if (!m_haveMesh || scene.implicits.empty()) return;
        bool want = false;
        for (const auto& d : scene.implicits) want |= (d.opacity < 1) == translucent;
        if (!want) return;
        if (!styleB) {
            m_styleScratch.clear();
            for (const auto& d : scene.implicits) m_styleScratch.push_back(simd_make_float4(d.color.r, d.color.g, d.color.b, d.opacity));
            styleB = m_meshStyles.upload(slot, m_styleScratch.data(), m_styleScratch.size() * sizeof(simd_float4));
        }
        re->setRenderPipelineState(translucent ? m_meshPipe : m_meshOpaquePipe);
        re->setDepthStencilState(translucent ? m_depthTranslucent : m_depthSurface);
        re->setVertexBuffer(m_meshVerts, 0, 0);
        re->setVertexBytes(&S, sizeof S, 1);
        re->setVertexBuffer(styleB, 0, 2);
        re->setFragmentBytes(&S, sizeof S, 1);
        re->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, MTL::IndexTypeUInt32, m_meshIdx[translucent ? 1 : 0], 0,
                                  m_meshArgs, translucent ? kArgsStride : 0);
    };

    drawGrid(false);
    drawMesh(false);
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
    // Translucent surfaces (inequalities) last, without depth writes, so what's behind shows.
    drawGrid(true);
    drawMesh(true);
    re->setDepthStencilState(m_depthOff);
    re->setScissorRect(MTL::ScissorRect{0, 0, targetW, targetH});
}

} // namespace calc
