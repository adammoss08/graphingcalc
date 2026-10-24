#pragma once
// Ast -> Program. Three stages:
//   1. resolve: inline variable definitions and user functions, apply degree mode, lower variadic
//      builtins and list reductions (total, mean, ...) to binary nodes;
//   2. expandLists: Desmos broadcasting, so an expression containing lists becomes one expression
//      per element;
//   3. fold + emit: constant folding, then stack bytecode.
#include "math/Ast.hpp"
#include "math/Vm.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace calc {

struct Definitions {
    struct Var { const AstPool* pool; int root; };
    struct Fn { const AstPool* pool; int body; std::vector<std::string> params; };
    std::map<std::string, Var> vars;
    std::map<std::string, Fn> fns;
};

// Internal variable names (sum indices, f' placeholders) start with a control character and are
// never reported as undefined.
inline bool isInternalName(const std::string& n) { return !n.empty() && (n[0] == '\x01' || n[0] == '\x02'); }

// Names that are graph coordinates rather than variables.
inline bool isCoordinateName(const std::string& n) {
    return n == "x" || n == "y" || n == "r" || n == "\xCE\xB8" /* θ */;
}

class Resolver {
public:
    Resolver(const Definitions& defs, bool degrees) : m_defs(defs), m_degrees(degrees) {}

    // Fully inlines `root` of `src` into `out`. Undefined names are collected in missing() and
    // replaced by NaN. Throws ParseError on recursion or misuse.
    int resolve(const AstPool& src, int root, AstPool& out);
    const std::set<std::string>& missing() const { return m_missing; }
    // Names kept as plain variables instead of being reported missing (regression parameters).
    void setPassThrough(std::set<std::string> names) { m_passThrough = std::move(names); }
    // Names replaced by another variable (regression: the data list becomes x for graphing).
    void setRename(std::map<std::string, std::string> r) { m_rename = std::move(r); }

private:
    using Scope = std::map<std::string, int>;
    const Definitions& m_defs;
    bool m_degrees;
    std::set<std::string> m_missing;
    std::set<std::string> m_passThrough;
    std::map<std::string, std::string> m_rename;
    std::set<std::string> m_active;          // definitions currently being resolved (cycle check)
    std::map<std::string, int> m_varCache;   // resolved global definitions (ids in `out`)
    AstPool* m_out = nullptr;
    int m_depth = 0;
    int m_loopCounter = 0;   // unique names for sum/product/integral index variables

    int go(const AstPool& src, int node, const Scope& scope);
    int call(const std::string& name, std::vector<int> args);
    std::vector<int> elements(int node);
};

// Broadcast: returns one root per list element (or just {root} when there are no lists).
// Throws ParseError if a range isn't constant or the list is too long.
std::vector<int> expandLists(AstPool& pool, int root);

int fold(AstPool& pool, int root);
// Copy of `root` with the named variables replaced by numbers.
int substitute(AstPool& pool, int root, const std::map<std::string, double>& values);
double evalConstant(const AstPool& pool, int root);  // NaN if not constant
bool containsVar(const AstPool& pool, int root, const std::string& name);
void collectVars(const AstPool& pool, int root, std::set<std::string>& out);

// Variable name -> operand op for the graph coordinates in use (e.g. {"x", OP_X}, {"θ", OP_T}).
using ParamMap = std::map<std::string, BcOp>;
Program emit(const AstPool& pool, int root, const ParamMap& params);  // throws ParseError
// As above; variables named in `paramNames` compile to OP_PARAM (evaluate(..., params)).
Program emitWithParams(const AstPool& pool, int root, const ParamMap& params,
                       const std::vector<std::string>& paramNames);

constexpr int kMaxListLength = 10000;

} // namespace calc
