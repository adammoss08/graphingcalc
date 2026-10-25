// STUB — see CLAUDE.md "Work in progress: 3D mode", step 1. Not yet wired into Renderer.
#include "render/Renderer3D.hpp"
#include "render/Renderer.hpp"

namespace calc {

Renderer3D::~Renderer3D() = default;

bool Renderer3D::init(MTL::Device* device, MTL::Library*, std::string&) {
    m_device = device;
    return true;
}

void Renderer3D::encodeCompute(MTL::CommandBuffer*, int, const Scene3D&) {}

void Renderer3D::draw(MTL::RenderCommandEncoder*, int, const Scene3D&, const Rect&, const FrameUniforms&,
                      NS::UInteger, NS::UInteger) {}

} // namespace calc
