#include "math/Compiler.hpp"
#include "math/Builtins.hpp"
#include "math/Derivative.hpp"
#include "math/Parser.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace calc {

namespace {

struct BuiltinInfo { const char* name; BcOp op; int arity; };

const BuiltinInfo kBuiltins[] = {
    {"sin", OP_SIN, 1}, {"cos", OP_COS, 1}, {"tan", OP_TAN, 1}, {"sec", OP_SEC, 1},
    {"csc", OP_CSC, 1}, {"cot", OP_COT, 1}, {"arcsin", OP_ASIN, 1}, {"arccos", OP_ACOS, 1},
    {"arctan", OP_ATAN, 1}, {"arcsec", OP_ASEC, 1}, {"arccsc", OP_ACSC, 1},
    {"arccot", OP_ACOT, 1}, {"sinh", OP_SINH, 1}, {"cosh", OP_COSH, 1}, {"tanh", OP_TANH, 1},
    {"sech", OP_SECH, 1}, {"csch", OP_CSCH, 1}, {"coth", OP_COTH, 1}, {"arcsinh", OP_ASINH, 1},
    {"arccosh", OP_ACOSH, 1}, {"arctanh", OP_ATANH, 1}, {"sqrt", OP_SQRT, 1},
    {"abs", OP_ABS, 1}, {"sign", OP_SIGN, 1}, {"floor", OP_FLOOR, 1}, {"ceil", OP_CEIL, 1},
    {"round", OP_ROUND, 1}, {"exp", OP_EXP, 1}, {"ln", OP_LN, 1}, {"log", OP_LOG10, 1},
    {"logb", OP_LOGB, 2}, {"nthroot", OP_NTHROOT, 2}, {"mod", OP_MOD, 2}, {"nCr", OP_NCR, 2},
    {"nPr", OP_NPR, 2}, {"min", OP_MIN, 2}, {"max", OP_MAX, 2}, {"gcd", OP_GCD, 2},
    {"lcm", OP_LCM, 2}, {"hypot", OP_HYPOT, 2}, {"atan2", OP_ATAN2, 2},
};

const BuiltinInfo* findBuiltin(const std::string& name) {
    for (auto& b : kBuiltins) if (name == b.name) return &b;
    return nullptr;
}

bool isForwardTrig(const std::string& n) {
    return n == "sin" || n == "cos" || n == "tan" || n == "sec" || n == "csc" || n == "cot";
}
bool isInverseTrig(const std::string& n) {
    return n == "arcsin" || n == "arccos" || n == "arctan" || n == "arcsec" || n == "arccsc" ||
           n == "arccot";
}
bool isVariadic(const std::string& n) { return n == "min" || n == "max" || n == "gcd" || n == "lcm"; }
bool isReduction(const std::string& n) {
    return n == "total" || n == "mean" || n == "length" || n == "median" || n == "stdev" ||
           n == "var";
}

bool tryEvalConstant(const AstPool& pool, int root, double& out) {
    try {
        Program p = emit(pool, root, {});
        out = evaluate(p, 0, 0, 0);
        return true;
    } catch (const ParseError&) {
        return false;
    }
}

} // namespace

// ---- resolve -----------------------------------------------------------------------------------

int Resolver::resolve(const AstPool& src, int root, AstPool& out) {
    m_out = &out;
    m_varCache.clear();
    return go(src, root, {});
}

std::vector<int> Resolver::elements(int node) {
    AstPool& o = *m_out;
    if (o[node].op == AstOp::List) return o[node].kids;
    return expandLists(o, node);
}

