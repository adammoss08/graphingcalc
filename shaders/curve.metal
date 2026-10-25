// Thick anti-aliased curves (Desmos: width 2.5, opacity .7) and inequality fills.
//
// No double blending: a curve drawn at 70% opacity must look like a single canvas stroke, even
// where its own segments overlap at joints. Attachments 1 and 2 are memoryless tile storage:
//   color(1) = destination colour before the current curve started ("base")
//   color(2) = (curve id << 8) | max coverage so far for that curve
// Each fragment recomposites base + curve at max(coverage), so overlaps never accumulate. On Apple
// GPUs, framebuffer fetch is ordered per pixel, so this is exact and costs no memory bandwidth.
#include "render/ShaderTypes.hpp"

struct CurveVOut {
    float4 position [[position]];
    float2 local;              // (along, across) in points
    float len [[flat]];
    float dashStart [[flat]];
    uint style [[flat]];
};

struct CurveFOut {
    float4 color [[color(0)]];
    float4 base [[color(1)]];
    uint idcov [[color(2)]];
};

static CurveVOut expandSegment(float2 a, float2 b, uint vid, float halfWidth, float aa,
                               constant FrameUniforms& u) {
    float2 corner = float2(vid & 1, vid >> 1);
    float2 d = b - a;
    float len = length(d);
    float2 dir = len > 1e-5 ? d / len : float2(1, 0);
    float2 nrm = float2(-dir.y, dir.x);
    float hw = halfWidth + aa;
    float along = mix(-hw, len + hw, corner.x);
    float across = mix(-hw, hw, corner.y);
    float2 p = a + dir * along + nrm * across;
    CurveVOut o;
    o.position = float4(p / u.viewSize * float2(2, -2) + float2(-1, 1), 0, 1);
    o.local = float2(along, across);
    o.len = len;
    return o;
}

static CurveVOut degenerate() {
    CurveVOut o;
    o.position = float4(-4, -4, 0, 1);
    o.local = 0;
    o.len = 0;
    o.dashStart = 0;
    o.style = 0;
    return o;
}

vertex CurveVOut curveVertex(uint vid [[vertex_id]], uint iid [[instance_id]],
                             const device CurveVertex* verts [[buffer(0)]],
                             const device CurveStyle* styles [[buffer(1)]],
                             constant FrameUniforms& u [[buffer(2)]]) {
    CurveVertex a = verts[iid], b = verts[iid + 1];
    if (a.curve != b.curve || any(isnan(a.pos)) || any(isnan(b.pos))) return degenerate();
    CurveStyle st = styles[a.curve];
    CurveVOut o = expandSegment(a.pos * st.zoom + st.translate, b.pos * st.zoom + st.translate, vid, st.width * 0.5,
                                1.0 / u.scale, u);
    o.dashStart = a.cumLen * st.zoom;
    o.style = a.curve;
    return o;
}

vertex CurveVOut implicitSegmentVertex(uint vid [[vertex_id]], uint iid [[instance_id]],
                                       const device ImplicitSegment* segs [[buffer(0)]],
                                       const device CurveStyle* styles [[buffer(1)]],
                                       constant FrameUniforms& u [[buffer(2)]]) {
    ImplicitSegment s = segs[iid];
    CurveStyle st = styles[s.style];
    float2 a = s.ends.xy + st.translate, b = s.ends.zw + st.translate;
    CurveVOut o = expandSegment(a, b, vid, st.width * 0.5, 1.0 / u.scale, u);
    // Screen-space dash phase so neighbouring segments line up.
    float2 dir = normalize(b - a + 1e-6);
    o.dashStart = abs(dir.x) > abs(dir.y) ? a.x * sign(dir.x) : a.y * sign(dir.y);
    o.style = s.style;
    return o;
}

