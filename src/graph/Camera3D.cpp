#include "graph/Camera3D.hpp"

#include <algorithm>
#include <cmath>

namespace calc {

namespace {

simd_float4x4 perspective(float fovy, float aspect, float nearZ, float farZ) {
    float ys = 1.0f / std::tan(fovy * 0.5f), xs = ys / aspect, zs = farZ / (nearZ - farZ);
    return simd_matrix(simd_make_float4(xs, 0, 0, 0), simd_make_float4(0, ys, 0, 0),
                       simd_make_float4(0, 0, zs, -1), simd_make_float4(0, 0, zs * nearZ, 0));
}

simd_float4x4 lookAt(simd_float3 eye, simd_float3 center, simd_float3 up) {
    simd_float3 f = simd_normalize(center - eye);
    simd_float3 s = simd_normalize(simd_cross(f, up));
    simd_float3 u = simd_cross(s, f);
    return simd_matrix(simd_make_float4(s.x, u.x, -f.x, 0), simd_make_float4(s.y, u.y, -f.y, 0),
                       simd_make_float4(s.z, u.z, -f.z, 0),
                       simd_make_float4(-simd_dot(s, eye), -simd_dot(u, eye), simd_dot(f, eye), 1));
}

} // namespace

void Camera3D::orbit(double dAzimuth, double dElevation) {
    azimuth += dAzimuth;
    elevation = std::clamp(elevation + dElevation, -1.55, 1.55);
}

simd_float3 Camera3D::eye() const {
    double c = std::cos(elevation);
    return simd_make_float3(float(kDistance * c * std::cos(azimuth)), float(kDistance * c * std::sin(azimuth)),
                            float(kDistance * std::sin(elevation)));
}

simd_float4x4 Camera3D::matrix(float aspect) const {
    simd_float4x4 proj = perspective(float(kFovY), aspect, 0.1f, 20.0f);
    simd_float4x4 view = lookAt(eye(), simd_make_float3(0, 0, 0), simd_make_float3(0, 0, 1));
    float k = float(1.0 / halfSize);
    simd_float4x4 model = simd_matrix(simd_make_float4(k, 0, 0, 0), simd_make_float4(0, k, 0, 0),
                                      simd_make_float4(0, 0, k, 0), simd_make_float4(0, 0, 0, 1));
    return simd_mul(proj, simd_mul(view, model));
}

bool Camera3D::project(double x, double y, double z, float w, float h, float& sx, float& sy, float& depth) const {
    simd_float4 c = simd_mul(matrix(w / h), simd_make_float4(float(x), float(y), float(z), 1));
    if (c.w <= 1e-4f) return false;
    sx = (c.x / c.w * 0.5f + 0.5f) * w;
    sy = (0.5f - c.y / c.w * 0.5f) * h;
    depth = c.z / c.w;
    return true;
}

} // namespace calc