int Resolver::call(const std::string& name, std::vector<int> args) {
    AstPool& o = *m_out;
    if (isReduction(name)) {
        if (args.size() != 1) {
            // total(1,2,3) etc.: treat the arguments as the list.
            AstNode l; l.op = AstOp::List; l.kids = args;
            args = {o.add(l)};
        }
        std::vector<int> el = elements(args[0]);
        int n = int(el.size());
        if (name == "length") return o.num(n);
        if (n == 0) return o.num(bi::kNaN);
        auto sum = [&](const std::vector<int>& v) {
            int acc = v[0];
            for (size_t k = 1; k < v.size(); ++k) acc = o.binary(AstOp::Add, acc, v[k]);
            return acc;
        };
        if (name == "total") return sum(el);
        int mean = o.binary(AstOp::Div, sum(el), o.num(n));
        if (name == "mean") return mean;
        if (name == "median") {
            std::vector<double> vals;
            for (int e : el) {
                double v;
                if (!tryEvalConstant(o, e, v)) throw ParseError("median needs constant values");
                vals.push_back(v);
            }
            std::sort(vals.begin(), vals.end());
            double med = n % 2 ? vals[n / 2] : 0.5 * (vals[n / 2 - 1] + vals[n / 2]);
            return o.num(med);
        }
        // stdev / var (sample, like Desmos)
        if (n < 2) return o.num(bi::kNaN);
        std::vector<int> sq;
        for (int e : el) {
            int d = o.binary(AstOp::Sub, e, mean);
            sq.push_back(o.binary(AstOp::Pow, d, o.num(2)));
        }
        int variance = o.binary(AstOp::Div, sum(sq), o.num(n - 1));
        if (name == "var") return variance;
        return o.call("sqrt", {variance});
    }
    if (isVariadic(name)) {
        if (args.size() == 1) args = elements(args[0]);
        if (args.empty()) throw ParseError(name + " needs an argument");
        int acc = args[0];
        for (size_t k = 1; k < args.size(); ++k) acc = o.call(name, {acc, args[k]});
        return acc;
    }
    if (name == "round" && args.size() == 2) {
        int scale = o.binary(AstOp::Pow, o.num(10), args[1]);
        int r = o.call("round", {o.binary(AstOp::Mul, args[0], scale)});
        return o.binary(AstOp::Div, r, scale);
    }
    if (name == "log" && args.size() == 2) return o.call("logb", {args[1], args[0]});
    const BuiltinInfo* info = findBuiltin(name);
    if (!info) throw ParseError("'" + name + "' isn't supported yet");
    if (int(args.size()) != info->arity)
        throw ParseError("'" + name + "' takes " + std::to_string(info->arity) +
                         (info->arity == 1 ? " argument" : " arguments"));
    if (m_degrees && isForwardTrig(name))
        args[0] = o.binary(AstOp::Mul, args[0], o.num(M_PI / 180.0));
    int c = o.call(name, args);
    if (m_degrees && isInverseTrig(name)) c = o.binary(AstOp::Mul, c, o.num(180.0 / M_PI));
    return c;
}

