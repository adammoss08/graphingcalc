// 3D mode: grid surfaces (z = f(x, y) and parametric (f, g, h)(u, v)) evaluated on the GPU with the
// bytecode VM; implicit surfaces F(x, y, z) = 0 meshed with surface nets; depth-tested 3D lines
// (axes, grids, space curves, points). Everything is clipped to the [-L, L]^3 box.
#include "render/ShaderTypes.hpp"
#include "vm.h"

// ---- shared ------------------------------------------------------------------------------------

static float4 toView(constant Scene3DUniforms& S, float3 world) {
    float4 c = S.viewProj * float4(world, 1);
    c.x = c.x * S.clipRemap.x + S.clipRemap.z * c.w;
    c.y = c.y * S.clipRemap.y + S.clipRemap.w * c.w;
    return c;
}

static bool outsideBox(constant Scene3DUniforms& S, float3 w) {
    float L = S.eye.w * 1.0001;
    return any(abs(w) > L);
}

// Two-sided diffuse + a little specular, like Desmos 3D's matte surfaces.
static float3 shade(constant Scene3DUniforms& S, float3 base, float3 n, float3 world) {
    float3 v = normalize(S.eye.xyz - world);
    if (dot(n, v) < 0) n = -n;
    float3 l = normalize(S.light.xyz);
    float diffuse = max(dot(n, l), 0.0);
    float3 h = normalize(l + v);
    float spec = pow(max(dot(n, h), 0.0), 40.0) * 0.18;
    return base * (0.58 + 0.42 * diffuse) + spec;
}

// ---- grid surfaces -----------------------------------------------------------------------------

kernel void surfaceGrid(uint3 gid [[thread_position_in_grid]],
                        device const uint* code [[buffer(0)]],
                        device const float* consts [[buffer(1)]],
                        device const SurfaceProgram3D* progs [[buffer(2)]],
                        constant SurfaceGridUniforms& U [[buffer(3)]],
                        device float4* positions [[buffer(4)]]) {
    uint n = U.gridN;
    if (gid.x >= n || gid.y >= n || gid.z >= U.count) return;
    SurfaceProgram3D p = progs[gid.z];
    float2 f = float2(gid.xy) / float(n - 1);
    float3 w;
    if (p.code.w == SURFACE_PARAMETRIC) {
        float u = mix(p.domain.x, p.domain.y, f.x), v = mix(p.domain.z, p.domain.w, f.y);
        w.x = gvm::run(code + p.code.x, p.length.x, consts + p.consts.x, u, v);
        w.y = gvm::run(code + p.code.y, p.length.y, consts + p.consts.y, u, v);
        w.z = gvm::run(code + p.code.z, p.length.z, consts + p.consts.z, u, v);
    } else {
        float L = U.halfSize;
        w.x = -L + 2 * L * f.x;
        w.y = -L + 2 * L * f.y;
        w.z = gvm::run(code + p.code.x, p.length.x, consts + p.consts.x, w.x, w.y);
    }
    bool valid = all(isfinite(w));
    positions[gid.z * n * n + gid.y * n + gid.x] = float4(valid ? w : float3(0), valid ? 1.0 : 0.0);
}

// Grid neighbour (clamped), falling back to the centre point when it's undefined.
static float3 neighbour(const device float4* positions, uint base, uint n, uint i, uint j, int di, int dj, float3 centre) {
    int ii = clamp(int(i) + di, 0, int(n) - 1), jj = clamp(int(j) + dj, 0, int(n) - 1);
    float4 q = positions[base + uint(jj) * n + uint(ii)];
    return q.w > 0.5 ? q.xyz : centre;
}

struct SurfaceVOut {
    float4 position [[position]];
    float3 world;
    float3 normal;
    float2 uv;
    float valid;
};

vertex SurfaceVOut surfaceVertex(uint vid [[vertex_id]],
                                 const device float4* positions [[buffer(0)]],
                                 constant Scene3DUniforms& S [[buffer(1)]],
                                 constant SurfaceStyle& st [[buffer(2)]]) {
    uint n = S.gridN;
    uint local = vid - st.base;
    uint i = local % n, j = local / n;
    float4 p = positions[vid];
    // Normal from neighbouring grid points (central differences).
    float3 dx = neighbour(positions, st.base, n, i, j, 1, 0, p.xyz) - neighbour(positions, st.base, n, i, j, -1, 0, p.xyz);
    float3 dy = neighbour(positions, st.base, n, i, j, 0, 1, p.xyz) - neighbour(positions, st.base, n, i, j, 0, -1, p.xyz);
    SurfaceVOut o;
    o.world = p.xyz;
    float3 c = cross(dx, dy);
    o.normal = length(c) > 0 ? normalize(c) : float3(0, 0, 1);
    o.uv = float2(i, j) / float(n - 1);
    o.valid = p.w;
    o.position = toView(S, p.xyz);
    return o;
}

