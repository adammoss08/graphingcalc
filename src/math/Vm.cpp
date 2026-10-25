#include "math/Vm.hpp"
#include "math/Builtins.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <dispatch/dispatch.h>

namespace calc {

uint64_t Program::hash() const {
    // FNV-1a over code and constant bit patterns.
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* data, size_t len) {
        auto* b = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < len; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    mix(code.data(), code.size() * sizeof(uint32_t));
    mix(consts.data(), consts.size() * sizeof(double));
    return h;
}

double evaluate(const Program& p, double x, double y, double t, const double* params) {
    struct Loop { double i, hi, acc, a, b; int k; };
    double stack[kBcMaxStack];
    Loop loops[kBcMaxLoops];
    int sp = 0;
    const uint32_t* code = p.code.data();
    const size_t n = p.code.size();
    for (size_t pc = 0; pc < n; ++pc) {
        uint32_t ins = code[pc];
        BcOp op = bcOpOf(ins);
        switch (op) {
        case OP_CONST: stack[sp++] = p.consts[bcArgOf(ins)]; break;
        case OP_X: stack[sp++] = x; break;
        case OP_Y: stack[sp++] = y; break;
        case OP_T: stack[sp++] = t; break;
        case OP_R: stack[sp++] = std::hypot(x, y); break;
        case OP_THETA: stack[sp++] = std::atan2(y, x); break;
        case OP_NAN: stack[sp++] = bi::kNaN; break;
        case OP_PARAM: stack[sp++] = params ? params[bcArgOf(ins)] : bi::kNaN; break;
        case OP_LIST_INDEX: stack[sp - 1] = bi::listIndex(p.consts.data() + bcArgOf(ins), stack[sp - 1]); break;
        case OP_ADD: --sp; stack[sp - 1] += stack[sp]; break;
        case OP_SUB: --sp; stack[sp - 1] -= stack[sp]; break;
        case OP_MUL: --sp; stack[sp - 1] *= stack[sp]; break;
        case OP_DIV: --sp; stack[sp - 1] /= stack[sp]; break;
        case OP_NEG: stack[sp - 1] = -stack[sp - 1]; break;
        case OP_SELECT: sp -= 2; stack[sp - 1] = stack[sp - 1] != 0 ? stack[sp] : stack[sp + 1]; break;
        case OP_LOOP_BEGIN: {
            uint32_t arg = bcArgOf(ins), level = arg & 3, kind = (arg >> 2) & 3, len = arg >> 4;
            double hi = stack[--sp], lo = stack[--sp];
            Loop& L = loops[level];
            double empty;
            if (kind < 2) {
                double start = std::ceil(lo - 1e-9), end = std::floor(hi + 1e-9);
                if (!std::isfinite(start) || !std::isfinite(end) || end - start > bi::kMaxLoopIterations) empty = bi::kNaN;
                else if (start > end) empty = kind == 0 ? 0.0 : 1.0;
                else { L = {start, end, kind == 0 ? 0.0 : 1.0, 0, 0, 0}; break; }
            } else {
                if (!std::isfinite(lo) || !std::isfinite(hi)) empty = bi::kNaN;
                else if (lo == hi) empty = 0;
                else { L = {0, 0, 0, lo, hi, 0}; break; }
            }
            stack[sp++] = empty;
            pc += len + 1;  // skip body and END
            break;
        }
        case OP_LOOP_IDX: {
            uint32_t arg = bcArgOf(ins), level = arg & 3, kind = (arg >> 2) & 3;
            const Loop& L = loops[level];
            // Sums store i; integrals compute the quadrature node from k.
            if (kind == 2) {
                int panel = L.k / 5, j = L.k % 5;
                double h = (L.b - L.a) / kBcIntegralPanels;
                stack[sp++] = L.a + h * (panel + 0.5 * (bi::kGLNodes[j] + 1));
            } else {
                stack[sp++] = L.i;
            }
            break;
        }
        case OP_LOOP_END: {
            uint32_t arg = bcArgOf(ins), level = arg & 3, kind = (arg >> 2) & 3, len = arg >> 4;
            double v = stack[--sp];
            Loop& L = loops[level];
            bool done;
            if (kind == 0) { L.acc += v; done = ++L.i > L.hi; }
            else if (kind == 1) { L.acc *= v; done = ++L.i > L.hi; }
            else { L.acc += bi::kGLWeights[L.k % 5] * v; done = ++L.k >= 5 * kBcIntegralPanels; }
            if (!done) { pc -= len + 1; break; }
            stack[sp++] = kind == 2 ? L.acc * (L.b - L.a) / (2.0 * kBcIntegralPanels) : L.acc;
            break;
        }
        default:
            if (bcArity(op) == 1) stack[sp - 1] = bi::unary(op, stack[sp - 1]);
            else { --sp; stack[sp - 1] = bi::binary(op, stack[sp - 1], stack[sp]); }
            break;
        }
    }
    return sp ? stack[sp - 1] : bi::kNaN;
}

namespace {

constexpr int kChunk = 256;

// One chunk (m <= kChunk lanes). Returns false if a sum/product's bounds differ between lanes (the
// caller then evaluates that chunk per lane). Integrals batch even with per-lane limits (∫₀ˣ).
bool evaluateChunk(const Program& p, int m, const double* cx, const double* cy, const double* ct,
                   double* out, double y0, double t0) {
    struct Loop {
        double i, hi;              // sums / products: shared index
        int k;                     // integrals: shared quadrature node counter
        double acc[kChunk], a[kChunk], b[kChunk];
    };
    double stack[kBcMaxStack][kChunk];
    Loop loops[kBcMaxLoops];
    int sp = 0;
    const uint32_t* code = p.code.data();
    const size_t n = p.code.size();
    for (size_t pc = 0; pc < n; ++pc) {
        uint32_t ins = code[pc];
        BcOp op = bcOpOf(ins);
        double* top = stack[sp > 0 ? sp - 1 : 0];
        switch (op) {
        case OP_CONST: { double v = p.consts[bcArgOf(ins)]; double* d = stack[sp++]; for (int i = 0; i < m; ++i) d[i] = v; break; }
        case OP_X: { double* d = stack[sp++]; if (cx) std::memcpy(d, cx, m * sizeof(double)); else std::fill(d, d + m, 0.0); break; }
        case OP_Y: { double* d = stack[sp++]; if (cy) std::memcpy(d, cy, m * sizeof(double)); else std::fill(d, d + m, y0); break; }
        case OP_T: { double* d = stack[sp++]; if (ct) std::memcpy(d, ct, m * sizeof(double)); else std::fill(d, d + m, t0); break; }
        case OP_R: { double* d = stack[sp++]; for (int i = 0; i < m; ++i) d[i] = std::hypot(cx ? cx[i] : 0, cy ? cy[i] : y0); break; }
        case OP_THETA: { double* d = stack[sp++]; for (int i = 0; i < m; ++i) d[i] = std::atan2(cy ? cy[i] : y0, cx ? cx[i] : 0); break; }
        case OP_NAN: case OP_PARAM: { double* d = stack[sp++]; std::fill(d, d + m, bi::kNaN); break; }
        case OP_ADD: { --sp; double* a = stack[sp - 1]; double* b = stack[sp]; for (int i = 0; i < m; ++i) a[i] += b[i]; break; }
        case OP_SUB: { --sp; double* a = stack[sp - 1]; double* b = stack[sp]; for (int i = 0; i < m; ++i) a[i] -= b[i]; break; }
        case OP_MUL: { --sp; double* a = stack[sp - 1]; double* b = stack[sp]; for (int i = 0; i < m; ++i) a[i] *= b[i]; break; }
        case OP_DIV: { --sp; double* a = stack[sp - 1]; double* b = stack[sp]; for (int i = 0; i < m; ++i) a[i] /= b[i]; break; }
        case OP_NEG: for (int i = 0; i < m; ++i) top[i] = -top[i]; break;
        case OP_LIST_INDEX: {
            const double* block = p.consts.data() + bcArgOf(ins);
            for (int i = 0; i < m; ++i) top[i] = bi::listIndex(block, top[i]);
            break;
        }
        case OP_SIN: for (int i = 0; i < m; ++i) top[i] = std::sin(top[i]); break;
        case OP_COS: for (int i = 0; i < m; ++i) top[i] = std::cos(top[i]); break;
        case OP_SQRT: for (int i = 0; i < m; ++i) top[i] = std::sqrt(top[i]); break;
        case OP_SELECT: {
            sp -= 2;
            double* c = stack[sp - 1]; double* a = stack[sp]; double* b = stack[sp + 1];
            for (int i = 0; i < m; ++i) c[i] = c[i] != 0 ? a[i] : b[i];
            break;
        }
        case OP_LOOP_BEGIN: {
            uint32_t arg = bcArgOf(ins), level = arg & 3, kind = (arg >> 2) & 3, len = arg >> 4;
            sp -= 2;
            const double* lo = stack[sp];
            const double* hi = stack[sp + 1];
            Loop& L = loops[level];
            if (kind < 2) {
                double start = std::ceil(lo[0] - 1e-9), end = std::floor(hi[0] + 1e-9);
                for (int i = 1; i < m; ++i)
                    if (std::ceil(lo[i] - 1e-9) != start || std::floor(hi[i] + 1e-9) != end) return false;
                double empty;
                if (!std::isfinite(start) || !std::isfinite(end) || end - start > bi::kMaxLoopIterations) empty = bi::kNaN;
                else if (start > end) empty = kind == 0 ? 0.0 : 1.0;
                else {
                    L.i = start;
                    L.hi = end;
                    std::fill(L.acc, L.acc + m, kind == 0 ? 0.0 : 1.0);
                    break;
                }
                double* d = stack[sp++];
                std::fill(d, d + m, empty);
                pc += len + 1;  // skip body and END
            } else {
                std::memcpy(L.a, lo, m * sizeof(double));
                std::memcpy(L.b, hi, m * sizeof(double));
                std::fill(L.acc, L.acc + m, 0.0);
                L.k = 0;
            }
            break;
        }
        case OP_LOOP_IDX: {
            uint32_t arg = bcArgOf(ins), level = arg & 3, kind = (arg >> 2) & 3;
            const Loop& L = loops[level];
            double* d = stack[sp++];
            if (kind == 2) {
                int panel = L.k / 5, j = L.k % 5;
                double f = panel + 0.5 * (bi::kGLNodes[j] + 1);
                for (int i = 0; i < m; ++i) d[i] = L.a[i] + (L.b[i] - L.a[i]) / kBcIntegralPanels * f;
            } else {
                std::fill(d, d + m, L.i);
            }
            break;
        }
        case OP_LOOP_END: {
            uint32_t arg = bcArgOf(ins), level = arg & 3, kind = (arg >> 2) & 3, len = arg >> 4;
            const double* v = stack[--sp];
            Loop& L = loops[level];
            bool done;
            if (kind == 0) { for (int i = 0; i < m; ++i) L.acc[i] += v[i]; done = ++L.i > L.hi; }
            else if (kind == 1) { for (int i = 0; i < m; ++i) L.acc[i] *= v[i]; done = ++L.i > L.hi; }
            else {
                double w = bi::kGLWeights[L.k % 5];
                for (int i = 0; i < m; ++i) L.acc[i] += w * v[i];
                done = ++L.k >= 5 * kBcIntegralPanels;
            }
            if (!done) { pc -= len + 1; break; }
            double* d = stack[sp++];
            if (kind == 2) {
                for (int i = 0; i < m; ++i) {
                    double a = L.a[i], b = L.b[i];
                    d[i] = !std::isfinite(a) || !std::isfinite(b) ? bi::kNaN
                           : a == b ? 0.0 : L.acc[i] * (b - a) / (2.0 * kBcIntegralPanels);
                }
            } else {
                std::memcpy(d, L.acc, m * sizeof(double));
            }
            break;
        }
        default:
            if (bcArity(op) == 1) {
                for (int i = 0; i < m; ++i) top[i] = bi::unary(op, top[i]);
            } else {
                --sp;
                double* a = stack[sp - 1]; double* b = stack[sp];
                for (int i = 0; i < m; ++i) a[i] = bi::binary(op, a[i], b[i]);
            }
            break;
        }
    }
    if (sp) std::memcpy(out, stack[sp - 1], m * sizeof(double));
    else std::fill(out, out + m, bi::kNaN);
    return true;
}

struct BatchJob {
    const Program* p;
    int n;
    const double *xs, *ys, *ts;
    double* out;
    double y0, t0;
};

void runChunk(const BatchJob& j, int base) {
    int m = std::min(kChunk, j.n - base);
    const double* cx = j.xs ? j.xs + base : nullptr;
    const double* cy = j.ys ? j.ys + base : nullptr;
    const double* ct = j.ts ? j.ts + base : nullptr;
    if (!evaluateChunk(*j.p, m, cx, cy, ct, j.out + base, j.y0, j.t0)) {
        for (int i = 0; i < m; ++i)
            j.out[base + i] = evaluate(*j.p, cx ? cx[i] : 0, cy ? cy[i] : j.y0, ct ? ct[i] : j.t0);
    }
}

void chunkWorker(void* ctx, size_t c) {
    const BatchJob& j = *static_cast<const BatchJob*>(ctx);
    runChunk(j, int(c) * kChunk);
}

} // namespace

void evaluateBatch(const Program& p, int n, const double* xs, const double* ys, const double* ts,
                   double* out, double y0, double t0) {
    BatchJob j{&p, n, xs, ys, ts, out, y0, t0};
    for (int base = 0; base < n; base += kChunk) runChunk(j, base);
}

void evaluateBatchParallel(const Program& p, int n, const double* xs, const double* ys, const double* ts,
                           double* out, double y0, double t0) {
    // Cheap programs aren't worth the dispatch; expensive ones (loops) spread across all cores.
    if (n < 2 * kChunk || (!p.hasLoops && n < 16 * kChunk)) {
        evaluateBatch(p, n, xs, ys, ts, out, y0, t0);
        return;
    }
    BatchJob j{&p, n, xs, ys, ts, out, y0, t0};
    size_t chunks = size_t((n + kChunk - 1) / kChunk);
    dispatch_apply_f(chunks, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), &j, chunkWorker);
}

} // namespace calc