int Resolver::go(const AstPool& src, int node, const Scope& scope) {
    if (++m_depth > 200) throw ParseError("Expression is too deeply nested");
    struct DepthGuard { int& d; ~DepthGuard() { --d; } } guard{m_depth};
    AstPool& o = *m_out;
    const AstNode& n = src[node];
    switch (n.op) {
    case AstOp::Num: return o.num(n.num);
    case AstOp::Const:
        if (n.name == "pi") return o.num(M_PI);
        if (n.name == "tau") return o.num(2 * M_PI);
        if (n.name == "infinity") return o.num(INFINITY);
        return o.num(bi::kNaN);
    case AstOp::Var: {
        auto s = scope.find(n.name);
        if (s != scope.end()) return s->second;
        auto rn = m_rename.find(n.name);
        if (rn != m_rename.end()) return o.var(rn->second);
        if (m_passThrough.count(n.name)) return o.var(n.name);
        auto d = m_defs.vars.find(n.name);
        if (d != m_defs.vars.end()) {
            auto cached = m_varCache.find(n.name);
            if (cached != m_varCache.end()) return cached->second;
            if (m_active.count(n.name)) throw ParseError("'" + n.name + "' is defined in terms of itself");
            m_active.insert(n.name);
            int r = go(*d->second.pool, d->second.root, {});
            m_active.erase(n.name);
            m_varCache[n.name] = r;
            return r;
        }
        if (isCoordinateName(n.name) || n.name == "t") return o.var(n.name);
        if (n.name == "e") return o.num(M_E);
        m_missing.insert(n.name);
        return o.num(bi::kNaN);
    }
    case AstOp::UserCall: {
        auto f = m_defs.fns.find(n.name);
        if (f == m_defs.fns.end()) throw ParseError("'" + n.name + "' is not defined");
        const auto& fn = f->second;
        if (n.kids.size() != fn.params.size())
            throw ParseError("'" + n.name + "' takes " + std::to_string(fn.params.size()) +
                             (fn.params.size() == 1 ? " argument" : " arguments"));
        std::string key = n.name + "()";
        if (m_active.count(key)) throw ParseError("'" + n.name + "' can't call itself");
        Scope inner;
        for (size_t k = 0; k < n.kids.size(); ++k) inner[fn.params[k]] = go(src, n.kids[k], scope);
        m_active.insert(key);
        int r;
        int primes = int(n.num);
        if (primes > 0) {
            // f'(a): differentiate the body with respect to its (single) parameter, then
            // substitute the argument.
            if (fn.params.size() != 1) throw ParseError("' only works on functions of one variable");
            const std::string placeholder = "\x01" + fn.params[0];
            Scope ph;
            ph[fn.params[0]] = o.var(placeholder);
            r = go(*fn.pool, fn.body, ph);
            for (int k = 0; k < primes; ++k) r = differentiate(o, r, placeholder);
            r = replaceVar(o, r, placeholder, inner[fn.params[0]]);
        } else {
            r = go(*fn.pool, fn.body, inner);
        }
        m_active.erase(key);
        return r;
    }
    case AstOp::Call: {
        std::vector<int> args;
        for (int k : n.kids) args.push_back(go(src, k, scope));
        return call(n.name, std::move(args));
    }
    case AstOp::Deriv: {
        // Keep the variable symbolic while resolving the operand, then differentiate. A defined
        // variable (d/da with a slider) is substituted back afterwards.
        bool wasPass = m_passThrough.count(n.name) > 0;
        m_passThrough.insert(n.name);
        auto savedCache = m_varCache;
        m_varCache.clear();
        int inner;
        try {
            inner = go(src, n.kids[0], scope);
        } catch (...) {
            if (!wasPass) m_passThrough.erase(n.name);
            m_varCache = savedCache;
            throw;
        }
        if (!wasPass) m_passThrough.erase(n.name);
        m_varCache = std::move(savedCache);
        int d = differentiate(o, inner, n.name);
        if (m_defs.vars.count(n.name) && !wasPass && !isCoordinateName(n.name)) {
            AstNode v; v.op = AstOp::Var; v.name = n.name;
            AstPool tmp;
            int vid = tmp.add(v);
            d = replaceVar(o, d, n.name, go(tmp, vid, scope));
        }
        return d;
    }
    case AstOp::Sum: case AstOp::Prod: case AstOp::Integral: {
        int lo = go(src, n.kids[0], scope), hi = go(src, n.kids[1], scope);
        std::string idx = "\x02" + std::to_string(m_loopCounter++) + n.name;
        Scope inner = scope;
        inner[n.name] = o.var(idx);
        int body = go(src, n.kids[2], inner);
        AstNode c = n;
        c.name = idx;
        c.kids = {lo, hi, body};
        return o.add(std::move(c));
    }
    case AstOp::Index: {
        int list = go(src, n.kids[0], scope);
        int idx = go(src, n.kids[1], scope);
        double iv;
        if (tryEvalConstant(o, idx, iv)) {
            std::vector<int> el = elements(list);
            long k = std::lround(iv);
            if (k < 1 || k > long(el.size())) return o.num(bi::kNaN);
            return el[k - 1];
        }
        // Variable index (e.g. a[k] inside a sum): keep a lookup into an all-numbers list, which
        // compiles to OP_LIST_INDEX. One copy per list, however many times it's indexed.
        auto cached = m_constLists.find(list);
        int numbers;
        if (cached != m_constLists.end()) {
            numbers = cached->second;
        } else {
            AstNode l;
            l.op = AstOp::List;
            for (int e : elements(list)) {
                double v;
                if (!tryEvalConstant(o, e, v))
                    throw ParseError("A list indexed by a variable must contain only numbers");
                l.kids.push_back(o.num(v));
            }
            numbers = o.add(std::move(l));
            m_constLists[list] = numbers;
        }
        AstNode ix;
        ix.op = AstOp::Index;
        ix.kids = {numbers, idx};
        return o.add(std::move(ix));
    }
    default: {
        AstNode copy = n;
        for (int& k : copy.kids)
            if (k >= 0) k = go(src, k, scope);
        return o.add(std::move(copy));
    }
    }
}