fragment float4 surfaceFragment(SurfaceVOut in [[stage_in]],
                                constant Scene3DUniforms& S [[buffer(1)]],
                                constant SurfaceStyle& st [[buffer(2)]]) {
    if (in.valid < 0.999 || outsideBox(S, in.world)) discard_fragment();
    float3 c = shade(S, st.color.rgb, normalize(in.normal), in.world);
    // Desmos-style lines on the surface: world x/y grid steps, or u/v lines on parametric surfaces.
    float2 g = st.kind == SURFACE_PARAMETRIC ? in.uv * float(S.paramLines) : in.world.xy / S.light.w;
    float2 d = abs(fract(g - 0.5) - 0.5) / max(fwidth(g), float2(1e-6));
    float line = 1.0 - min(min(d.x, d.y), 1.0);
    c = mix(c, c * 0.62, line * 0.55);
    float a = st.color.a;
    return float4(c * a, a);
}

// ---- implicit surfaces: surface nets -----------------------------------------------------------
// 1. implicit3dField: F at every grid point of the surface's box.
// 2. implicit3dCells: each cell with a sign change gets one vertex at the mean of its edge
//    crossings (normal = trilinear gradient). Cells touching an undefined corner (restrictions,
//    sqrt of negatives...) get none, which clips the surface there.
// 3. implicit3dQuads: each grid edge with a sign change joins the vertices of its 4 cells.

static uint fieldIndex(uint3 p, uint n) { return (p.z * n + p.y) * n + p.x; }

kernel void implicit3dField(uint3 gid [[thread_position_in_grid]],
                            device const uint* code [[buffer(0)]],
                            device const float* consts [[buffer(1)]],
                            constant Implicit3DUniforms& U [[buffer(2)]],
                            device float* field [[buffer(3)]]) {
    uint n = U.n;
    if (any(gid >= n)) return;
    float3 p = mix(U.boxMin.xyz, U.boxMax.xyz, float3(gid) / float(n - 1));
    field[fieldIndex(gid, n)] = gvm::run(code + U.codeOffset, U.codeLength, consts + U.constOffset, p.x, p.y, p.z);
}

kernel void implicit3dCells(uint3 gid [[thread_position_in_grid]],
                            constant Implicit3DUniforms& U [[buffer(2)]],
                            device const float* field [[buffer(3)]],
                            device uint* cellIndex [[buffer(4)]],
                            device MeshVertex3D* verts [[buffer(5)]],
                            device atomic_uint* counters [[buffer(6)]]) {
    uint n = U.n, m = n - 1;
    if (any(gid >= m)) return;
    uint cell = (gid.z * m + gid.y) * m + gid.x;
    float c[8];
    int positive = 0;
    for (uint k = 0; k < 8; ++k) {
        c[k] = field[fieldIndex(gid + uint3(k & 1, (k >> 1) & 1, (k >> 2) & 1), n)];
        if (!isfinite(c[k])) { cellIndex[cell] = MESH_INVALID; return; }
        positive += c[k] >= 0 ? 1 : 0;
    }
    if (positive == 0 || positive == 8) { cellIndex[cell] = MESH_INVALID; return; }
    float3 sum = 0;
    float crossings = 0;
    for (uint bit = 1; bit <= 4; bit <<= 1) {
        for (uint a = 0; a < 8; ++a) {
            if (a & bit) continue;
            uint b = a | bit;
            if ((c[a] >= 0) == (c[b] >= 0)) continue;
            float t = c[a] / (c[a] - c[b]);
            float3 pa = float3(a & 1, (a >> 1) & 1, (a >> 2) & 1);
            float3 pb = float3(b & 1, (b >> 1) & 1, (b >> 2) & 1);
            sum += mix(pa, pb, t);
            crossings += 1;
        }
    }
    float3 cellSize = (U.boxMax.xyz - U.boxMin.xyz) / float(m);
    float3 world = U.boxMin.xyz + (float3(gid) + sum / crossings) * cellSize;
    float3 g = float3((c[1] - c[0]) + (c[3] - c[2]) + (c[5] - c[4]) + (c[7] - c[6]),
                      (c[2] - c[0]) + (c[3] - c[1]) + (c[6] - c[4]) + (c[7] - c[5]),
                      (c[4] - c[0]) + (c[5] - c[1]) + (c[6] - c[2]) + (c[7] - c[3])) / cellSize;
    float3 normal = length(g) > 0 ? normalize(g) : float3(0, 0, 1);
    uint idx = atomic_fetch_add_explicit(&counters[MESH_COUNT_VERTICES], 1, memory_order_relaxed);
    if (idx >= U.vertexCapacity) { cellIndex[cell] = MESH_INVALID; return; }  // CPU grows and retries
    verts[idx].pos = float4(world, float(U.surface));
    verts[idx].normal = float4(normal, 0);
    cellIndex[cell] = idx;
}

