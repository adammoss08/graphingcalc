#pragma once
// 3D mode rendering: GPU-evaluated surfaces (compute) and depth-tested lines/points, drawn inside the
// graph area of the main render pass. Owned by Renderer.
#include "render/RingBuffer.hpp"
#include "render/ShaderTypes.hpp"

#include <Metal/Metal.hpp>

#include <string>
#include <vector>

namespace calc {

struct Scene3D;
struct Rect;

class Renderer3D {
public:
    ~Renderer3D();
    bool init(MTL::Device* device, MTL::Library* lib, std::string& error);
    // Evaluates every surface into the positions buffer (before the render pass). Skipped while the
    // surfaces and box are unchanged (orbiting only moves the camera).
    void encodeCompute(MTL::CommandBuffer* cb, int slot, const Scene3D& scene);
    // Draws surfaces then lines, scissored to the graph area. Leaves depth testing off afterwards.
    void draw(MTL::RenderCommandEncoder* re, int slot, const Scene3D& scene, const Rect& area,
              const FrameUniforms& fu, NS::UInteger targetW, NS::UInteger targetH);

private:
    MTL::Device* m_device = nullptr;
    MTL::RenderPipelineState* m_surfacePipe = nullptr;
    MTL::RenderPipelineState* m_linePipe = nullptr;
    MTL::ComputePipelineState* m_gridPipe = nullptr;
    MTL::DepthStencilState* m_depthSurface = nullptr;
    MTL::DepthStencilState* m_depthLines = nullptr;
    MTL::DepthStencilState* m_depthOff = nullptr;
    MTL::Buffer* m_positions = nullptr;   // private: gridN^2 float4 per surface
    MTL::Buffer* m_indices = nullptr;
    uint32_t m_indexCount = 0;
    uint32_t m_gridN = 0;
    uint32_t m_surfaceCount = 0;
    uint64_t m_gridKey = 0;               // programs + L the positions buffer holds
    RingBuffer m_code, m_consts, m_pieces, m_lines;
    std::vector<uint32_t> m_codeScratch;
    std::vector<float> m_constScratch;
    std::vector<ImplicitPiece> m_pieceScratch;
};

} // namespace calc
