// All 2D UI in one instanced draw: rounded rects, gradients, lines, glyphs, shadows.
#include "render/ShaderTypes.hpp"

struct UiVOut {
    float4 position [[position]];
    float2 local;          // position relative to the primitive (points)
    float2 uv;
    uint inst [[flat]];
};

vertex UiVOut uiVertex(uint vid [[vertex_id]], uint iid [[instance_id]],
                       const device UiInstance* insts [[buffer(0)]],
                       constant FrameUniforms& u [[buffer(1)]]) {
    UiInstance in = insts[iid];
    float2 corner = float2(vid & 1, vid >> 1);
    float kind = in.params.z;
    float2 p;
    float2 local;
    float aa = 1.5 / u.scale;
    if (kind == UI_KIND_LINE) {
        float2 a = in.rect.xy, b = in.rect.zw;
        float2 d = b - a;
        float len = max(length(d), 1e-4);
        float2 dir = d / len, nrm = float2(-dir.y, dir.x);
        float hw = in.params.w * 0.5 + aa;
        float along = mix(-hw, len + hw, corner.x);
        float across = mix(-hw, hw, corner.y);
        p = a + dir * along + nrm * across;
        local = float2(along, across);
    } else {
        // Glyph quads already include the atlas padding (which also holds the halo).
        float pad = kind == UI_KIND_GLYPH ? 0.0 : (kind == UI_KIND_SHADOW ? in.params.y * 2.0 : 0.0) + aa;
        float2 lo = in.rect.xy - pad, hi = in.rect.xy + in.rect.zw + pad;
        p = mix(lo, hi, corner);
        local = p - in.rect.xy;
    }
    UiVOut o;
    o.position = float4(p / u.viewSize * float2(2, -2) + float2(-1, 1), 0, 1);
    o.local = local;
    o.uv = mix(in.uv.xy, in.uv.zw, local / max(in.rect.zw, float2(1e-4)));
    o.inst = iid;
    return o;
}

static float roundedBoxSdf(float2 p, float2 halfSize, float r) {
    float2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

fragment float4 uiFragment(UiVOut in [[stage_in]],
                           const device UiInstance* insts [[buffer(0)]],
                           constant FrameUniforms& u [[buffer(1)]],
                           texture2d<float> atlas [[texture(0)]],
                           texture2d<float> image [[texture(1)]]) {
    UiInstance s = insts[in.inst];
    float2 screen = in.position.xy / u.scale;
    if (screen.x < s.clip.x || screen.y < s.clip.y || screen.x > s.clip.z || screen.y > s.clip.w)
        discard_fragment();
    float px = 1.0 / u.scale;   // one device pixel in points
    float kind = s.params.z;
    float4 c;
    if (kind == UI_KIND_LINE) {
        float len = length(s.rect.zw - s.rect.xy);
        float2 q = in.local;
        float dx = max(max(-q.x, q.x - len), 0.0);
        float d = length(float2(dx, q.y)) - s.params.w * 0.5;
        float cov = clamp(0.5 - d / px, 0.0, 1.0);
        c = float4(s.color.rgb, s.color.a * cov);
    } else if (kind == UI_KIND_GLYPH) {
        constexpr sampler smp(filter::linear, address::clamp_to_zero);
        float a = atlas.sample(smp, in.uv).r;
        float halo = s.params.y;
        if (halo > 0) {
            // Dilated coverage for the white label halo, composited under the glyph.
            float2 texel = (s.uv.zw - s.uv.xy) / s.rect.zw * halo;
            float h = a;
            for (int k = 0; k < 12; ++k) {
                float ang = float(k) * (6.2831853 / 12.0);
                h = max(h, atlas.sample(smp, in.uv + float2(cos(ang), sin(ang)) * texel).r);
                h = max(h, atlas.sample(smp, in.uv + float2(cos(ang), sin(ang)) * texel * 0.5).r);
            }
            float4 hc = float4(s.color2.rgb, s.color2.a * h);
            float4 gc = float4(s.color.rgb, s.color.a * a);
            float outA = gc.a + hc.a * (1 - gc.a);
            float3 rgb = outA > 0 ? (gc.rgb * gc.a + hc.rgb * hc.a * (1 - gc.a)) / outA : float3(0);
            c = float4(rgb, outA);
        } else {
            c = float4(s.color.rgb, s.color.a * a);
        }
    } else if (kind == UI_KIND_IMAGE) {
        constexpr sampler smp(filter::linear, mip_filter::none, address::clamp_to_edge);
        float4 t = image.sample(smp, in.uv);    // premultiplied
        float2 inside = step(float2(0), in.local) * step(in.local, s.rect.zw);
        return t * s.color.a * inside.x * inside.y;
    } else if (kind == UI_KIND_SHADOW) {
        float2 hs = s.rect.zw * 0.5;
        float d = roundedBoxSdf(in.local - hs, hs, s.params.x);
        float blur = max(s.params.y, 0.5);
        float a = 1.0 - smoothstep(-blur, blur, d);
        c = float4(s.color.rgb, s.color.a * a);
    } else {
        float2 hs = s.rect.zw * 0.5;
        float d = roundedBoxSdf(in.local - hs, hs, min(s.params.x, min(hs.x, hs.y)));
        float cov = clamp(0.5 - d / px, 0.0, 1.0);
        float4 fill = s.color;
        if (kind == UI_KIND_GRADIENT) fill = mix(s.color, s.color2, clamp(in.local.y / s.rect.w, 0.0, 1.0));
        float bw = s.params.y;
        if (bw > 0 && kind != UI_KIND_GRADIENT) {
            float inner = clamp(0.5 - (d + bw) / px, 0.0, 1.0);  // inside the border
            float4 border = s.color2;
            float4 pm = mix(float4(border.rgb * border.a, border.a), float4(fill.rgb * fill.a, fill.a), inner);
            return pm * cov;
        }
        c = float4(fill.rgb, fill.a * cov);
    }
    return float4(c.rgb * c.a, c.a);  // premultiplied
}
