#pragma once
// Linear least squares for Desmos-style regressions (y_1 ~ m x_1 + b).
// Small problems run on the CPU in double. Large ones (n·k >= kMpsThreshold) form the normal
// equations with MPS in float32, then iteratively refine in double on the CPU so the result has
// double accuracy despite the float32 Gram matrix.
#include <functional>
#include <vector>

namespace calc {

constexpr long kMpsThreshold = 200000;  // below this, CPU double beats MPS dispatch overhead

struct LeastSquaresResult {
    std::vector<double> params;
    bool ok = false;
    bool usedMps = false;
};

// A is row-major n×k, b has n entries. Solves min |A p - b|.
LeastSquaresResult leastSquares(const std::vector<double>& A, const std::vector<double>& b, int n, int k,
                                bool allowMps = true);

// Nonlinear least squares (Levenberg–Marquardt) for models that aren't linear in their parameters.
// `model(p, f)` writes the n model values for parameters p. Several starting points are tried and
// the best fit kept. JᵀJ comes from the same Gram path as leastSquares (MPS when large).
LeastSquaresResult levenbergMarquardt(int n, int k, const std::function<void(const double*, double*)>& model,
                                      const std::vector<double>& y, bool allowMps = true);

} // namespace calc
