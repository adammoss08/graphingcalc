#pragma once
// Attachment formats of the single render pass, shared by every pipeline.
#include <Metal/Metal.hpp>

namespace calc {

constexpr MTL::PixelFormat kColorFormat = MTL::PixelFormatBGRA8Unorm;
constexpr MTL::PixelFormat kBaseFormat = MTL::PixelFormatRGBA8Unorm;   // memoryless: curve "base" colour
constexpr MTL::PixelFormat kIdFormat = MTL::PixelFormatR32Uint;        // memoryless: curve id + coverage
constexpr MTL::PixelFormat kDepthFormat = MTL::PixelFormatDepth32Float; // memoryless: 3D mode only

inline void setAttachments(MTL::RenderPipelineDescriptor* d, bool writesTile, bool premultipliedBlend) {
    auto* c0 = d->colorAttachments()->object(0);
    c0->setPixelFormat(kColorFormat);
    if (premultipliedBlend) {
        c0->setBlendingEnabled(true);
        c0->setRgbBlendOperation(MTL::BlendOperationAdd);
        c0->setAlphaBlendOperation(MTL::BlendOperationAdd);
        c0->setSourceRGBBlendFactor(MTL::BlendFactorOne);
        c0->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
        c0->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        c0->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    }
    auto* c1 = d->colorAttachments()->object(1);
    c1->setPixelFormat(kBaseFormat);
    c1->setWriteMask(writesTile ? MTL::ColorWriteMaskAll : MTL::ColorWriteMaskNone);
    auto* c2 = d->colorAttachments()->object(2);
    c2->setPixelFormat(kIdFormat);
    c2->setWriteMask(writesTile ? MTL::ColorWriteMaskAll : MTL::ColorWriteMaskNone);
    d->setDepthAttachmentPixelFormat(kDepthFormat);
}

} // namespace calc