kernel void implicit3dQuads(uint3 gid [[thread_position_in_grid]],
                            constant Implicit3DUniforms& U [[buffer(2)]],
                            device const float* field [[buffer(3)]],
                            device const uint* cellIndex [[buffer(4)]],
                            device uint* indices [[buffer(5)]],
                            device atomic_uint* counters [[buffer(6)]]) {
    uint n = U.n, m = n - 1;
    if (any(gid >= n)) return;
    float f0 = field[fieldIndex(gid, n)];
    if (!isfinite(f0)) return;
    uint counter = U.translucent ? MESH_COUNT_TRANSLUCENT : MESH_COUNT_OPAQUE;
    for (uint axis = 0; axis < 3; ++axis) {
        uint b = (axis + 1) % 3, c = (axis + 2) % 3;
        uint3 q = gid;
        q[axis] += 1;
        if (q[axis] >= n || gid[b] < 1 || gid[c] < 1 || gid[b] > m - 1 || gid[c] > m - 1) continue;
        float f1 = field[fieldIndex(q, n)];
        if (!isfinite(f1) || (f0 >= 0) == (f1 >= 0)) continue;
        // The 4 cells around this edge.
        uint ids[4];
        bool ok = true;
        for (uint k = 0; k < 4; ++k) {
            uint3 cc = gid;
            cc[b] -= (k == 0 || k == 3) ? 1 : 0;
            cc[c] -= (k == 0 || k == 1) ? 1 : 0;
            ids[k] = cellIndex[(cc.z * m + cc.y) * m + cc.x];
            ok = ok && ids[k] != MESH_INVALID;
        }
        if (!ok) continue;
        uint base = atomic_fetch_add_explicit(&counters[counter], 6, memory_order_relaxed);
        if (base + 6 > U.indexCapacity) continue;  // counted, so the CPU sees the overflow and grows
        device uint* out = indices + base;
        out[0] = ids[0]; out[1] = ids[1]; out[2] = ids[2];
        out[3] = ids[0]; out[4] = ids[2]; out[5] = ids[3];
    }
}

// Indirect draw arguments for the opaque and translucent lists (indices clamped to capacity).
kernel void implicit3dArgs(device const uint* counters [[buffer(0)]],
                           device uint* args [[buffer(1)]],
                           constant uint2& capacity [[buffer(2)]]) {
    for (uint list = 0; list < 2; ++list) {
        uint count = counters[list == 0 ? MESH_COUNT_OPAQUE : MESH_COUNT_TRANSLUCENT];
        uint cap = (list == 0 ? capacity.x : capacity.y) / 6 * 6;
        device uint* a = args + list * 5;   // MTLDrawIndexedPrimitivesIndirectArguments
        a[0] = min(count, cap);
        a[1] = 1;
        a[2] = 0;
        a[3] = 0;
        a[4] = 0;
    }
}

struct MeshVOut {
    float4 position [[position]];
    float3 world [[user(world)]];
    float3 normal [[user(normal)]];
    float4 color [[user(color), flat]];
    // The box faces as hardware clip planes: meshes extend a little past the box and are cut
    // exactly at it, without a discard (which would turn off hidden-surface removal).
    float clip [[clip_distance]] [6];
};

