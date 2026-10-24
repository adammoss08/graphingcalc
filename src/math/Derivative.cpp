#include "math/Derivative.hpp"
#include "math/Compiler.hpp"
#include "math/Parser.hpp"

#include <cmath>

namespace calc {

namespace {

struct Builder {
    AstPool& p;

    bool isNum(int n, double v) const { return p[n].op == AstOp::Num && p[n].num == v; }
    bool isConst(int n) const { return p[n].op == AstOp::Num; }

    int num(double v) { return p.num(v); }
    int add(int a, int b) {
        if (isNum(a, 0)) return b;
        if (isNum(b, 0)) return a;
        if (isConst(a) && isConst(b)) return num(p[a].num + p[b].num);
        return p.binary(AstOp::Add, a, b);
    }
    int sub(int a, int b) {
        if (isNum(b, 0)) return a;
        if (isNum(a, 0)) return neg(b);
        if (isConst(a) && isConst(b)) return num(p[a].num - p[b].num);
        return p.binary(AstOp::Sub, a, b);
    }
    int mul(int a, int b) {
        if (isNum(a, 0) || isNum(b, 0)) return num(0);
        if (isNum(a, 1)) return b;
        if (isNum(b, 1)) return a;
        if (isNum(a, -1)) return neg(b);
        if (isNum(b, -1)) return neg(a);
        if (isConst(a) && isConst(b)) return num(p[a].num * p[b].num);
        return p.binary(AstOp::Mul, a, b);
    }
    int div(int a, int b) {
        if (isNum(a, 0)) return num(0);
        if (isNum(b, 1)) return a;
        if (isConst(a) && isConst(b) && p[b].num != 0) return num(p[a].num / p[b].num);
        return p.binary(AstOp::Div, a, b);
    }
    int neg(int a) {
        if (isConst(a)) return num(-p[a].num);
        if (p[a].op == AstOp::Neg) return p[a].kids[0];
        return p.unary(AstOp::Neg, a);
    }
    int pow(int a, int b) {
        if (isNum(b, 0)) return num(1);
        if (isNum(b, 1)) return a;
        return p.binary(AstOp::Pow, a, b);
    }
    int sq(int a) { return pow(a, num(2)); }
    int call(const char* name, int a) { return p.call(name, {a}); }
    int call2(const char* name, int a, int b) { return p.call(name, {a, b}); }
};

int deriv(Builder& B, int node, const std::string& var);

int derivCall(Builder& B, const AstNode& n, int self, const std::string& var) {
    AstPool& p = B.p;
    const std::string& f = n.name;
    if (n.kids.size() == 2) {
        int a = n.kids[0], b = n.kids[1];
        if (f == "logb") {  // log_a(b) = ln b / ln a
            int e = B.div(B.call("ln", b), B.call("ln", a));
            return deriv(B, e, var);
        }
        if (f == "nthroot") {  // d nthroot(k, u) = nthroot(k, u) / (k u) · du
            return B.mul(B.div(self, B.mul(a, b)), deriv(B, b, var));
        }
        int da = deriv(B, a, var), db = deriv(B, b, var);
        if (f == "min" || f == "max") {
            AstNode c; c.op = AstOp::Compare; c.cmp = f == "min" ? Cmp::Lt : Cmp::Gt; c.kids = {a, b};
            AstNode pw; pw.op = AstOp::Piecewise; pw.hasDefault = true;
            pw.kids = {p.add(c), da, db};
            return p.add(pw);
        }
        if (f == "mod") return B.sub(da, B.mul(db, B.call("floor", B.div(a, b))));
        if (f == "hypot") return B.div(B.add(B.mul(a, da), B.mul(b, db)), self);
        if (f == "atan2") return B.div(B.sub(B.mul(b, da), B.mul(a, db)), B.add(B.sq(a), B.sq(b)));
        return B.num(0);  // nCr, nPr, gcd, lcm: integer-valued
    }
    int u = n.kids[0];
    int du = deriv(B, u, var);
    if (B.isNum(du, 0)) return B.num(0);
    int one = B.num(1);
    int g;  // f'(u)
    if (f == "sin") g = B.call("cos", u);
    else if (f == "cos") g = B.neg(B.call("sin", u));
    else if (f == "tan") g = B.div(one, B.sq(B.call("cos", u)));
    else if (f == "sec") g = B.mul(B.call("sec", u), B.call("tan", u));
    else if (f == "csc") g = B.neg(B.mul(B.call("csc", u), B.call("cot", u)));
    else if (f == "cot") g = B.neg(B.div(one, B.sq(B.call("sin", u))));
    else if (f == "arcsin") g = B.div(one, B.call("sqrt", B.sub(one, B.sq(u))));
    else if (f == "arccos") g = B.neg(B.div(one, B.call("sqrt", B.sub(one, B.sq(u)))));
    else if (f == "arctan") g = B.div(one, B.add(one, B.sq(u)));
    else if (f == "arcsec") g = B.div(one, B.mul(B.call("abs", u), B.call("sqrt", B.sub(B.sq(u), one))));
    else if (f == "arccsc") g = B.neg(B.div(one, B.mul(B.call("abs", u), B.call("sqrt", B.sub(B.sq(u), one)))));
    else if (f == "arccot") g = B.neg(B.div(one, B.add(one, B.sq(u))));
    else if (f == "sinh") g = B.call("cosh", u);
    else if (f == "cosh") g = B.call("sinh", u);
    else if (f == "tanh") g = B.div(one, B.sq(B.call("cosh", u)));
    else if (f == "sech") g = B.neg(B.mul(B.call("sech", u), B.call("tanh", u)));
    else if (f == "csch") g = B.neg(B.mul(B.call("csch", u), B.call("coth", u)));
    else if (f == "coth") g = B.neg(B.div(one, B.sq(B.call("sinh", u))));
    else if (f == "arcsinh") g = B.div(one, B.call("sqrt", B.add(B.sq(u), one)));
    else if (f == "arccosh") g = B.div(one, B.call("sqrt", B.sub(B.sq(u), one)));
    else if (f == "arctanh") g = B.div(one, B.sub(one, B.sq(u)));
    else if (f == "sqrt") g = B.div(one, B.mul(B.num(2), self));
    else if (f == "exp") g = self;
    else if (f == "ln") g = B.div(one, u);
    else if (f == "log") g = B.div(one, B.mul(u, B.num(std::log(10.0))));
    else if (f == "abs") g = B.call("sign", u);
    else if (f == "sign" || f == "floor" || f == "ceil" || f == "round") return B.num(0);
    else throw ParseError("Can't differentiate '" + f + "'");
    return B.mul(g, du);
}

int deriv(Builder& B, int node, const std::string& var) {
    AstPool& p = B.p;
    if (!containsVar(p, node, var)) {
        // Lists of constants differentiate to lists of zeros so broadcasting still lines up.
        if (p[node].op == AstOp::List) {
            AstNode l; l.op = AstOp::List;
            for (size_t k = 0; k < p[node].kids.size(); ++k) l.kids.push_back(B.num(0));
            return p.add(l);
        }
        return B.num(0);
    }
    const AstNode n = p[node];  // copy: the pool may grow while we build
    switch (n.op) {
    case AstOp::Var: return B.num(n.name == var ? 1 : 0);
    case AstOp::Add: return B.add(deriv(B, n.kids[0], var), deriv(B, n.kids[1], var));
    case AstOp::Sub: return B.sub(deriv(B, n.kids[0], var), deriv(B, n.kids[1], var));
    case AstOp::Neg: return B.neg(deriv(B, n.kids[0], var));
    case AstOp::Mul: {
        int a = n.kids[0], b = n.kids[1];
        return B.add(B.mul(deriv(B, a, var), b), B.mul(a, deriv(B, b, var)));
    }
    case AstOp::Div: {
        int a = n.kids[0], b = n.kids[1];
        int da = deriv(B, a, var), db = deriv(B, b, var);
        if (B.isNum(db, 0)) return B.div(da, b);
        return B.div(B.sub(B.mul(da, b), B.mul(a, db)), B.sq(b));
    }
    case AstOp::Pow: {
        int a = n.kids[0], b = n.kids[1];
        bool varBase = containsVar(p, a, var), varExp = containsVar(p, b, var);
        if (!varExp) {
            int bm1 = B.sub(b, B.num(1));
            if (p[b].op != AstOp::Num) bm1 = fold(p, bm1);
            return B.mul(B.mul(b, B.pow(a, bm1)), deriv(B, a, var));
        }
        if (!varBase) return B.mul(B.mul(node, B.call("ln", a)), deriv(B, b, var));
        // a^b (b' ln a + b a'/a)
        int t = B.add(B.mul(deriv(B, b, var), B.call("ln", a)), B.div(B.mul(b, deriv(B, a, var)), a));
        return B.mul(node, t);
    }
    case AstOp::Factorial: {
        // No closed form in our builtins (digamma): symmetric difference quotient.
        int u = n.kids[0];
        double h = 1e-5;
        int fp = p.unary(AstOp::Factorial, B.add(u, B.num(h)));
        int fm = p.unary(AstOp::Factorial, B.sub(u, B.num(h)));
        return B.mul(B.div(B.sub(fp, fm), B.num(2 * h)), deriv(B, u, var));
    }
    case AstOp::Call: return derivCall(B, n, node, var);
    case AstOp::Piecewise: {
        AstNode pw = n;
        size_t pairs = (n.kids.size() - (n.hasDefault ? 1 : 0)) / 2;
        for (size_t k = 0; k < pairs; ++k) pw.kids[2 * k + 1] = deriv(B, n.kids[2 * k + 1], var);
        if (n.hasDefault) pw.kids.back() = deriv(B, n.kids.back(), var);
        return p.add(pw);
    }
    case AstOp::Tuple: case AstOp::List: {
        AstNode t = n;
        for (int& k : t.kids) k = deriv(B, k, var);
        return p.add(t);
    }
    case AstOp::Sum: {
        AstNode c = n;
        c.kids[2] = deriv(B, n.kids[2], var);
        if (containsVar(p, n.kids[0], var) || containsVar(p, n.kids[1], var))
            throw ParseError("Can't differentiate a sum whose limits depend on " + var);
        return p.add(c);
    }
    case AstOp::Prod: {
        // d∏f = ∏f · Σ f'/f
        AstNode c = n;
        c.op = AstOp::Sum;
        c.kids[2] = B.div(deriv(B, n.kids[2], var), n.kids[2]);
        return B.mul(node, p.add(c));
    }
    case AstOp::Integral: {
        // Leibniz rule: f(b)·b' − f(a)·a' + ∫ ∂f/∂var
        int a = n.kids[0], b = n.kids[1], f = n.kids[2];
        int fb = replaceVar(p, f, n.name, b), fa = replaceVar(p, f, n.name, a);
        int r = B.sub(B.mul(fb, deriv(B, b, var)), B.mul(fa, deriv(B, a, var)));
        if (containsVar(p, f, var)) {
            AstNode c = n;
            c.kids[2] = deriv(B, f, var);
            r = B.add(r, p.add(c));
        }
        return r;
    }
    case AstOp::Compare: case AstOp::And: return B.num(0);
    case AstOp::Index: return B.num(0);  // lookups into constant lists are piecewise constant
    default: throw ParseError("Can't differentiate this expression");
    }
}

} // namespace

int differentiate(AstPool& pool, int node, const std::string& var) {
    Builder b{pool};
    return fold(pool, deriv(b, node, var));
}

int replaceVar(AstPool& pool, int root, const std::string& name, int replacement) {
    AstNode n = pool[root];
    if (n.op == AstOp::Var) return n.name == name ? replacement : root;
    bool changed = false;
    for (int& k : n.kids) {
        if (k < 0) continue;
        int r = replaceVar(pool, k, name, replacement);
        if (r != k) { k = r; changed = true; }
    }
    return changed ? pool.add(std::move(n)) : root;
}

} // namespace calc
