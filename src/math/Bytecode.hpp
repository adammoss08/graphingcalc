#pragma once
// Stack-machine bytecode shared by the CPU VM (Vm.cpp, double) and the GPU VM (shaders/vm.h,
// float). This header is included from both C++ and Metal Shading Language. Keep it plain.
//
// Instruction word: low 8 bits = opcode, high 24 bits = argument (constant index for OP_CONST).

#ifdef __METAL_VERSION__
#define BC_CONSTEXPR constant constexpr
#else
#include <cstdint>
#define BC_CONSTEXPR constexpr
namespace calc {
#endif

// Loops: at most 4 nested; integrals use composite 5-point Gauss-Legendre on this many panels.
BC_CONSTEXPR int kBcMaxLoops = 4;
BC_CONSTEXPR int kBcIntegralPanels = 16;

// Maximum evaluation stack depth. The compiler rejects programs that need more (the GPU keeps the
// stack in registers).
BC_CONSTEXPR int kBcMaxStack = 24;

enum BcOp : unsigned int {
    // operands
    OP_CONST = 0, OP_X, OP_Y, OP_T, OP_R, OP_THETA,
    // arithmetic
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW, OP_NEG,
    // roots & simple functions
    OP_SQRT, OP_NTHROOT, OP_ABS, OP_SIGN, OP_FLOOR, OP_CEIL, OP_ROUND, OP_FACT,
    // trig
    OP_SIN, OP_COS, OP_TAN, OP_SEC, OP_CSC, OP_COT,
    OP_ASIN, OP_ACOS, OP_ATAN, OP_ASEC, OP_ACSC, OP_ACOT, OP_ATAN2,
    // hyperbolic
    OP_SINH, OP_COSH, OP_TANH, OP_SECH, OP_CSCH, OP_COTH, OP_ASINH, OP_ACOSH, OP_ATANH,
    // exp / log
    OP_EXP, OP_LN, OP_LOG10, OP_LOGB,
    // binary misc
    OP_MIN, OP_MAX, OP_MOD, OP_HYPOT, OP_NCR, OP_NPR, OP_GCD, OP_LCM,
    // comparisons -> 1.0 / 0.0 (NaN compares false)
    OP_LT, OP_LE, OP_GT, OP_GE, OP_EQ, OP_AND,
    // select(cond, a, b) = cond != 0 ? a : b
    OP_SELECT,
    OP_NAN,
    // constant rational exponent p/q with q odd (detected at compile time): pow(a, e) where
    // POW_ODD = sign(a)·|a|^e (p odd), POW_EVEN = |a|^e (p even)
    OP_POW_ODD, OP_POW_EVEN,
    // regression parameter (arg = index); CPU only, never sent to the GPU
    OP_PARAM,
    // loops (sum / product / integral). arg = level (bits 0-1) | kind (bits 2-3: 0 sum, 1 product,
    // 2 integral) | body length (bits 4+). BEGIN pops (lo, hi) and either enters the body or pushes
    // the empty result and skips past END. IDX (arg = level) pushes the index / quadrature node.
    // END pops the body value, accumulates, and jumps back or pushes the result.
    OP_LOOP_BEGIN, OP_LOOP_IDX, OP_LOOP_END,
    // list lookup with a variable index (Desmos L[k], 1-based). arg = offset of a constant block
    // {n, v1..vn}; pops the index, pushes v[round(index)] or NaN when out of range.
    OP_LIST_INDEX,
    OP_COUNT
};

#ifdef __METAL_VERSION__
inline unsigned int bcOpOf(unsigned int ins) { return ins & 0xFFu; }
inline unsigned int bcArgOf(unsigned int ins) { return ins >> 8; }
#else
inline constexpr uint32_t bcEncode(BcOp op, uint32_t arg = 0) { return uint32_t(op) | (arg << 8); }
inline constexpr BcOp bcOpOf(uint32_t ins) { return BcOp(ins & 0xFFu); }
inline constexpr uint32_t bcArgOf(uint32_t ins) { return ins >> 8; }
inline constexpr uint32_t bcLoopArg(uint32_t level, uint32_t kind, uint32_t bodyLen) {
    return level | (kind << 2) | (bodyLen << 4);
}
// Stack effect: number of values popped by each op (pushes exactly one).
inline constexpr int bcArity(BcOp op) {
    switch (op) {
    case OP_CONST: case OP_X: case OP_Y: case OP_T: case OP_R: case OP_THETA: case OP_NAN:
    case OP_PARAM: case OP_LOOP_IDX: return 0;
    case OP_LOOP_BEGIN: return 2;  // and pushes nothing (see Compiler.cpp)
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_POW: case OP_NTHROOT:
    case OP_POW_ODD: case OP_POW_EVEN:
    case OP_ATAN2: case OP_LOGB: case OP_MIN: case OP_MAX: case OP_MOD: case OP_HYPOT:
    case OP_NCR: case OP_NPR: case OP_GCD: case OP_LCM:
    case OP_LT: case OP_LE: case OP_GT: case OP_GE: case OP_EQ: case OP_AND: return 2;
    case OP_SELECT: return 3;
    default: return 1;
    }
}
} // namespace calc
#endif
