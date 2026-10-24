#pragma once
// Metal Performance Shaders, reached through Objective-C++ (MPS has no C++ binding).
// Used only where it beats CPU code: large matrix products for regression (see Regression.cpp).
#include <vector>

namespace calc::mps {

bool available();

// Gram matrix G = AᵀA (k×k) and h = Aᵀb for row-major A (n×k), computed with
// MPSMatrixMultiplication in float32. Returns false if MPS isn't usable.
bool gram(const std::vector<float>& A, const std::vector<float>& b, int n, int k,
          std::vector<double>& G, std::vector<double>& h);

} // namespace calc::mps