// ---- list broadcasting -------------------------------------------------------------------------

namespace {

struct RangeSpec { double start, step; int count; };

RangeSpec rangeSpec(const AstPool& pool, const AstNode& r) {
    double a, b, last;
    if (!tryEvalConstant(pool, r.kids[0], a) || !tryEvalConstant(pool, r.kids[2], last) ||
        (r.kids[1] >= 0 && !tryEvalConstant(pool, r.kids[1], b)))
        throw ParseError("List ranges must use constant bounds");
    double step = r.kids[1] >= 0 ? b - a : (last >= a ? 1 : -1);
    if (step == 0 || !std::isfinite(step)) throw ParseError("List range step can't be 0");
    double cnt = std::floor((last - a) / step + 1e-9) + 1;
    if (cnt < 0) cnt = 0;
    if (cnt > kMaxListLength) throw ParseError("Lists can have at most 10000 elements");
    return {a, step, int(cnt)};
}

// Smallest list length within the tree (-1 when there are no lists).
int listLength(const AstPool& pool, int node) {
    const AstNode& n = pool[node];
    if (n.op == AstOp::List) return int(n.kids.size());
    if (n.op == AstOp::Range) return rangeSpec(pool, n).count;
    int best = -1;
    for (size_t ki = 0; ki < n.kids.size(); ++ki) {
        int k = n.kids[ki];
        if (k < 0 || (n.op == AstOp::Index && ki == 0)) continue;  // a looked-up list isn't broadcast
        int l = listLength(pool, k);
        if (l >= 0 && (best < 0 || l < best)) best = l;
    }
    return best;
}

int pick(AstPool& pool, int node, int i) {
    AstNode n = pool[node];
    if (n.op == AstOp::List) return pick(pool, n.kids[i], i);
    if (n.op == AstOp::Range) {
        RangeSpec r = rangeSpec(pool, n);
        return pool.num(r.start + r.step * i);
    }
    bool hasList = false;
    for (size_t ki = 0; ki < n.kids.size(); ++ki) {
        if (n.op == AstOp::Index && ki == 0) continue;
        if (n.kids[ki] >= 0 && listLength(pool, n.kids[ki]) >= 0) { hasList = true; break; }
    }
    if (!hasList) return node;
    for (size_t ki = 0; ki < n.kids.size(); ++ki) {
        if (n.op == AstOp::Index && ki == 0) continue;
        if (n.kids[ki] >= 0) n.kids[ki] = pick(pool, n.kids[ki], i);
    }
    return pool.add(std::move(n));
}

} // namespace

std::vector<int> expandLists(AstPool& pool, int root) {
    int len = listLength(pool, root);
    if (len < 0) return {root};
    std::vector<int> out;
    out.reserve(len);
    for (int i = 0; i < len; ++i) out.push_back(pick(pool, root, i));
    return out;
}

// ---- folding / queries -------------------------------------------------------------------------

int fold(AstPool& pool, int root) {
    AstNode n = pool[root];
    if (n.op == AstOp::Num || n.op == AstOp::Var) return root;
    bool allNum = true;
    bool changed = false;
    for (int& k : n.kids) {
        if (k < 0) continue;
        int f = fold(pool, k);
        if (f != k) { k = f; changed = true; }
        if (pool[k].op != AstOp::Num) allNum = false;
    }
    bool foldable = n.op != AstOp::Tuple && n.op != AstOp::List && n.op != AstOp::Range &&
                    n.op != AstOp::UserCall;
    int id = changed ? pool.add(n) : root;
    if (allNum && foldable) {
        double v;
        if (tryEvalConstant(pool, id, v)) return pool.num(v);
    }
    return id;
}

