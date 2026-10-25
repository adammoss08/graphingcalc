#pragma once
// Orbit camera for 3D mode. World space is maths space (x right, y away, z up). The visible box is
// [-L, L]^3 (L = halfSize); zooming changes L, as in Desmos 3D, so surfaces are re-sampled over the
// new box. The model matrix scales the box into a unit cube that the camera orbits.
#include <simd/simd.h>

namespace calc {

struct Camera3D {
    double azimuth = -0.95;      // radians around z (camera starts at +x, -y)
    double elevation = 0.42;     // radians above the xy-plane
    double halfSize = 10;        // L: the box is [-L, L]^3
    static constexpr double kDistance = 4.6;   // camera distance from the origin (unit-cube space)
    static constexpr double kFovY = 0.62;      // radians

    void reset() { *this = Camera3D{}; }
    void orbit(double dAzimuth, double dElevation);
    void zoom(double factor) { halfSize /= factor; }   // factor > 1 zooms in

    // viewProj * model for a viewport of the given aspect (width / height).
    simd_float4x4 matrix(float aspect) const;
    simd_float3 eye() const;      // camera position in world units

    // Projects a world point into a w × h area (points, y down). Returns false if behind the camera.
    bool project(double x, double y, double z, float w, float h, float& sx, float& sy, float& depth) const;
};

} // namespace calc
