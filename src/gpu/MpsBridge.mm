#import <Metal/Metal.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>

#include "gpu/MpsBridge.hpp"

#include <cstring>

namespace calc::mps {

namespace {

struct Context {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
};

Context& ctx() {
    static Context c = [] {
        Context x;
        x.device = MTLCreateSystemDefaultDevice();
        if (x.device && MPSSupportsMTLDevice(x.device)) x.queue = [x.device newCommandQueue];
        return x;
    }();
    return c;
}

id<MTLBuffer> bufferWith(id<MTLDevice> dev, const void* data, size_t bytes) {
    return [dev newBufferWithBytes:data length:bytes options:MTLResourceStorageModeShared];
}

} // namespace

bool available() { return ctx().queue != nil; }

bool gram(const std::vector<float>& A, const std::vector<float>& b, int n, int k,
          std::vector<double>& G, std::vector<double>& h) {
    @autoreleasepool {
        Context& c = ctx();
        if (!c.queue || n <= 0 || k <= 0) return false;
        // Treat [A | b] as one n×(k+1) matrix M; then MᵀM holds AᵀA and Aᵀb in one product.
        int kc = k + 1;
        std::vector<float> M(size_t(n) * kc);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < k; ++j) M[size_t(i) * kc + j] = A[size_t(i) * k + j];
            M[size_t(i) * kc + k] = b[i];
        }
        NSUInteger rowBytes = [MPSMatrixDescriptor rowBytesForColumns:kc dataType:MPSDataTypeFloat32];
        std::vector<float> padded(size_t(n) * (rowBytes / sizeof(float)), 0.0f);
        for (int i = 0; i < n; ++i)
            std::memcpy(&padded[size_t(i) * (rowBytes / sizeof(float))], &M[size_t(i) * kc], kc * sizeof(float));
        id<MTLBuffer> inBuf = bufferWith(c.device, padded.data(), padded.size() * sizeof(float));
        NSUInteger outRowBytes = [MPSMatrixDescriptor rowBytesForColumns:kc dataType:MPSDataTypeFloat32];
        id<MTLBuffer> outBuf = [c.device newBufferWithLength:outRowBytes * kc options:MTLResourceStorageModeShared];

        MPSMatrixDescriptor* inDesc = [MPSMatrixDescriptor matrixDescriptorWithRows:n columns:kc
                                                                           rowBytes:rowBytes dataType:MPSDataTypeFloat32];
        MPSMatrixDescriptor* outDesc = [MPSMatrixDescriptor matrixDescriptorWithRows:kc columns:kc
                                                                            rowBytes:outRowBytes dataType:MPSDataTypeFloat32];
        MPSMatrix* m = [[MPSMatrix alloc] initWithBuffer:inBuf descriptor:inDesc];
        MPSMatrix* out = [[MPSMatrix alloc] initWithBuffer:outBuf descriptor:outDesc];
        MPSMatrixMultiplication* mul = [[MPSMatrixMultiplication alloc] initWithDevice:c.device
                                                                         transposeLeft:YES
                                                                        transposeRight:NO
                                                                            resultRows:kc
                                                                         resultColumns:kc
                                                                       interiorColumns:n
                                                                                 alpha:1.0
                                                                                  beta:0.0];
        id<MTLCommandBuffer> cb = [c.queue commandBuffer];
        [mul encodeToCommandBuffer:cb leftMatrix:m rightMatrix:m resultMatrix:out];
        [cb commit];
        [cb waitUntilCompleted];
        if (cb.status != MTLCommandBufferStatusCompleted) return false;

        const float* r = static_cast<const float*>(outBuf.contents);
        size_t stride = outRowBytes / sizeof(float);
        G.assign(size_t(k) * k, 0);
        h.assign(k, 0);
        for (int i = 0; i < k; ++i) {
            for (int j = 0; j < k; ++j) G[size_t(i) * k + j] = r[i * stride + j];
            h[i] = r[i * stride + k];
        }
        return true;
    }
}

} // namespace calc::mps
