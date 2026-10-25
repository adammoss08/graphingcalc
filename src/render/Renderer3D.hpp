#pragma once
// 3D mode rendering, owned by Renderer:
//  - grid surfaces (z = f(x, y), parametric (f, g, h)(u, v)): a compute pass evaluates each into a
//    gridN² positions block; drawn as indexed triangles;
//  - implicit surfaces F(x, y, z) = 0: field grid -> surface-nets mesh (vertices + opaque and
//    translucent index lists), drawn with indirect arguments;
//  - depth-tested lines/points.
// Geometry is rebuilt only when the programs or the box change, so orbiting just redraws. Mesh
// buffers start small and grow when a rebuild overflows them (the completion handler reports it
// and asks for another frame).
#include "render/RingBuffer.hpp"
#include "render/ShaderTypes.hpp"

#include <Metal/Metal.hpp>

#include <atomic>
#include <string>
#include <vector>

namespace calc {

struct Scene3D;
struct Rect;

class Renderer3D {
public:
    ~Renderer3D();
    bool init(MTL::Device* device, MTL::Library* lib, std::string& error);
    // Rebuilds grid surfaces / implicit meshes that changed (before the render pass).
    void encodeCompute(MTL::CommandBuffer* cb, int slot, const Scene3D& scene);
    // Draws opaque surfaces, lines, then translucent surfaces, scissored to the graph area. Leaves
    // depth testing off afterwards.
    void draw(MTL::RenderCommandEncoder* re, int slot, const Scene3D& scene, const Rect& area,
              const FrameUniforms& fu, NS::UInteger targetW, NS::UInteger targetH);
    // From the frame's completion handler (any thread). True if a mesh overflowed its buffers and
    // the next frame will rebuild it larger.
    bool completed(int slot);
    bool pendingRebuild() const { return m_needRebuild.load(); }
    void meshStats(uint32_t& vertices, uint32_t& triangles, bool& overflowed) const;
    void debugMesh(std::vector<float>& xyz, std::vector<uint32_t>& tris) const;

private:
    MTL::Device* m_device = nullptr;
    MTL::RenderPipelineState* m_surfacePipe = nullptr;
    MTL::RenderPipelineState* m_meshPipe = nullptr;         // translucent (blended)
    MTL::RenderPipelineState* m_meshOpaquePipe = nullptr;   // opaque: no blend/discard (HSR)
    MTL::RenderPipelineState* m_linePipe = nullptr;
    MTL::ComputePipelineState* m_gridPipe = nullptr;
    MTL::ComputePipelineState* m_fieldPipe = nullptr;
    MTL::ComputePipelineState* m_cellsPipe = nullptr;
    MTL::ComputePipelineState* m_quadsPipe = nullptr;
    MTL::ComputePipelineState* m_argsPipe = nullptr;
    MTL::DepthStencilState* m_depthSurface = nullptr;      // less, write
    MTL::DepthStencilState* m_depthTranslucent = nullptr;  // less, no write
    MTL::DepthStencilState* m_depthLines = nullptr;        // lessEqual, no write
    MTL::DepthStencilState* m_depthOff = nullptr;

    // Grid surfaces
    MTL::Buffer* m_positions = nullptr;   // private: gridN^2 float4 per surface
    MTL::Buffer* m_indices = nullptr;
    uint32_t m_indexCount = 0;
    uint32_t m_gridN = 0;
    uint32_t m_surfaceCount = 0;
    uint64_t m_gridKey = 0;
    std::vector<SurfaceProgram3D> m_progScratch;

    // Implicit meshes
    uint32_t m_fieldN = 0;
    MTL::Buffer* m_field = nullptr;       // private: n^3 floats (reused per surface)
    MTL::Buffer* m_cellIndex = nullptr;   // private: (n-1)^3 vertex indices
    MTL::Buffer* m_meshVerts = nullptr;   // shared (tests read it back)
    MTL::Buffer* m_meshIdx[2] = {};       // opaque, translucent index lists
    MTL::Buffer* m_meshArgs = nullptr;    // 2 x MTLDrawIndexedPrimitivesIndirectArguments
    MTL::Buffer* m_counters[kFramesInFlight] = {};
    uint32_t m_vertexCap = 0, m_indexCap[2] = {};
    uint64_t m_meshKey = 0;
    bool m_haveMesh = false;
    std::atomic<bool> m_slotRebuilt[kFramesInFlight] = {};
    std::atomic<bool> m_needRebuild{false};
    std::atomic<uint32_t> m_wantVertexCap{0}, m_wantIndexCap0{0}, m_wantIndexCap1{0};
    std::atomic<uint32_t> m_statVertices{0}, m_statTriangles{0}, m_statOpaqueIndices{0};
    std::atomic<bool> m_statOverflow{false};
    bool m_reportedLimit = false;

    RingBuffer m_code, m_consts, m_pieces, m_lines, m_meshStyles;
    RingBuffer m_meshCode, m_meshConsts;   // implicit programs (separate from the grid pass's)
    std::vector<uint32_t> m_meshCodeScratch;
    std::vector<float> m_meshConstScratch;
    std::vector<uint32_t> m_codeScratch;
    std::vector<float> m_constScratch;
    std::vector<simd_float4> m_styleScratch;

    void growMesh();
    void encodeImplicit(MTL::CommandBuffer* cb, int slot, const Scene3D& scene);
};

} // namespace calc