// The fragment side of MeshVOut (clip distances are vertex-only).
struct MeshFIn {
    float4 position [[position]];
    float3 world [[user(world)]];
    float3 normal [[user(normal)]];
    float4 color [[user(color), flat]];
};

vertex MeshVOut meshVertex(uint vid [[vertex_id]],
                           const device MeshVertex3D* verts [[buffer(0)]],
                           constant Scene3DUniforms& S [[buffer(1)]],
                           const device float4* styles [[buffer(2)]]) {
    MeshVertex3D v = verts[vid];
    MeshVOut o;
    o.world = v.pos.xyz;
    o.normal = v.normal.xyz;
    o.color = styles[uint(v.pos.w)];
    o.position = toView(S, v.pos.xyz);
    float L = S.eye.w;
    for (int a = 0; a < 3; ++a) {
        o.clip[2 * a] = L - v.pos[a];
        o.clip[2 * a + 1] = L + v.pos[a];
    }
    return o;
}

// Translucent meshes (inequalities): blended, premultiplied.
fragment float4 meshFragment(MeshFIn in [[stage_in]], constant Scene3DUniforms& S [[buffer(1)]]) {
    float3 c = shade(S, in.color.rgb, normalize(in.normal), in.world);
    float a = in.color.a;
    return float4(c * a, a);
}

// Opaque meshes: no blending and no discard (vertices always lie inside the box), so the GPU's
// hidden-surface removal shades each pixel once however many sheets overlap (dense gyroids).
fragment float4 meshFragmentOpaque(MeshFIn in [[stage_in]], constant Scene3DUniforms& S [[buffer(1)]]) {
    return float4(shade(S, in.color.rgb, normalize(in.normal), in.world), 1);
}

// ---- lines and points --------------------------------------------------------------------------

struct Line3DVOut {
    float4 position [[position]];
    float2 local;
    float len [[flat]];
    float width [[flat]];
    float4 color [[flat]];
};

vertex Line3DVOut line3dVertex(uint vid [[vertex_id]], uint iid [[instance_id]],
                               const device Line3D* lines [[buffer(0)]],
                               constant Scene3DUniforms& S [[buffer(1)]],
                               constant FrameUniforms& u [[buffer(2)]]) {
    Line3D L = lines[iid];
    float4 ca = toView(S, L.a.xyz), cb = toView(S, L.b.xyz);
    Line3DVOut o;
    o.color = L.color;
    o.width = L.a.w;
    if (ca.w <= 1e-4 || cb.w <= 1e-4) {  // behind the camera
        o.position = float4(-4, -4, 0, 1);
        o.local = 0;
        o.len = 0;
        return o;
    }
    float3 na = ca.xyz / ca.w, nb = cb.xyz / cb.w;
    // Full-view NDC -> points, expand the capsule in screen space, back to NDC.
    float2 sa = (na.xy * float2(0.5, -0.5) + 0.5) * u.viewSize;
    float2 sb = (nb.xy * float2(0.5, -0.5) + 0.5) * u.viewSize;
    float2 corner = float2(vid & 1, vid >> 1);
    float2 d = sb - sa;
    float len = length(d);
    float2 dir = len > 1e-5 ? d / len : float2(1, 0);
    float2 nrm = float2(-dir.y, dir.x);
    float hw = L.a.w * 0.5 + 1.0 / u.scale;
    float along = mix(-hw, len + hw, corner.x);
    float across = mix(-hw, hw, corner.y);
    float2 p = sa + dir * along + nrm * across;
    float t = len > 1e-5 ? clamp(along / len, 0.0, 1.0) : 0.0;
    float z = mix(na.z, nb.z, t) - 2e-4;            // nudge towards the camera: lines sit on surfaces
    o.position = float4(p / u.viewSize * float2(2, -2) + float2(-1, 1), z, 1);
    o.local = float2(along, across);
    o.len = len;
    return o;
}

fragment float4 line3dFragment(Line3DVOut in [[stage_in]], constant FrameUniforms& u [[buffer(2)]]) {
    float px = 1.0 / u.scale;
    float dx = max(max(-in.local.x, in.local.x - in.len), 0.0);
    float d = length(float2(dx, in.local.y)) - in.width * 0.5;
    float cov = clamp(0.5 - d / px, 0.0, 1.0);
    float a = in.color.a * cov;
    if (a <= 0) discard_fragment();
    return float4(in.color.rgb * a, a);
}
