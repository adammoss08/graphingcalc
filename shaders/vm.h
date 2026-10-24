// GPU bytecode interpreter (float). Mirrors src/math/Vm.cpp + Builtins.hpp; keep them in sync.
#pragma once
#include <metal_stdlib>
#include "math/Bytecode.hpp"
using namespace metal;

namespace gvm {

constant float kNaN = NAN;
constant float kGLNodes[5] = {-0.9061798459386640f, -0.5384693101056831f, 0.0f, 0.5384693101056831f, 0.9061798459386640f};
constant float kGLWeights[5] = {0.2369268850561891f, 0.4786286704993665f, 0.5688888888888889f, 0.4786286704993665f, 0.2369268850561891f};
constant float kGpuMaxLoopIterations = 10000.0f;
constant float kPi = 3.14159265358979f;

// Variable exponents: real only for integer exponents of negative bases. (Constant rational
// exponents are compiled to OP_POW_ODD / OP_POW_EVEN, which handle negative bases exactly.)
inline float powReal(float b, float e) {
    if (b >= 0 || isnan(b) || isnan(e)) return precise::pow(b, e);
    if (e == floor(e)) {
        float m = precise::pow(-b, e);
        return fmod(fabs(e), 2.0f) == 1.0f ? -m : m;
    }
    return kNaN;
}

inline float nthroot(float n, float x) {
    if (n == 0) return kNaN;
    if (x < 0) {
        float r = rint(n);
        if (r == n && fmod(fabs(r), 2.0f) == 1.0f) return -precise::pow(-x, 1.0f / n);
        return kNaN;
    }
    return precise::pow(x, 1.0f / n);
}

inline float lgammaApprox(float x) {
    // Stirling series, x > 0
    float x2 = x * x;
    return (x - 0.5f) * log(x) - x + 0.918938533f + 1.0f / (12.0f * x) - 1.0f / (360.0f * x * x2);
}

inline float gammaf(float x) {
    // Shift into the accurate range, then use Stirling.
    if (x <= 0 && x == floor(x)) return kNaN;
    float acc = 1;
    if (x < 0) {
        // reflection: Γ(x) = π / (sin(πx) Γ(1-x))
        float g = exp(lgammaApprox(1 - x + 8));
        for (int i = 0; i < 8; ++i) g /= (1 - x + float(i));
        return kPi / (sin(kPi * x) * g);
    }
    while (x < 8) { acc *= x; x += 1; }
    return exp(lgammaApprox(x)) / acc;
}

inline float factorial(float x) {
    if (x == floor(x)) {
        if (x < 0) return kNaN;
        if (x > 34) return INFINITY;
        float r = 1;
        for (int i = 2; i <= int(x); ++i) r *= float(i);
        return r;
    }
    return gammaf(x + 1);
}

inline float modDesmos(float a, float b) { return b == 0 ? kNaN : a - b * floor(a / b); }
inline float signf(float x) { return x > 0 ? 1.0f : x < 0 ? -1.0f : x == 0 ? 0.0f : kNaN; }

inline float gcdf(float a, float b) {
    a = fabs(rint(a)); b = fabs(rint(b));
    for (int i = 0; i < 64 && b != 0; ++i) { float t = fmod(a, b); a = b; b = t; }
    return a;
}

inline float ncr(float n, float r) {
    n = rint(n); r = rint(r);
    if (r < 0 || n < 0 || r > n) return 0;
    r = min(r, n - r);
    float res = 1;
    for (int i = 1; i <= int(r) && i < 64; ++i) res = res * (n - r + float(i)) / float(i);
    return rint(res);
}

inline float npr(float n, float r) {
    n = rint(n); r = rint(r);
    if (r < 0 || n < 0 || r > n) return 0;
    float res = 1;
    for (int i = 0; i < int(r) && i < 64; ++i) res *= n - float(i);
    return res;
}

inline float unaryOp(uint op, float a) {
    switch (op) {
    case OP_NEG: return -a;
    case OP_SQRT: return precise::sqrt(a);
    case OP_ABS: return fabs(a);
    case OP_SIGN: return signf(a);
    case OP_FLOOR: return floor(a);
    case OP_CEIL: return ceil(a);
    case OP_ROUND: return floor(a + 0.5f);
    case OP_FACT: return factorial(a);
    case OP_SIN: return precise::sin(a);
    case OP_COS: return precise::cos(a);
    case OP_TAN: return precise::tan(a);
    case OP_SEC: return 1.0f / precise::cos(a);
    case OP_CSC: return 1.0f / precise::sin(a);
    case OP_COT: return 1.0f / precise::tan(a);
    case OP_ASIN: return precise::asin(a);
    case OP_ACOS: return precise::acos(a);
    case OP_ATAN: return precise::atan(a);
    case OP_ASEC: return precise::acos(1.0f / a);
    case OP_ACSC: return precise::asin(1.0f / a);
    case OP_ACOT: return a == 0 ? kPi / 2 : precise::atan(1.0f / a) + (a < 0 ? kPi : 0.0f);
    case OP_SINH: return precise::sinh(a);
    case OP_COSH: return precise::cosh(a);
    case OP_TANH: return precise::tanh(a);
    case OP_SECH: return 1.0f / precise::cosh(a);
    case OP_CSCH: return 1.0f / precise::sinh(a);
    case OP_COTH: return 1.0f / precise::tanh(a);
    case OP_ASINH: return precise::asinh(a);
    case OP_ACOSH: return precise::acosh(a);
    case OP_ATANH: return precise::atanh(a);
    case OP_EXP: return precise::exp(a);
    case OP_LN: return precise::log(a);
    case OP_LOG10: return precise::log10(a);
    default: return kNaN;
    }
}

inline float binaryOp(uint op, float a, float b) {
    switch (op) {
    case OP_POW: return powReal(a, b);
    case OP_POW_ODD: return a < 0 ? -precise::pow(-a, b) : precise::pow(a, b);
    case OP_POW_EVEN: return precise::pow(fabs(a), b);
    case OP_NTHROOT: return nthroot(a, b);
    case OP_ATAN2: return precise::atan2(a, b);
    case OP_LOGB: return precise::log(b) / precise::log(a);
    case OP_MIN: return isnan(a) || isnan(b) ? kNaN : min(a, b);
    case OP_MAX: return isnan(a) || isnan(b) ? kNaN : max(a, b);
    case OP_MOD: return modDesmos(a, b);
    case OP_HYPOT: return precise::sqrt(a * a + b * b);
    case OP_NCR: return ncr(a, b);
    case OP_NPR: return npr(a, b);
    case OP_GCD: return gcdf(a, b);
    case OP_LCM: { float g = gcdf(a, b); return g == 0 ? 0.0f : fabs(rint(a) * rint(b)) / g; }
    case OP_LT: return a < b ? 1.0f : 0.0f;
    case OP_LE: return a <= b ? 1.0f : 0.0f;
    case OP_GT: return a > b ? 1.0f : 0.0f;
    case OP_GE: return a >= b ? 1.0f : 0.0f;
    case OP_EQ: return a == b ? 1.0f : 0.0f;
    case OP_AND: return (a != 0 && b != 0 && !isnan(a) && !isnan(b)) ? 1.0f : 0.0f;
    default: return kNaN;
    }
}

inline float run(device const uint* code, uint len, device const float* consts, float x, float y) {
    float stack[kBcMaxStack];
    float loopI[kBcMaxLoops], loopHi[kBcMaxLoops], loopAcc[kBcMaxLoops], loopA[kBcMaxLoops], loopB[kBcMaxLoops];
    int loopK[kBcMaxLoops];
    int sp = 0;
    for (uint pc = 0; pc < len; ++pc) {
        uint ins = code[pc];
        uint op = bcOpOf(ins);
        switch (op) {
        case OP_CONST: stack[sp++] = consts[bcArgOf(ins)]; break;
        case OP_X: stack[sp++] = x; break;
        case OP_Y: stack[sp++] = y; break;
        case OP_T: stack[sp++] = 0; break;
        case OP_R: stack[sp++] = precise::sqrt(x * x + y * y); break;
        case OP_THETA: stack[sp++] = precise::atan2(y, x); break;
        case OP_NAN: case OP_PARAM: stack[sp++] = kNaN; break;  // OP_PARAM is CPU-only
        case OP_LIST_INDEX: {
            device const float* block = consts + bcArgOf(ins);
            float idx = stack[sp - 1];
            float k = rint(idx);
            stack[sp - 1] = (isfinite(idx) && k >= 1 && k <= block[0]) ? block[uint(k)] : kNaN;
            break;
        }
        case OP_ADD: --sp; stack[sp - 1] += stack[sp]; break;
        case OP_SUB: --sp; stack[sp - 1] -= stack[sp]; break;
        case OP_MUL: --sp; stack[sp - 1] *= stack[sp]; break;
        case OP_DIV: --sp; stack[sp - 1] /= stack[sp]; break;
        case OP_NEG: stack[sp - 1] = -stack[sp - 1]; break;
        case OP_SELECT: sp -= 2; stack[sp - 1] = stack[sp - 1] != 0 ? stack[sp] : stack[sp + 1]; break;
        case OP_LOOP_BEGIN: {
            uint arg = bcArgOf(ins), lv = arg & 3u, kind = (arg >> 2) & 3u, blen = arg >> 4;
            float hi = stack[--sp], lo = stack[--sp];
            float empty;
            bool enter = false;
            if (kind < 2) {
                float start = ceil(lo - 1e-6f), end = floor(hi + 1e-6f);
                if (!isfinite(start) || !isfinite(end) || end - start > kGpuMaxLoopIterations) empty = kNaN;
                else if (start > end) empty = kind == 0 ? 0.0f : 1.0f;
                else { loopI[lv] = start; loopHi[lv] = end; loopAcc[lv] = kind == 0 ? 0.0f : 1.0f; enter = true; empty = 0; }
            } else {
                if (!isfinite(lo) || !isfinite(hi)) empty = kNaN;
                else if (lo == hi) empty = 0;
                else { loopA[lv] = lo; loopB[lv] = hi; loopK[lv] = 0; loopAcc[lv] = 0; enter = true; empty = 0; }
            }
            if (!enter) { stack[sp++] = empty; pc += blen + 1; }
            break;
        }
        case OP_LOOP_IDX: {
            uint arg = bcArgOf(ins), lv = arg & 3u, kind = (arg >> 2) & 3u;
            if (kind == 2) {
                int panel = loopK[lv] / 5, j = loopK[lv] % 5;
                float h = (loopB[lv] - loopA[lv]) / float(kBcIntegralPanels);
                stack[sp++] = loopA[lv] + h * (float(panel) + 0.5f * (kGLNodes[j] + 1.0f));
            } else {
                stack[sp++] = loopI[lv];
            }
            break;
        }
        case OP_LOOP_END: {
            uint arg = bcArgOf(ins), lv = arg & 3u, kind = (arg >> 2) & 3u, blen = arg >> 4;
            float v = stack[--sp];
            bool done;
            if (kind == 0) { loopAcc[lv] += v; loopI[lv] += 1; done = loopI[lv] > loopHi[lv]; }
            else if (kind == 1) { loopAcc[lv] *= v; loopI[lv] += 1; done = loopI[lv] > loopHi[lv]; }
            else { loopAcc[lv] += kGLWeights[loopK[lv] % 5] * v; loopK[lv] += 1; done = loopK[lv] >= 5 * kBcIntegralPanels; }
            if (!done) { pc -= blen + 1; break; }
            stack[sp++] = kind == 2 ? loopAcc[lv] * (loopB[lv] - loopA[lv]) / (2.0f * float(kBcIntegralPanels)) : loopAcc[lv];
            break;
        }
        default:
            if (op >= OP_SQRT && op <= OP_LOG10 && op != OP_NTHROOT && op != OP_ATAN2 && op != OP_LOGB) {
                stack[sp - 1] = unaryOp(op, stack[sp - 1]);
            } else {
                --sp;
                stack[sp - 1] = binaryOp(op, stack[sp - 1], stack[sp]);
            }
            break;
        }
    }
    return sp > 0 ? stack[sp - 1] : kNaN;
}

} // namespace gvm
