// Implicit curves on the GPU: evaluate F(x, y) on a grid (one dispatch for every implicit piece),
// then marching squares with edge refinement emits contour segments into a buffer, and a
// one-thread kernel writes the indirect draw arguments. Nothing goes back to the CPU.
#include "render/ShaderTypes.hpp"
#include "vm.h"

static float2 worldAt(constant ImplicitUniforms& U, float2 gpt) {
    // gpt: graph-area point coordinates
    return float2(U.centerX + (gpt.x - U.graphSize.x * 0.5) / U.scale,
                  U.centerY + (U.graphSize.y * 0.5 - gpt.y) / U.scale);
}

static float evalPiece(device const uint* code, device const float* consts, ImplicitPiece p,
                       constant ImplicitUniforms& U, float2 gpt) {
    float2 w = worldAt(U, gpt);
    return gvm::run(code + p.codeOffset, p.codeLength, consts + p.constOffset, w.x, w.y);
}

kernel void implicitField(uint3 gid [[thread_position_in_grid]],
                          device const uint* code [[buffer(0)]],
                          device const float* consts [[buffer(1)]],
                          device const ImplicitPiece* pieces [[buffer(2)]],
                          constant ImplicitUniforms& U [[buffer(3)]],
                          texture2d_array<float, access::write> field [[texture(0)]]) {
    if (gid.x >= U.gridW || gid.y >= U.gridH || gid.z >= U.pieceCount) return;
    float f = evalPiece(code, consts, pieces[gid.z], U, float2(gid.xy) * U.cellPts);
    field.write(float4(f), gid.xy, gid.z);
}

// Regula falsi (Illinois) along an edge from a to b. Returns the crossing parameter t in [0, 1],
// or NaN when the sign flip is not a genuine root (a pole, e.g. tan) or the edge is undefined.
static float refineCrossing(device const uint* code, device const float* consts, ImplicitPiece p,
                            constant ImplicitUniforms& U, float2 a, float fa, float2 b, float fb) {
    float ref = max(fabs(fa), fabs(fb));
    float ta = 0, tb = 1;
    float t = fa / (fa - fb);
    float ft = 0;
    int side = 0;
    for (int i = 0; i < 3; ++i) {
        t = (ta * fb - tb * fa) / (fb - fa);
        if (!isfinite(t)) t = 0.5 * (ta + tb);
        ft = evalPiece(code, consts, p, U, mix(a, b, t));
        if (isnan(ft)) return NAN;
        if ((ft > 0) == (fa > 0)) {
            ta = t; fa = ft;
            if (side == -1) fb *= 0.5;
            side = -1;
        } else {
            tb = t; fb = ft;
            if (side == 1) fa *= 0.5;
            side = 1;
        }
    }
    return fabs(ft) <= 0.1 * ref + 1e-6 ? t : NAN;
}

// One thread per grid node: refines the crossing on its right (horizontal) and down (vertical)
// edges, so every edge is refined exactly once (cells share edges).
kernel void implicitEdges(uint3 gid [[thread_position_in_grid]],
                          device const uint* code [[buffer(0)]],
                          device const float* consts [[buffer(1)]],
                          device const ImplicitPiece* pieces [[buffer(2)]],
                          constant ImplicitUniforms& U [[buffer(3)]],
                          texture2d_array<float, access::read> field [[texture(0)]],
                          texture2d_array<float, access::write> hEdge [[texture(1)]],
                          texture2d_array<float, access::write> vEdge [[texture(2)]]) {
    if (gid.x >= U.gridW || gid.y >= U.gridH || gid.z >= U.pieceCount) return;
    ImplicitPiece p = pieces[gid.z];
    if (p.wantContour == 0) return;
    float cs = U.cellPts;
    float2 a = float2(gid.xy) * cs;
    float f0 = field.read(gid.xy, gid.z).r;
    if (gid.x + 1 < U.gridW) {
        float f1 = field.read(gid.xy + uint2(1, 0), gid.z).r;
        float t = NAN;
        if (!isnan(f0) && !isnan(f1) && (f0 > 0) != (f1 > 0))
            t = refineCrossing(code, consts, p, U, a, f0, a + float2(cs, 0), f1);
        hEdge.write(float4(t), gid.xy, gid.z);
    }
    if (gid.y + 1 < U.gridH) {
        float f1 = field.read(gid.xy + uint2(0, 1), gid.z).r;
        float t = NAN;
        if (!isnan(f0) && !isnan(f1) && (f0 > 0) != (f1 > 0))
            t = refineCrossing(code, consts, p, U, a, f0, a + float2(0, cs), f1);
        vEdge.write(float4(t), gid.xy, gid.z);
    }
}

