#pragma once
// Scalar math with Desmos semantics, shared by the CPU VM and constant folding.
// shaders/vm.h mirrors these in float; keep the two in sync.
#include "math/Bytecode.hpp"

#include <cmath>
#include <limits>

namespace calc::bi {

// 5-point Gauss-Legendre nodes/weights on [-1, 1] (integrals; shaders/vm.h has the same table).
inline constexpr double kGLNodes[5] = {-0.9061798459386640, -0.5384693101056831, 0.0,
                                       0.5384693101056831, 0.9061798459386640};
inline constexpr double kGLWeights[5] = {0.2369268850561891, 0.4786286704993665, 0.5688888888888889,
                                         0.4786286704993665, 0.2369268850561891};
constexpr double kMaxLoopIterations = 1e6;

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// Desmos: a negative base with an exponent p/q (q odd, in lowest terms) gives a real result.
inline double powReal(double b, double e) {
    if (b >= 0 || std::isnan(b) || std::isnan(e)) return std::pow(b, e);
    if (e == std::floor(e)) return std::pow(b, e);
    for (int q = 3; q <= 99; q += 2) {
        double p = e * q;
        double pr = std::round(p);
        if (std::fabs(p - pr) < 1e-9 * q) {
            double mag = std::pow(-b, e);
            return (long long)pr % 2 == 0 ? mag : -mag;
        }
    }
    return kNaN;
}

// For a constant exponent: is e = p/q with q odd (q <= 99, not an integer)? Sets pOdd.
inline bool oddDenominatorRational(double e, bool& pOdd) {
    if (!std::isfinite(e) || e == std::floor(e)) return false;
    for (int q = 3; q <= 99; q += 2) {
        double p = e * q, pr = std::round(p);
        if (std::fabs(p - pr) < 1e-9 * q) { pOdd = (long long)pr % 2 != 0; return true; }
    }
    return false;
}

inline double nthroot(double n, double x) {
    if (n == 0) return kNaN;
    if (x < 0) {
        double r = std::round(n);
        if (r == n && (long long)r % 2 != 0) return -std::pow(-x, 1.0 / n);
        return kNaN;
    }
    return std::pow(x, 1.0 / n);
}

inline double factorial(double x) {
    if (x == std::floor(x)) {
        if (x < 0) return kNaN;
        if (x > 170) return std::numeric_limits<double>::infinity();
        double r = 1;
        for (int i = 2; i <= int(x); ++i) r *= i;
        return r;
    }
    return std::tgamma(x + 1);
}

inline double mod(double a, double b) {
    if (b == 0) return kNaN;
    return a - b * std::floor(a / b);
}

inline double sign(double x) { return x > 0 ? 1 : x < 0 ? -1 : x == 0 ? 0 : kNaN; }
inline double roundHalfUp(double x) { return std::floor(x + 0.5); }

inline double gcd(double a, double b) {
    a = std::fabs(std::round(a));
    b = std::fabs(std::round(b));
    if (!std::isfinite(a) || !std::isfinite(b)) return kNaN;
    for (int i = 0; b != 0 && i < 200; ++i) { double t = std::fmod(a, b); a = b; b = t; }
    return a;
}

inline double lcm(double a, double b) {
    double g = gcd(a, b);
    return g == 0 ? 0 : std::fabs(std::round(a) * std::round(b)) / g;
}

inline double nCr(double n, double r) {
    n = std::round(n); r = std::round(r);
    if (r < 0 || n < 0 || r > n) return 0;
    if (r > n - r) r = n - r;
    double res = 1;
    for (int i = 1; i <= int(r); ++i) res = res * (n - r + i) / i;
    return std::round(res);
}

inline double nPr(double n, double r) {
    n = std::round(n); r = std::round(r);
    if (r < 0 || n < 0 || r > n) return 0;
    double res = 1;
    for (int i = 0; i < int(r); ++i) res *= n - i;
    return res;
}

inline double cmpResult(bool b) { return b ? 1.0 : 0.0; }

// Applies a unary op (arity 1).
inline double unary(BcOp op, double a) {
    switch (op) {
    case OP_NEG: return -a;
    case OP_SQRT: return std::sqrt(a);
    case OP_ABS: return std::fabs(a);
    case OP_SIGN: return sign(a);
    case OP_FLOOR: return std::floor(a);
    case OP_CEIL: return std::ceil(a);
    case OP_ROUND: return roundHalfUp(a);
    case OP_FACT: return factorial(a);
    case OP_SIN: return std::sin(a);
    case OP_COS: return std::cos(a);
    case OP_TAN: return std::tan(a);
    case OP_SEC: return 1.0 / std::cos(a);
    case OP_CSC: return 1.0 / std::sin(a);
    case OP_COT: return 1.0 / std::tan(a);
    case OP_ASIN: return std::asin(a);
    case OP_ACOS: return std::acos(a);
    case OP_ATAN: return std::atan(a);
    case OP_ASEC: return std::acos(1.0 / a);
    case OP_ACSC: return std::asin(1.0 / a);
    case OP_ACOT: return a == 0 ? M_PI / 2 : std::atan(1.0 / a) + (a < 0 ? M_PI : 0);
    case OP_SINH: return std::sinh(a);
    case OP_COSH: return std::cosh(a);
    case OP_TANH: return std::tanh(a);
    case OP_SECH: return 1.0 / std::cosh(a);
    case OP_CSCH: return 1.0 / std::sinh(a);
    case OP_COTH: return 1.0 / std::tanh(a);
    case OP_ASINH: return std::asinh(a);
    case OP_ACOSH: return std::acosh(a);
    case OP_ATANH: return std::atanh(a);
    case OP_EXP: return std::exp(a);
    case OP_LN: return std::log(a);
    case OP_LOG10: return std::log10(a);
    default: return kNaN;
    }
}

inline double binary(BcOp op, double a, double b) {
    switch (op) {
    case OP_ADD: return a + b;
    case OP_SUB: return a - b;
    case OP_MUL: return a * b;
    case OP_DIV: return a / b;
    case OP_POW: return powReal(a, b);
    case OP_POW_ODD: return a < 0 ? -std::pow(-a, b) : std::pow(a, b);
    case OP_POW_EVEN: return std::pow(std::fabs(a), b);
    case OP_NTHROOT: return nthroot(a, b);
    case OP_ATAN2: return std::atan2(a, b);
    case OP_LOGB: return std::log(b) / std::log(a);
    case OP_MIN: return std::isnan(a) || std::isnan(b) ? kNaN : std::fmin(a, b);
    case OP_MAX: return std::isnan(a) || std::isnan(b) ? kNaN : std::fmax(a, b);
    case OP_MOD: return mod(a, b);
    case OP_HYPOT: return std::hypot(a, b);
    case OP_NCR: return nCr(a, b);
    case OP_NPR: return nPr(a, b);
    case OP_GCD: return gcd(a, b);
    case OP_LCM: return lcm(a, b);
    case OP_LT: return cmpResult(a < b);
    case OP_LE: return cmpResult(a <= b);
    case OP_GT: return cmpResult(a > b);
    case OP_GE: return cmpResult(a >= b);
    case OP_EQ: return cmpResult(a == b);
    case OP_AND: return cmpResult(a != 0 && b != 0 && !std::isnan(a) && !std::isnan(b));
    default: return kNaN;
    }
}

} // namespace calc::bi