int substitute(AstPool& pool, int root, const std::map<std::string, double>& values) {
    AstNode n = pool[root];
    if (n.op == AstOp::Var) {
        auto it = values.find(n.name);
        return it == values.end() ? root : pool.num(it->second);
    }
    bool changed = false;
    for (int& k : n.kids) {
        if (k < 0) continue;
        int s = substitute(pool, k, values);
        if (s != k) { k = s; changed = true; }
    }
    return changed ? pool.add(std::move(n)) : root;
}

double evalConstant(const AstPool& pool, int root) {
    double v;
    return tryEvalConstant(pool, root, v) ? v : bi::kNaN;
}

bool containsVar(const AstPool& pool, int root, const std::string& name) {
    const AstNode& n = pool[root];
    if (n.op == AstOp::Var) return n.name == name;
    for (int k : n.kids)
        if (k >= 0 && containsVar(pool, k, name)) return true;
    return false;
}

void collectVars(const AstPool& pool, int root, std::set<std::string>& out) {
    const AstNode& n = pool[root];
    if (n.op == AstOp::Var && !isInternalName(n.name)) out.insert(n.name);
    for (int k : n.kids)
        if (k >= 0) collectVars(pool, k, out);
}

// ---- emit --------------------------------------------------------------------------------------

namespace {

struct Emitter {
    const AstPool& pool;
    const ParamMap& params;
    Program prog;
    const std::vector<std::string>* paramNames = nullptr;
    std::vector<std::pair<std::string, uint32_t>> loops;  // active loop index names -> (level, kind)
    std::map<int, uint32_t> listBlocks;                   // list node -> constant block offset
    int sp = 0;

    void push(BcOp op, uint32_t arg = 0) {
        prog.code.push_back(bcEncode(op, arg));
        sp += 1 - bcArity(op);
        prog.maxStack = std::max(prog.maxStack, sp);
        if (prog.maxStack > kBcMaxStack) throw ParseError("Expression is too complex");
    }

    void constant(double v) {
        if (std::isnan(v)) { push(OP_NAN); return; }
        uint32_t idx = 0;
        auto it = std::find_if(prog.consts.begin(), prog.consts.end(), [&](double c) {
            return std::memcmp(&c, &v, sizeof v) == 0;
        });
        if (it != prog.consts.end()) idx = uint32_t(it - prog.consts.begin());
        else { idx = uint32_t(prog.consts.size()); prog.consts.push_back(v); }
        push(OP_CONST, idx);
    }

    void piecewise(const AstNode& n, size_t k) {
        size_t pairs = (n.kids.size() - (n.hasDefault ? 1 : 0)) / 2;
        if (k == pairs) {
            if (n.hasDefault) node(n.kids.back());
            else push(OP_NAN);
            return;
        }
        node(n.kids[2 * k]);
        node(n.kids[2 * k + 1]);
        piecewise(n, k + 1);
        push(OP_SELECT);
    }

