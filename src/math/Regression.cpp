#include "math/Regression.hpp"
#include "gpu/MpsBridge.hpp"

#include <cmath>

namespace calc {

namespace {

// In-place Cholesky solve of the k×k SPD system G x = h. Returns false if G isn't positive definite.
bool choleskySolve(std::vector<double> G, const std::vector<double>& h, int k, std::vector<double>& x) {
    for (int j = 0; j < k; ++j) {
        double d = G[j * k + j];
        for (int p = 0; p < j; ++p) d -= G[j * k + p] * G[j * k + p];
        if (!(d > 0)) return false;
        d = std::sqrt(d);
        G[j * k + j] = d;
        for (int i = j + 1; i < k; ++i) {
            double s = G[i * k + j];
            for (int p = 0; p < j; ++p) s -= G[i * k + p] * G[j * k + p];
            G[i * k + j] = s / d;
        }
    }
    std::vector<double> y(k);
    for (int i = 0; i < k; ++i) {
        double s = h[i];
        for (int p = 0; p < i; ++p) s -= G[i * k + p] * y[p];
        y[i] = s / G[i * k + i];
    }
    x.assign(k, 0);
    for (int i = k - 1; i >= 0; --i) {
        double s = y[i];
        for (int p = i + 1; p < k; ++p) s -= G[p * k + i] * x[p];
        x[i] = s / G[i * k + i];
    }
    return true;
}

// G = AᵀA, h = Aᵀb (A row-major n×k), using MPS for large problems.
void gram(const std::vector<double>& A, const std::vector<double>& b, int n, int k, bool allowMps,
          std::vector<double>& G, std::vector<double>& h, bool& usedMps) {
    usedMps = false;
    if (allowMps && long(n) * k >= kMpsThreshold && mps::available()) {
        std::vector<float> Af(A.begin(), A.end()), bf(b.begin(), b.end());
        if (mps::gram(Af, bf, n, k, G, h)) { usedMps = true; return; }
    }
    G.assign(size_t(k) * k, 0);
    h.assign(k, 0);
    for (int i = 0; i < n; ++i)
        for (int r = 0; r < k; ++r) {
            double ar = A[size_t(i) * k + r];
            h[r] += ar * b[i];
            for (int c = 0; c <= r; ++c) G[r * k + c] += ar * A[size_t(i) * k + c];
        }
    for (int r = 0; r < k; ++r)
        for (int c = r + 1; c < k; ++c) G[r * k + c] = G[c * k + r];
}

} // namespace

LeastSquaresResult levenbergMarquardt(int n, int k, const std::function<void(const double*, double*)>& model,
                                      const std::vector<double>& y, bool allowMps) {
    LeastSquaresResult best;
    double bestSse = INFINITY;
    std::vector<double> f(n), fp(n), fm(n), r(n), J(size_t(n) * k);
    auto sse = [&](const std::vector<double>& p) {
        model(p.data(), f.data());
        double s = 0;
        for (int i = 0; i < n; ++i) {
            double d = y[i] - f[i];
            if (!std::isfinite(d)) return double(INFINITY);
            s += d * d;
        }
        return s;
    };
    for (double start : {1.0, 0.1, -1.0, 2.0}) {
        std::vector<double> p(k, start);
        double cur = sse(p);
        if (!std::isfinite(cur)) continue;
        double lambda = 1e-3;
        bool mpsUsed = false;
        for (int it = 0; it < 300; ++it) {
            model(p.data(), f.data());
            for (int i = 0; i < n; ++i) r[i] = y[i] - f[i];
            // Jacobian by central differences.
            for (int j = 0; j < k; ++j) {
                double h = 1e-6 * (1 + std::fabs(p[j]));
                std::vector<double> q = p;
                q[j] = p[j] + h;
                model(q.data(), fp.data());
                q[j] = p[j] - h;
                model(q.data(), fm.data());
                for (int i = 0; i < n; ++i) J[size_t(i) * k + j] = (fp[i] - fm[i]) / (2 * h);
            }
            std::vector<double> G, g;
            bool used;
            gram(J, r, n, k, allowMps, G, g, used);
            mpsUsed |= used;
            bool improved = false;
            for (int tries = 0; tries < 12; ++tries) {
                std::vector<double> A = G;
                for (int j = 0; j < k; ++j) A[j * k + j] += lambda * std::max(G[j * k + j], 1e-12);
                std::vector<double> d;
                if (!choleskySolve(A, g, k, d)) { lambda *= 10; continue; }
                std::vector<double> q(k);
                for (int j = 0; j < k; ++j) q[j] = p[j] + d[j];
                double next = sse(q);
                if (next < cur) {
                    double rel = (cur - next) / std::max(cur, 1e-300);
                    p = q;
                    cur = next;
                    lambda = std::max(lambda / 3, 1e-12);
                    improved = true;
                    if (rel < 1e-15) it = 1 << 20;  // converged
                    break;
                }
                lambda *= 4;
            }
            if (!improved || cur < 1e-28) break;
        }
        if (cur < bestSse) {
            bestSse = cur;
            best.params = p;
            best.ok = true;
            best.usedMps = mpsUsed;
        }
    }
    return best;
}

LeastSquaresResult leastSquares(const std::vector<double>& A, const std::vector<double>& b, int n, int k,
                                bool allowMps) {
    LeastSquaresResult res;
    if (n < k || k <= 0) return res;

    // Column scaling improves conditioning (and keeps float32 in range for MPS).
    std::vector<double> colScale(k, 1.0);
    for (int j = 0; j < k; ++j) {
        double m = 0;
        for (int i = 0; i < n; ++i) m = std::fmax(m, std::fabs(A[size_t(i) * k + j]));
        colScale[j] = m > 0 ? 1.0 / m : 1.0;
    }
    auto As = [&](int i, int j) { return A[size_t(i) * k + j] * colScale[j]; };

    std::vector<double> G, h;
    bool mpsUsed = false;
    if (allowMps && long(n) * k >= kMpsThreshold && mps::available()) {
        std::vector<float> Af(size_t(n) * k), bf(n);
        double bScale = 0;
        for (int i = 0; i < n; ++i) bScale = std::fmax(bScale, std::fabs(b[i]));
        bScale = bScale > 0 ? 1.0 / bScale : 1.0;
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < k; ++j) Af[size_t(i) * k + j] = float(As(i, j));
            bf[i] = float(b[i] * bScale);
        }
        if (mps::gram(Af, bf, n, k, G, h)) {
            for (double& v : h) v /= bScale;
            mpsUsed = true;
        }
    }
    if (!mpsUsed) {
        G.assign(size_t(k) * k, 0);
        h.assign(k, 0);
        for (int i = 0; i < n; ++i)
            for (int r = 0; r < k; ++r) {
                double ar = As(i, r);
                h[r] += ar * b[i];
                for (int c = 0; c <= r; ++c) G[r * k + c] += ar * As(i, c);
            }
        for (int r = 0; r < k; ++r)
            for (int c = r + 1; c < k; ++c) G[r * k + c] = G[c * k + r];
    }

    std::vector<double> p;
    if (!choleskySolve(G, h, k, p)) return res;
    if (mpsUsed) {
        // Mixed-precision iterative refinement: residual and Aᵀr in double, correction via G.
        for (int it = 0; it < 4; ++it) {
            std::vector<double> g(k, 0);
            for (int i = 0; i < n; ++i) {
                double r = b[i];
                for (int j = 0; j < k; ++j) r -= As(i, j) * p[j];
                for (int j = 0; j < k; ++j) g[j] += As(i, j) * r;
            }
            std::vector<double> dp;
            if (!choleskySolve(G, g, k, dp)) break;
            double maxRel = 0;
            for (int j = 0; j < k; ++j) {
                p[j] += dp[j];
                maxRel = std::fmax(maxRel, std::fabs(dp[j]) / std::fmax(std::fabs(p[j]), 1e-300));
            }
            if (maxRel < 1e-14) break;
        }
    }
    for (int j = 0; j < k; ++j) p[j] *= colScale[j];
    res.params = std::move(p);
    res.ok = true;
    res.usedMps = mpsUsed;
    return res;
}

} // namespace calc
