#include "Test.hpp"

#include "gpu/MpsBridge.hpp"
#include "math/Regression.hpp"

#include <random>

using namespace calc;

// Large problem: the MPS (float32 Gram) path plus double refinement must match the CPU double path.
TEST(least_squares_mps_matches_cpu) {
    const int n = 150000, k = 3;
    std::mt19937 rng(42);
    std::normal_distribution<double> noise(0, 0.01);
    std::vector<double> A(size_t(n) * k), b(n);
    for (int i = 0; i < n; ++i) {
        double x = 1000.0 + i * 0.001;  // offset data: badly conditioned unless scaled + refined
        A[size_t(i) * k + 0] = 1;
        A[size_t(i) * k + 1] = x;
        A[size_t(i) * k + 2] = std::sin(x);
        b[i] = 2.5 - 0.75 * x + 3 * std::sin(x) + noise(rng);
    }
    LeastSquaresResult cpu = leastSquares(A, b, n, k, false);
    LeastSquaresResult gpu = leastSquares(A, b, n, k, true);
    CHECK(cpu.ok && gpu.ok);
    CHECK(!cpu.usedMps);
    CHECK(gpu.usedMps == mps::available());
    for (int j = 0; j < k; ++j) CHECK_NEAR(gpu.params[j], cpu.params[j], 1e-6 * (1 + std::fabs(cpu.params[j])));
    CHECK_NEAR(cpu.params[1], -0.75, 1e-3);
}