fragment CurveFOut curveFragment(CurveVOut in [[stage_in]],
                                 float4 dst [[color(0)]],
                                 float4 base [[color(1)]],
                                 uint idcov [[color(2)]],
                                 const device CurveStyle* styles [[buffer(1)]],
                                 constant FrameUniforms& u [[buffer(2)]]) {
    CurveStyle st = styles[in.style];
    float2 screen = in.position.xy / u.scale;
    CurveFOut o;
    o.color = dst;
    o.base = base;
    o.idcov = idcov;
    if (screen.x < st.clip.x || screen.y < st.clip.y || screen.x > st.clip.z || screen.y > st.clip.w)
        return o;

    float px = 1.0 / u.scale;
    float dx = max(max(-in.local.x, in.local.x - in.len), 0.0);
    float d = length(float2(dx, in.local.y)) - st.width * 0.5;
    float cov = clamp(0.5 - d / px, 0.0, 1.0);
    if (st.dash > 0) {
        float s = in.dashStart + clamp(in.local.x, 0.0, in.len);
        float period = st.dash * 18.0 / 11.0;
        float ph = s - floor(s / period) * period;
        // anti-aliased dash ends
        float onEdge = min(ph, st.dash - ph);
        cov *= clamp(0.5 + onEdge / px, 0.0, 1.0);
    } else if (st.dash < 0) {
        // Dotted: round dots of the line's width every -dash points along the curve.
        float period = -st.dash;
        float s = in.dashStart + in.local.x;
        float ph = s - floor(s / period) * period - period * 0.5;
        float dd = length(float2(ph, in.local.y)) - st.width * 0.5;
        cov = min(cov, clamp(0.5 - dd / px, 0.0, 1.0));
    }

    uint id = idcov >> 8;
    float prev = float(idcov & 255u) / 255.0;
    if (id != st.id) {
        base = dst;
        prev = 0;
    }
    float c = max(prev, cov);
    float a = st.color.a * c;
    o.color = float4(mix(base.rgb, st.color.rgb, a), base.a + (1 - base.a) * a);
    o.base = base;
    o.idcov = (st.id << 8) | uint(round(c * 255.0));
    return o;
}

// ---- inequality fill ---------------------------------------------------------------------------

struct FillVOut {
    float4 position [[position]];
};

vertex FillVOut fillVertex(uint vid [[vertex_id]], constant FillUniforms& f [[buffer(0)]],
                           constant FrameUniforms& u [[buffer(1)]]) {
    float2 corner = float2(vid & 1, vid >> 1);
    float2 p = mix(f.clip.xy, f.clip.zw, corner);
    FillVOut o;
    o.position = float4(p / u.viewSize * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

fragment float4 fillFragment(FillVOut in [[stage_in]], constant FillUniforms& f [[buffer(0)]],
                             constant FrameUniforms& u [[buffer(1)]],
                             texture2d_array<float> field [[texture(0)]]) {
    float2 screen = in.position.xy / u.scale;
    float2 g = (screen - f.origin) / f.cellPts;
    g = clamp(g, float2(0), float2(f.gridW - 1, f.gridH - 1) - 1e-3);
    uint2 i = uint2(floor(g));
    float2 t = g - float2(i);
    float v00 = field.read(i, f.slice).r;
    float v10 = field.read(i + uint2(1, 0), f.slice).r;
    float v01 = field.read(i + uint2(0, 1), f.slice).r;
    float v11 = field.read(i + uint2(1, 1), f.slice).r;
    float v = mix(mix(v00, v10, t.x), mix(v01, v11, t.x), t.y);
    if (isnan(v)) {
        // Partially undefined cell: use the nearest defined corner's sign.
        float4 vs = float4(v00, v10, v01, v11);
        float2 c = round(t);
        v = vs[uint(c.y) * 2 + uint(c.x)];
        if (isnan(v)) discard_fragment();
    }
    float w = fwidth(v);
    float cov = w > 0 ? clamp(v / w + 0.5, 0.0, 1.0) : (v > 0 ? 1.0 : 0.0);
    float a = f.color.a * cov;
    return float4(f.color.rgb * a, a);
}
