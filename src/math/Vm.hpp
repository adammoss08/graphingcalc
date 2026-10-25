#pragma once
// Compiled expression + CPU (double precision) interpreter.
#include "math/Bytecode.hpp"

#include <cstdint>
#include <vector>

namespace calc {

struct Program {
    std::vector<uint32_t> code;
    std::vector<double> consts;
    int maxStack = 0;
    bool usesX = false, usesY = false, usesT = false, usesPolar = false;
    bool hasLoops = false;   // sums/products/integrals (batched when bounds agree across lanes)

    bool empty() const { return code.empty(); }
    uint64_t hash() const;
    bool isConstant() const { return !usesX && !usesY && !usesT && !usesPolar; }
};

// Scalar evaluation. `t` is the curve parameter (t for parametric, θ for polar).
double evaluate(const Program& p, double x, double y = 0, double t = 0, const double* params = nullptr);

// Batched evaluation: out[i] = f(xs[i], ys ? ys[i] : y0, ts ? ts[i] : t0). Each instruction runs
// across the whole chunk, so the inner loops vectorise.
void evaluateBatch(const Program& p, int n, const double* xs, const double* ys, const double* ts,
                   double* out, double y0 = 0, double t0 = 0);
// As evaluateBatch, split across all cores when the batch is large enough to pay for it.
void evaluateBatchParallel(const Program& p, int n, const double* xs, const double* ys, const double* ts,
                           double* out, double y0 = 0, double t0 = 0);

} // namespace calc
