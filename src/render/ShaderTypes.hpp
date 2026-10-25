#pragma once
// Structs shared by C++ and Metal shaders. Layouts must match exactly: use only 4-component
// vectors and scalars, ordered so nothing needs padding.
#ifdef __METAL_VERSION__
#include <metal_stdlib>
using namespace metal;
typedef float4 sh_float4;
typedef float2 sh_float2;
typedef uint sh_uint;
#else
#include <simd/simd.h>
typedef simd_float4 sh_float4;
typedef simd_float2 sh_float2;
typedef uint32_t sh_uint;
#endif

// UI primitive kinds (UiInstance.params.z)
#define UI_KIND_RECT 0      // rounded rect: fill color, border color2/width
#define UI_KIND_GRADIENT 1  // vertical gradient color -> color2
#define UI_KIND_LINE 2      // capsule from rect.xy to rect.zw, thickness params.w
#define UI_KIND_GLYPH 3     // atlas coverage, uv; params.y = halo radius (points) in color2
#define UI_KIND_SHADOW 4    // soft drop shadow for a rounded rect: params.y = blur radius
#define UI_KIND_IMAGE 5     // RGBA image from texture(1) (premultiplied), times color.a

struct UiInstance {
    sh_float4 rect;     // x, y, w, h (points); for lines: x0, y0, x1, y1
    sh_float4 color;    // straight RGBA
    sh_float4 color2;   // border / gradient end / halo colour
    sh_float4 params;   // x: corner radius, y: border width | halo | blur, z: kind, w: line thickness
    sh_float4 uv;       // glyph atlas uv rect
    sh_float4 clip;     // clip rect x0, y0, x1, y1 (points)
};

struct FrameUniforms {
    sh_float2 viewSize;     // points
    float scale;            // device pixels per point
    float time;
};

// CPU-sampled curve vertex: position relative to its curve's anchor (points, y down).
struct CurveVertex {
    sh_float2 pos;
    float cumLen;           // arc length from the start of the run (points), for dashes
    sh_uint curve;          // index into CurveStyle array
};

struct CurveStyle {
    sh_float4 color;        // rgb + opacity in a
    sh_float4 clip;         // graph area x0, y0, x1, y1 (points)
    sh_float2 translate;    // screen position of the anchor (points)
    float width;            // line width (points)
    float dash;             // 0 solid; > 0 dash length (gap = dash * 7/11); < 0 dotted, period -dash
    sh_uint id;             // unique per curve per frame (>= 1), for no-double-blend
    float zoom;             // current scale / scale at sampling (a stale curve drawn while it resamples)
    sh_uint pad1, pad2;
};

// GPU implicit-curve piece.
struct ImplicitPiece {
    sh_uint codeOffset, codeLength, constOffset, styleIndex;
    sh_uint wantContour, pad0, pad1, pad2;
};

struct ImplicitUniforms {
    sh_float2 origin;       // graph-area top-left on screen (points)
    sh_float2 graphSize;    // points
    float cellPts;          // grid cell size (points)
    float scale;            // points per world unit
    float centerX, centerY; // world centre (float; see CLAUDE.md precision note)
    sh_uint gridW, gridH;   // nodes per row/column
    sh_uint pieceCount, segmentCapacity;
};

// One implicit contour segment (screen points) + its style.
struct ImplicitSegment {
    sh_float4 ends;         // x0, y0, x1, y1
    sh_uint style;
    sh_uint pad0, pad1, pad2;
};

struct FillUniforms {
    sh_float4 color;        // rgb + fill opacity
    sh_float4 clip;
    sh_uint slice;          // field texture slice
    sh_uint gridW, gridH;
    float cellPts;
    sh_float2 origin;
    float strictEdge;       // unused by fill; kept for alignment
    float pad;
};
