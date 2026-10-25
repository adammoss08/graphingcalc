#pragma once
// Structs shared by C++ and Metal shaders. Layouts must match exactly: use only 4-component
// vectors and scalars, ordered so nothing needs padding.
#ifdef __METAL_VERSION__
#include <metal_stdlib>
using namespace metal;
typedef float4 sh_float4;
typedef float2 sh_float2;
typedef float4x4 sh_float4x4;
typedef uint4 sh_uint4;
typedef uint sh_uint;
#else
#include <simd/simd.h>
typedef simd_float4 sh_float4;
typedef simd_float2 sh_float2;
typedef simd_float4x4 sh_float4x4;
typedef simd_uint4 sh_uint4;
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

// ---- 3D mode ------------------------------------------------------------------------------------

struct Scene3DUniforms {
    sh_float4x4 viewProj;   // world -> clip (camera space of the [-L, L]^3 box)
    sh_float4 clipRemap;    // camera NDC -> full-view NDC for the graph area: x' = x*a + b (xy: a, zw: b)
    sh_float4 eye;          // camera position (world units), w = L (box half size)
    sh_float4 light;        // xyz: direction towards the light (world), w = grid step on surfaces
    sh_uint gridN;          // surface grid resolution (points per side)
    sh_uint paramLines;     // u/v lines across a parametric surface's domain
    sh_uint pad1, pad2;
};

struct SurfaceGridUniforms {
    float halfSize;         // L
    sh_uint gridN;
    sh_uint count;          // surfaces in this dispatch
    sh_uint pad;
};

// Programs of one grid surface. Explicit: z = f(x, y) over the box. Parametric: (f, g, h)(u, v)
// over the (u, v) domain.
#define SURFACE_EXPLICIT 0
#define SURFACE_PARAMETRIC 1
struct SurfaceProgram3D {
    sh_uint4 code;          // code offsets of f, g, h; w = SURFACE_* kind
    sh_uint4 length;        // code lengths of f, g, h
    sh_uint4 consts;        // const offsets of f, g, h
    sh_float4 domain;       // parametric: u0, u1, v0, v1
};

struct SurfaceStyle {
    sh_float4 color;        // rgb + opacity
    sh_uint base;           // first vertex of this surface in the positions buffer
    sh_uint kind;           // SURFACE_*: explicit surfaces get world grid lines, parametric u/v lines
    sh_uint pad1, pad2;
};

// ---- implicit 3D surfaces F(x, y, z) = 0: field grid -> surface nets mesh ----

#define MESH_INVALID 0xFFFFFFFFu

struct Implicit3DUniforms {
    sh_float4 boxMin;       // xyz: first grid point (world)
    sh_float4 boxMax;       // xyz: last grid point (world)
    sh_uint n;              // grid points per side
    sh_uint surface;        // index into the mesh style array
    sh_uint codeOffset, codeLength;
    sh_uint constOffset;
    sh_uint translucent;    // quads go to the translucent index list
    sh_uint vertexCapacity;
    sh_uint indexCapacity;  // per list (opaque / translucent): see Renderer3D
};

// One surface-nets vertex (shared by up to 4 quads).
struct MeshVertex3D {
    sh_float4 pos;          // xyz world, w = style index (as float)
    sh_float4 normal;       // xyz unit normal (field gradient direction)
};

// Counters written by the mesh kernels (one set per frame slot).
#define MESH_COUNT_VERTICES 0
#define MESH_COUNT_OPAQUE 1      // indices in the opaque list
#define MESH_COUNT_TRANSLUCENT 2 // indices in the translucent list

// A depth-tested 3D line segment (a == b draws a round point).
struct Line3D {
    sh_float4 a;            // xyz world, w = width (points)
    sh_float4 b;            // xyz world
    sh_float4 color;        // straight RGBA
};