    void node(int id) {
        const AstNode& n = pool[id];
        switch (n.op) {
        case AstOp::Num: constant(n.num); return;
        case AstOp::Var: {
            for (auto it = loops.rbegin(); it != loops.rend(); ++it) {
                if (it->first == n.name) {
                    uint32_t level = uint32_t(loops.rend() - it - 1);
                    push(OP_LOOP_IDX, bcLoopArg(level, it->second, 0));
                    return;
                }
            }
            if (paramNames) {
                auto it = std::find(paramNames->begin(), paramNames->end(), n.name);
                if (it != paramNames->end()) { push(OP_PARAM, uint32_t(it - paramNames->begin())); return; }
            }
            auto p = params.find(n.name);
            if (p == params.end()) throw ParseError("'" + n.name + "' can't be used here");
            push(p->second);
            if (p->second == OP_X) prog.usesX = true;
            else if (p->second == OP_Y) prog.usesY = true;
            else if (p->second == OP_T) prog.usesT = true;
            else prog.usesPolar = true;
            return;
        }
        case AstOp::Pow: {
            const AstNode& ex = pool[n.kids[1]];
            bool pOdd;
            if (ex.op == AstOp::Num && bi::oddDenominatorRational(ex.num, pOdd)) {
                node(n.kids[0]);
                constant(ex.num);
                push(pOdd ? OP_POW_ODD : OP_POW_EVEN);
                return;
            }
            node(n.kids[0]);
            node(n.kids[1]);
            push(OP_POW);
            return;
        }
        case AstOp::Add: case AstOp::Sub: case AstOp::Mul: case AstOp::Div: {
            node(n.kids[0]);
            node(n.kids[1]);
            static const BcOp ops[] = {OP_ADD, OP_SUB, OP_MUL, OP_DIV};
            push(ops[int(n.op) - int(AstOp::Add)]);
            return;
        }
        case AstOp::Neg: node(n.kids[0]); push(OP_NEG); return;
        case AstOp::Factorial: node(n.kids[0]); push(OP_FACT); return;
        case AstOp::Call: {
            const BuiltinInfo* info = findBuiltin(n.name);
            if (!info || int(n.kids.size()) != info->arity) throw ParseError("'" + n.name + "' isn't supported");
            for (int k : n.kids) node(k);
            push(info->op);
            return;
        }
        case AstOp::Compare: {
            node(n.kids[0]);
            node(n.kids[1]);
            static const BcOp ops[] = {OP_EQ, OP_LT, OP_LE, OP_GT, OP_GE, OP_EQ};
            push(ops[int(n.cmp)]);
            return;
        }
        case AstOp::And:
            node(n.kids[0]);
            for (size_t k = 1; k < n.kids.size(); ++k) { node(n.kids[k]); push(OP_AND); }
            return;
        case AstOp::Piecewise: piecewise(n, 0); return;
        case AstOp::Index: {
            const AstNode& l = pool[n.kids[0]];
            if (l.op != AstOp::List) throw ParseError("List index must be a constant");
            uint32_t off;
            auto it = listBlocks.find(n.kids[0]);
            if (it != listBlocks.end()) {
                off = it->second;
            } else {
                off = uint32_t(prog.consts.size());
                prog.consts.push_back(double(l.kids.size()));
                for (int k : l.kids) prog.consts.push_back(pool[k].num);
                listBlocks[n.kids[0]] = off;
            }
            node(n.kids[1]);
            push(OP_LIST_INDEX, off);
            return;
        }
        case AstOp::Sum: case AstOp::Prod: case AstOp::Integral: {
            uint32_t kind = n.op == AstOp::Sum ? 0 : n.op == AstOp::Prod ? 1 : 2;
            if (loops.size() >= size_t(kBcMaxLoops)) throw ParseError("Too many nested sums, products or integrals");
            node(n.kids[0]);
            node(n.kids[1]);
            uint32_t level = uint32_t(loops.size());
            size_t beginAt = prog.code.size();
            prog.code.push_back(0);   // patched below
            sp -= 2;                  // BEGIN pops lo, hi and pushes nothing
            loops.push_back({n.name, kind});
            node(n.kids[2]);
            loops.pop_back();
            uint32_t bodyLen = uint32_t(prog.code.size() - beginAt - 1);
            if (bodyLen >= (1u << 20)) throw ParseError("Expression is too long");
            prog.code[beginAt] = bcEncode(OP_LOOP_BEGIN, bcLoopArg(level, kind, bodyLen));
            prog.code.push_back(bcEncode(OP_LOOP_END, bcLoopArg(level, kind, bodyLen)));  // pops 1, pushes 1
            prog.hasLoops = true;
            return;
        }
        case AstOp::Tuple: throw ParseError("Points can't be used in arithmetic here");
        case AstOp::List: case AstOp::Range: throw ParseError("Lists can't be used here");
        default: throw ParseError("Unsupported expression");
        }
    }
};

} // namespace

Program emit(const AstPool& pool, int root, const ParamMap& params) {
    Emitter e{pool, params, {}};
    e.node(root);
    return std::move(e.prog);
}

Program emitWithParams(const AstPool& pool, int root, const ParamMap& params,
                       const std::vector<std::string>& paramNames) {
    Emitter e{pool, params, {}};
    e.paramNames = &paramNames;
    e.node(root);
    return std::move(e.prog);
}

} // namespace calc
