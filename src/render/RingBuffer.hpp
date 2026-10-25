#pragma once
// Triple-buffered per-frame data in StorageModeShared buffers (unified memory: the CPU writes, the GPU
// reads, no blits). FrameRing gates the CPU to at most kFramesInFlight frames ahead.
#include <Metal/Metal.hpp>
#include <dispatch/dispatch.h>

#include <cstring>

namespace calc {

constexpr int kFramesInFlight = 3;

class FrameRing {
public:
    FrameRing() : m_sem(dispatch_semaphore_create(kFramesInFlight)) {}
    ~FrameRing() {
        for (int i = 0; i < kFramesInFlight; ++i) dispatch_semaphore_wait(m_sem, DISPATCH_TIME_FOREVER);
        for (int i = 0; i < kFramesInFlight; ++i) dispatch_semaphore_signal(m_sem);
        dispatch_release(m_sem);
    }
    // Blocks only if the GPU is kFramesInFlight frames behind. Returns the slot to use.
    int begin() {
        dispatch_semaphore_wait(m_sem, DISPATCH_TIME_FOREVER);
        m_slot = (m_slot + 1) % kFramesInFlight;
        return m_slot;
    }
    // Call from the command buffer's completion handler.
    void end() { dispatch_semaphore_signal(m_sem); }
    // Blocks until every frame in flight has completed (its handler has run to end()).
    void drain() {
        for (int i = 0; i < kFramesInFlight; ++i) dispatch_semaphore_wait(m_sem, DISPATCH_TIME_FOREVER);
        for (int i = 0; i < kFramesInFlight; ++i) dispatch_semaphore_signal(m_sem);
    }
    int slot() const { return m_slot; }

private:
    dispatch_semaphore_t m_sem;
    int m_slot = 0;
};

// One shared buffer per frame slot. Grows (rarely) to the largest size requested; never shrinks.
class RingBuffer {
public:
    void init(MTL::Device* dev, const char* label) { m_dev = dev; m_label = label; }
    ~RingBuffer() { for (auto* b : m_bufs) if (b) b->release(); }

    MTL::Buffer* upload(int slot, const void* data, size_t bytes) {
        MTL::Buffer* b = ensure(slot, bytes);
        if (bytes) std::memcpy(b->contents(), data, bytes);
        return b;
    }

    MTL::Buffer* ensure(int slot, size_t bytes) {
        size_t need = bytes < 256 ? 256 : bytes;
        MTL::Buffer*& b = m_bufs[slot];
        if (!b || b->length() < need) {
            if (b) b->release();
            size_t cap = need + need / 2;
            b = m_dev->newBuffer(cap, MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined);
            b->setLabel(NS::String::string(m_label, NS::UTF8StringEncoding));
        }
        return b;
    }

private:
    MTL::Device* m_dev = nullptr;
    const char* m_label = "";
    MTL::Buffer* m_bufs[kFramesInFlight] = {};
};

} // namespace calc
