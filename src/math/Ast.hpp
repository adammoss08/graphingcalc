#pragma once
// Expression AST stored in a flat pool (indices, no pointers) so it is cheap to copy and substitute.
#include <cstdint>
#include <string>
#include <vector>

namespace calc {

enum class AstOp : uint8_t {
    Num,        // num
    Var,        // name (x, y, a, a_{1}, θ, ...)
    Const,      // name: "pi", "e", "infinity", "tau"
    Add, Sub, Mul, Div, Pow, Neg, Factorial,
    Call,       // builtin: name + kids (sin, sqrt, nthroot, log, logb, min, ...)
    UserCall,   // user function: name + kids
    Tuple,      // (a, b) or (a, b, c)
    List,       // [a, b, c]
    Range,      // [a ... b] or [a, b, ..., c]: kids = {first, second-or-(-1), last}
    Index,      // kids = {list, index}  (L[2], 1-based like Desmos)
    Compare,    // cmp + kids {a, b}
    And,        // kids: conditions (from chained comparisons)
    Piecewise,  // kids: cond0, val0, cond1, val1, ..., [default]; hasDefault flag
    Deriv,      // d/d(name) of kids[0]
    Sum,        // name = index variable; kids = {lo, hi, body}
    Prod,       // as Sum
    Integral,   // name = integration variable; kids = {lo, hi, body}
};

enum class Cmp : uint8_t { None, Lt, Le, Gt, Ge, Eq };

inline bool isStrict(Cmp c) { return c == Cmp::Lt || c == Cmp::Gt; }

struct AstNode {
    AstOp op = AstOp::Num;
    Cmp cmp = Cmp::None;
    bool hasDefault = false;
    double num = 0;             // Num value; UserCall: number of primes (f'(x) = 1)
    std::string name;
    std::vector<int> kids;
};

struct AstPool {
    std::vector<AstNode> nodes;

    int add(AstNode n) { nodes.push_back(std::move(n)); return int(nodes.size()) - 1; }
    int num(double v) { AstNode n; n.op = AstOp::Num; n.num = v; return add(std::move(n)); }
    int var(const std::string& name) { AstNode n; n.op = AstOp::Var; n.name = name; return add(std::move(n)); }
    int unary(AstOp op, int a) { AstNode n; n.op = op; n.kids = {a}; return add(std::move(n)); }
    int binary(AstOp op, int a, int b) { AstNode n; n.op = op; n.kids = {a, b}; return add(std::move(n)); }
    int call(const std::string& name, std::vector<int> args, AstOp op = AstOp::Call) {
        AstNode n; n.op = op; n.name = name; n.kids = std::move(args); return add(std::move(n));
    }
    const AstNode& operator[](int i) const { return nodes[i]; }
    AstNode& operator[](int i) { return nodes[i]; }

    // Copies subtree `root` of `src` into this pool, returning the new root.
    int import(const AstPool& src, int root);
    std::string debugString(int root) const;
};

// A parsed row: either a bare expression or a chain e0 rel0 e1 rel1 e2 ...
struct Statement {
    std::vector<int> exprs;      // >= 1
    std::vector<Cmp> rels;       // exprs.size() - 1
    bool topLevelList = false;   // "(1,2),(3,4)": exprs holds the items, no rels
    bool regression = false;     // "y_1 ~ m x_1 + b": exprs = {data, model}, no rels
};

} // namespace calc
