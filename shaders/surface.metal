// 3D mode: surfaces z = f(x, y) evaluated on the GPU with the bytecode VM, lit and clipped to the
// [-L, L]^3 box, plus depth-tested 3D lines (axes, grids, space curves, points).
#include "render/ShaderTypes.hpp"
#include "vm.h"

kernel void surfaceGrid(uint3 gid [[thread_position_in_grid]],
                        device const uint* code [[buffer(0)]],
                        device const float* consts [[buffer(1)]],
                        device const ImplicitPiece* pieces [[buffer(2)]],
                        constant SurfaceGridUniforms& U [[buffer(3)]],
                        device float4* positions [[buffer(4)]]) {
    uint n = U.gridN;
    if (gid.x >= n || gid.y >= n || gid.z >= U.count) return;
    float L = U.halfSize;
    float x = -L + 2 * L * float(gid.x) / float(n - 1);
    float y = -L + 2 * L * float(gid.y) / float(n - 1);
    ImplicitPiece p = pieces[gid.z];
    float z = gvm::run(code + p.codeOffset, p.codeLength, consts + p.constOffset, x, y);
    positions[gid.z * n * n + gid.y * n + gid.x] = float4(x, y, z, isfinite(z) ? 1.0 : 0.0);
}

static float4 toView(constant Scene3DUniforms& S, float3 world) {
    float4 c = S.viewProj * float4(world, 1);
    c.x = c.x * S.clipRemap.x + S.clipRemap.z * c.w;
    c.y = c.y * S.clipRemap.y + S.clipRemap.w * c.w;
    return c;
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
    float3 world = p.w > 0.5 ? p.xyz : float3(0);
    o.world = world;
    o.normal = normalize(cross(dx, dy) + float3(0, 0, 1e-12));
    o.valid = p.w;
    o.position = toView(S, world);
    return o;
}

fragment float4 surfaceFragment(SurfaceVOut in [[stage_in]], bool front [[front_facing]],
                                constant Scene3DUniforms& S [[buffer(1)]],
                                constant SurfaceStyle& st [[buffer(2)]]) {
    float L = S.eye.w;
    if (in.valid < 0.999 || abs(in.world.z) > L) discard_fragment();
    float3 n = normalize(in.normal);
    float3 v = normalize(S.eye.xyz - in.world);
    if (dot(n, v) < 0) n = -n;                       // two-sided lighting
    float3 l = normalize(S.light.xyz);
    float diffuse = max(dot(n, l), 0.0);
    float3 h = normalize(l + v);
    float spec = pow(max(dot(n, h), 0.0), 40.0) * 0.18;
    float3 base = st.color.rgb;
    float3 c = base * (0.58 + 0.42 * diffuse) + spec;
    // Desmos-style grid lines on the surface at each grid step in x and y.
    float2 g = in.world.xy / S.light.w;
    float2 d = abs(fract(g - 0.5) - 0.5) / max(fwidth(g), float2(1e-6));
    float line = 1.0 - min(min(d.x, d.y), 1.0);
    c = mix(c, c * 0.62, line * 0.55);
    float a = st.color.a;
    return float4(c * a, a);
}

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