kernel void implicitContour(uint3 gid [[thread_position_in_grid]],
                            device const ImplicitPiece* pieces [[buffer(2)]],
                            constant ImplicitUniforms& U [[buffer(3)]],
                            texture2d_array<float, access::read> field [[texture(0)]],
                            texture2d_array<float, access::read> hEdge [[texture(1)]],
                            texture2d_array<float, access::read> vEdge [[texture(2)]],
                            device ImplicitSegment* segs [[buffer(4)]],
                            device atomic_uint* counter [[buffer(5)]]) {
    if (gid.x + 1 >= U.gridW || gid.y + 1 >= U.gridH || gid.z >= U.pieceCount) return;
    ImplicitPiece p = pieces[gid.z];
    if (p.wantContour == 0) return;
    uint2 c = gid.xy;
    float f00 = field.read(c, gid.z).r;
    float f10 = field.read(c + uint2(1, 0), gid.z).r;
    float f11 = field.read(c + uint2(1, 1), gid.z).r;
    float f01 = field.read(c + uint2(0, 1), gid.z).r;
    if (isnan(f00) || isnan(f10) || isnan(f11) || isnan(f01)) return;
    uint idx = (f00 > 0 ? 1u : 0u) | (f10 > 0 ? 2u : 0u) | (f11 > 0 ? 4u : 0u) | (f01 > 0 ? 8u : 0u);
    if (idx == 0 || idx == 15) return;

    float cs = U.cellPts;
    float2 p00 = float2(c) * cs;
    // Edges: 0 = top (00-10), 1 = right (10-11), 2 = bottom (01-11), 3 = left (00-01)
    float te[4] = {hEdge.read(c, gid.z).r, vEdge.read(c + uint2(1, 0), gid.z).r,
                   hEdge.read(c + uint2(0, 1), gid.z).r, vEdge.read(c, gid.z).r};
    float2 ea[4] = {p00, p00 + float2(cs, 0), p00 + float2(0, cs), p00};
    float2 eb[4] = {p00 + float2(cs, 0), p00 + float2(cs, cs), p00 + float2(cs, cs), p00 + float2(0, cs)};

    // Segment table: pairs of edges per case. Saddles are resolved with the centre value.
    int segA0 = -1, segB0 = -1, segA1 = -1, segB1 = -1;
    switch (idx) {
    case 1: case 14: segA0 = 3; segB0 = 0; break;
    case 2: case 13: segA0 = 0; segB0 = 1; break;
    case 3: case 12: segA0 = 3; segB0 = 1; break;
    case 4: case 11: segA0 = 1; segB0 = 2; break;
    case 6: case 9:  segA0 = 0; segB0 = 2; break;
    case 7: case 8:  segA0 = 3; segB0 = 2; break;
    case 5: case 10: {
        float center = 0.25 * (f00 + f10 + f11 + f01);
        if ((center > 0) == (f00 > 0)) { segA0 = 0; segB0 = 1; segA1 = 2; segB1 = 3; }
        else { segA0 = 3; segB0 = 0; segA1 = 1; segB1 = 2; }
        break;
    }
    default: return;
    }

    for (int s = 0; s < 2; ++s) {
        int e0 = s == 0 ? segA0 : segA1, e1 = s == 0 ? segB0 : segB1;
        if (e0 < 0) continue;
        float t0 = te[e0], t1 = te[e1];
        if (isnan(t0) || isnan(t1)) continue;
        uint slot = atomic_fetch_add_explicit(counter, 1, memory_order_relaxed);
        if (slot >= U.segmentCapacity) return;
        ImplicitSegment seg;
        seg.ends = float4(mix(ea[e0], eb[e0], t0), mix(ea[e1], eb[e1], t1));
        seg.style = p.styleIndex;
        segs[slot] = seg;
    }
}

struct DrawArgs { uint vertexCount, instanceCount, vertexStart, baseInstance; };

kernel void implicitDrawArgs(device atomic_uint* counter [[buffer(0)]],
                             device DrawArgs* args [[buffer(1)]],
                             constant ImplicitUniforms& U [[buffer(2)]]) {
    uint n = atomic_load_explicit(counter, memory_order_relaxed);
    args->vertexCount = 4;
    args->instanceCount = min(n, U.segmentCapacity);
    args->vertexStart = 0;
    args->baseInstance = 0;
}

// Test hook (tests/test_vm.cpp): evaluates one program at a list of points.
kernel void vmEvalPoints(uint gid [[thread_position_in_grid]],
                         device const uint* code [[buffer(0)]],
                         device const float* consts [[buffer(1)]],
                         constant uint& codeLength [[buffer(2)]],
                         device const float2* points [[buffer(3)]],
                         device float* out [[buffer(4)]],
                         constant uint& count [[buffer(5)]]) {
    if (gid >= count) return;
    out[gid] = gvm::run(code, codeLength, consts, points[gid].x, points[gid].y);
}

// Test hook (tests/test_vm.cpp): evaluate a program at (x, y, t) points; t carries z in 3D.
kernel void vmEvalPoints3(uint gid [[thread_position_in_grid]],
                          device const uint* code [[buffer(0)]],
                          device const float* consts [[buffer(1)]],
                          constant uint& codeLength [[buffer(2)]],
                          device const float4* points [[buffer(3)]],
                          device float* out [[buffer(4)]],
                          constant uint& count [[buffer(5)]]) {
    if (gid >= count) return;
    out[gid] = gvm::run(code, codeLength, consts, points[gid].x, points[gid].y, points[gid].z);
}
