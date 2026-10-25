#include "math/Analyzer.hpp"
#include "math/Compiler.hpp"
#include "math/MathTree.hpp"
#include "math/Parser.hpp"
#include "math/Regression.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>

namespace calc {

namespace {

const std::string kTheta = "\xCE\xB8";  // θ

struct ParsedRow {
    bool empty = true;
    std::string error;
    AstPool pool;
    Statement st;
    enum class Def { None, Var, Fn } def = Def::None;
    std::string name;
    std::vector<std::string> params;
    int defRoot = -1;
};

bool isSingleVar(const AstPool& p, int id, std::string* name = nullptr) {
    if (p[id].op != AstOp::Var) return false;
    if (name) *name = p[id].name;
    return true;
}

bool isPlainNumber(const AstPool& p, int id, double& v) {
    const AstNode& n = p[id];
    if (n.op == AstOp::Num) { v = n.num; return true; }
    if (n.op == AstOp::Neg && p[n.kids[0]].op == AstOp::Num) { v = -p[n.kids[0]].num; return true; }
    return false;
}

void mix(uint64_t& h, uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); }
void mixDouble(uint64_t& h, double d) { uint64_t u; std::memcpy(&u, &d, 8); mix(h, u); }

class RowCompiler {
public:
    RowCompiler(const Definitions& defs, const AnalyzerSettings& s) : m_resolver(defs, s.degrees) {}

    int resolve(const ParsedRow& row, int root) { return m_resolver.resolve(row.pool, root, m_pool); }
    AstPool& pool() { return m_pool; }

    // Variables in `node` not in `allowed` become missing (undefined) variables.
    void checkVars(int node, std::initializer_list<const char*> allowed) {
        std::set<std::string> vars;
        collectVars(m_pool, node, vars);
        for (auto& v : vars) {
            bool ok = false;
            for (auto* a : allowed) ok |= v == a;
            if (!ok) m_extraMissing.insert(v);
        }
    }

    bool uses(int node, const std::string& var) { return containsVar(m_pool, node, var); }

    std::set<std::string> missing() const {
        std::set<std::string> m = m_resolver.missing();
        m.insert(m_extraMissing.begin(), m_extraMissing.end());
        return m;
    }

    Program program(int node, const ParamMap& params) { return emit(m_pool, fold(m_pool, node), params); }

    std::vector<int> broadcast(int node) { return expandLists(m_pool, node); }

private:
    Resolver m_resolver;
    AstPool m_pool;
    std::set<std::string> m_extraMissing;
};

const ParamMap kParamsX = {{"x", OP_X}};
const ParamMap kParamsY = {{"y", OP_Y}};
const ParamMap kParamsT = {{"t", OP_T}};
const ParamMap kParamsTheta = {{kTheta, OP_T}};
const ParamMap kParamsXY = {{"x", OP_X}, {"y", OP_Y}, {"r", OP_R}, {kTheta, OP_THETA}};

void setMissingError(RowAnalysis& out, const std::set<std::string>& missing) {
    out.kind = RowKind::Error;
    std::string list;
    for (auto& m : missing) {
        if (!list.empty()) list += ", ";
        list += "'" + m + "'";
        if (m != "x" && m != "y" && m != "r" && m != kTheta) out.sliderSuggestions.push_back(m);
    }
    out.error = missing.size() == 1 ? list + " is not defined" : "Undefined variables: " + list;
}

// region(x, y) > 0 where `lhs rel rhs` holds.
int regionNode(AstPool& p, int lhs, int rhs, Cmp rel) {
    bool lessThan = rel == Cmp::Lt || rel == Cmp::Le;
    return lessThan ? p.binary(AstOp::Sub, rhs, lhs) : p.binary(AstOp::Sub, lhs, rhs);
}

void finishHash(RowAnalysis& r) {
    uint64_t h = 0x12345;
    mix(h, uint64_t(r.kind));
    mix(h, uint64_t(r.graph));
    mix(h, uint64_t(r.rel));
    mix(h, r.strict);
    mixDouble(h, r.tMin);
    mixDouble(h, r.tMax);
    for (auto& p : r.pieces) {
        mix(h, p.f.hash());
        mix(h, p.g.hash());
        mix(h, p.region.hash());
        mixDouble(h, p.px);
        mixDouble(h, p.py);
    }
    r.hash = h;
}

void analyzeGraphRow(const ParsedRow& row, const Definitions& defs, const AnalyzerSettings& settings,
                     const std::set<std::string>& sliders, RowAnalysis& out) {
    RowCompiler rc(defs, settings);
    AstPool& P = rc.pool();
    const Statement& st = row.st;

    auto failIfMissing = [&]() -> bool {
        auto m = rc.missing();
        if (m.empty()) return false;
        setMissingError(out, m);
        return true;
    };

    // ---- bare expression / points ----
    if (st.rels.empty()) {
        std::vector<int> items;
        for (int e : st.exprs) {
            int r = rc.resolve(row, e);
            for (int b : rc.broadcast(r)) items.push_back(b);
        }
        bool allTuples = true;
        for (int it : items) allTuples &= P[it].op == AstOp::Tuple;
        if (allTuples && !items.empty()) {
            bool param = false;
            for (int it : items) param |= rc.uses(it, "t");
            for (int it : items) rc.checkVars(it, {"t"});
            if (failIfMissing()) return;
            out.kind = RowKind::Graph;
            for (int it : items) {
                const AstNode& tup = P[it];
                if (tup.kids.size() != 2) throw ParseError("Only 2D points can be graphed");
                GraphPiece pc;
                if (param) {
                    pc.f = rc.program(tup.kids[0], kParamsT);
                    pc.g = rc.program(tup.kids[1], kParamsT);
                } else {
                    pc.px = evalConstant(P, tup.kids[0]);
                    pc.py = evalConstant(P, tup.kids[1]);
                }
                out.pieces.push_back(std::move(pc));
            }
            out.graph = param ? GraphKind::Parametric : GraphKind::Point;
            // Desmos: a point whose coordinate is a slider variable can be dragged on the graph.
            if (!param && items.size() == st.exprs.size()) {
                for (size_t k = 0; k < st.exprs.size(); ++k) {
                    const AstNode& raw = row.pool[st.exprs[k]];
                    if (raw.op != AstOp::Tuple || raw.kids.size() != 2) continue;
                    std::string vx, vy;
                    if (isSingleVar(row.pool, raw.kids[0], &vx) && sliders.count(vx)) out.pieces[k].dragX = vx;
                    if (isSingleVar(row.pool, raw.kids[1], &vy) && sliders.count(vy)) out.pieces[k].dragY = vy;
                }
            }
            return;
        }
        if (items.size() != 1 && st.topLevelList) throw ParseError("Unexpected ','");
        for (int it : items) if (P[it].op == AstOp::Tuple) throw ParseError("Can't mix points and numbers");
        bool usesX = false, usesY = false;
        for (int it : items) { usesX |= rc.uses(it, "x"); usesY |= rc.uses(it, "y"); }
        if (usesY) {
            for (int it : items) rc.checkVars(it, {"x", "y"});
            if (failIfMissing()) return;
            out.kind = RowKind::None;  // Desmos draws nothing for a bare expression in y
            return;
        }
        for (int it : items) rc.checkVars(it, {"x"});
        if (failIfMissing()) return;
        if (usesX) {
            out.kind = RowKind::Graph;
            out.graph = GraphKind::ExplicitY;
            for (int it : items) {
                GraphPiece pc;
                pc.f = rc.program(it, kParamsX);
                out.pieces.push_back(std::move(pc));
            }
            return;
        }
        out.kind = RowKind::Value;
        if (items.size() == 1) {
            out.showValue = true;
            out.value = evalConstant(P, items[0]);
        }
        return;
    }

    // ---- chained inequality: a < y < b ----
    if (st.rels.size() >= 2) {
        int region = -1;
        for (size_t k = 0; k < st.rels.size(); ++k) {
            if (st.rels[k] == Cmp::Eq) throw ParseError("Can't chain '=' with other comparisons");
            int a = rc.resolve(row, st.exprs[k]);
            int b = rc.resolve(row, st.exprs[k + 1]);
            int rk = regionNode(P, a, b, st.rels[k]);
            region = region < 0 ? rk : P.call("min", {region, rk});
        }
        rc.checkVars(region, {"x", "y", "r", kTheta.c_str()});
        if (failIfMissing()) return;
        out.kind = RowKind::Graph;
        out.graph = GraphKind::Implicit;
        out.rel = st.rels[0];
        out.strict = isStrict(st.rels[0]);
        out.shadeOnly = true;
        for (int b : rc.broadcast(region)) {
            GraphPiece pc;
            pc.region = rc.program(b, kParamsXY);
            pc.f = pc.region;
            out.pieces.push_back(std::move(pc));
        }
        return;
    }

    // ---- one relation ----
    Cmp rel = st.rels[0];
    int lhsRaw = st.exprs[0], rhsRaw = st.exprs[1];
    std::string lname, rname;
    bool lhsVar = isSingleVar(row.pool, lhsRaw, &lname);
    bool rhsVar = isSingleVar(row.pool, rhsRaw, &rname);
    int lhs = rc.resolve(row, lhsRaw);
    int rhs = rc.resolve(row, rhsRaw);
    out.kind = RowKind::Graph;
    out.rel = rel;
    out.strict = isStrict(rel);

    auto explicitIn = [&](const std::string& dep, const std::string& indep, int fnode, bool flipped) -> bool {
        // dep = f(indep)
        if (rc.uses(fnode, dep)) return false;
        rc.checkVars(fnode, {indep.c_str()});
        if (failIfMissing()) return true;
        out.graph = dep == "y" ? GraphKind::ExplicitY : GraphKind::ExplicitX;
        Cmp effective = rel;
        if (flipped) {  // f rel y  ==>  y rel' f
            if (rel == Cmp::Lt) effective = Cmp::Gt;
            else if (rel == Cmp::Gt) effective = Cmp::Lt;
            else if (rel == Cmp::Le) effective = Cmp::Ge;
            else if (rel == Cmp::Ge) effective = Cmp::Le;
        }
        out.rel = effective;
        const ParamMap& pm = dep == "y" ? kParamsX : kParamsY;
        for (int b : rc.broadcast(fnode)) {
            GraphPiece pc;
            pc.f = rc.program(b, pm);
            if (effective != Cmp::Eq) {
                int depVar = P.var(dep);
                pc.region = rc.program(regionNode(P, depVar, b, effective), kParamsXY);
            }
            out.pieces.push_back(std::move(pc));
        }
        return true;
    };

    if (lhsVar && lname == "y" && explicitIn("y", "x", rhs, false)) return;
    if (rhsVar && rname == "y" && explicitIn("y", "x", lhs, true)) return;
    if (lhsVar && lname == "x" && explicitIn("x", "y", rhs, false)) return;
    if (rhsVar && rname == "x" && explicitIn("x", "y", lhs, true)) return;

    // r = f(θ)
    if (rel == Cmp::Eq && lhsVar && lname == "r" && !rc.uses(rhs, "r") && !rc.uses(rhs, "x") &&
        !rc.uses(rhs, "y")) {
        rc.checkVars(rhs, {kTheta.c_str()});
        if (failIfMissing()) return;
        out.graph = GraphKind::Polar;
        out.tMin = 0;
        out.tMax = 12 * M_PI;
        for (int b : rc.broadcast(rhs)) {
            GraphPiece pc;
            pc.f = rc.program(b, kParamsTheta);
            out.pieces.push_back(std::move(pc));
        }
        return;
    }

    // Implicit F(x, y) rel 0
    int F = rel == Cmp::Eq ? P.binary(AstOp::Sub, lhs, rhs) : regionNode(P, lhs, rhs, rel);
    rc.checkVars(F, {"x", "y", "r", kTheta.c_str()});
    if (failIfMissing()) return;
    out.graph = GraphKind::Implicit;
    for (int b : rc.broadcast(F)) {
        GraphPiece pc;
        pc.f = rc.program(b, kParamsXY);
        if (pc.f.isConstant()) {
            // e.g. "1=2" or "3<4": nothing to draw
            out.kind = RowKind::None;
            out.graph = GraphKind::None;
            out.pieces.clear();
            return;
        }
        if (rel != Cmp::Eq) pc.region = pc.f;
        out.pieces.push_back(std::move(pc));
    }
}

// Fits `data ~ model` by linear least squares. Fitted parameters are added to `defs` (as Desmos
// does, so other rows can use them) and the model is graphed with the data list replaced by x.
void analyzeRegression(const ParsedRow& row, Definitions& defs, AstPool& paramPool,
                       const AnalyzerSettings& settings, RowAnalysis& out) {
    const Statement& st = row.st;
    out.regression = true;
    // Data
    RowCompiler dataRc(defs, settings);
    int data = dataRc.resolve(row, st.exprs[0]);
    if (!dataRc.missing().empty()) { setMissingError(out, dataRc.missing()); return; }
    std::vector<double> ys;
    for (int e : dataRc.broadcast(data)) {
        double v = evalConstant(dataRc.pool(), e);
        ys.push_back(v);
    }
    // Parameters: names in the model that aren't defined anywhere.
    std::set<std::string> params;
    {
        RowCompiler probe(defs, settings);
        probe.resolve(row, st.exprs[1]);
        params = probe.missing();
        for (const char* c : {"x", "y"}) params.erase(c);
    }
    if (params.empty()) throw ParseError("Regression needs at least one parameter to fit");
    if (params.size() > 32) throw ParseError("Too many regression parameters");
    Resolver res(defs, settings.degrees);
    res.setPassThrough(params);
    AstPool P;
    int modelRoot = res.resolve(row.pool, st.exprs[1], P);
    std::vector<int> items = expandLists(P, modelRoot);
    int n = int(items.size()), k = int(params.size());
    if (n != int(ys.size())) throw ParseError("The two sides of '~' must be lists of the same length");
    if (n < k) throw ParseError("Not enough data points for this regression");
    std::vector<std::string> names(params.begin(), params.end());

    // Compile each data item once; parameters are read from an array (OP_PARAM).
    std::vector<Program> progs;
    progs.reserve(n);
    for (int i = 0; i < n; ++i) progs.push_back(emitWithParams(P, items[i], {}, names));
    auto model = [&](const double* pv, double* f) {
        for (int i = 0; i < n; ++i) f[i] = evaluate(progs[i], 0, 0, 0, pv);
    };
    // Linear in the parameters? f(p) = f(0) + A p exactly (checked at a probe point).
    std::vector<double> A(size_t(n) * k), b(n), f0(n), fu(n), fq(n);
    std::vector<double> zero(k, 0.0), probeV(k), unit(k, 0.0);
    for (int j = 0; j < k; ++j) probeV[j] = 1.7 + 0.3 * j;
    model(zero.data(), f0.data());
    for (int j = 0; j < k; ++j) {
        unit[j] = 1;
        model(unit.data(), fu.data());
        unit[j] = 0;
        for (int i = 0; i < n; ++i) A[size_t(i) * k + j] = fu[i] - f0[i];
    }
    model(probeV.data(), fq.data());
    bool linear = true;
    for (int i = 0; i < n && linear; ++i) {
        double predicted = f0[i];
        for (int j = 0; j < k; ++j) predicted += probeV[j] * A[size_t(i) * k + j];
        linear = std::isfinite(fq[i]) && std::isfinite(predicted) &&
                 std::fabs(fq[i] - predicted) <= 1e-7 * (1 + std::fabs(fq[i]));
    }
    LeastSquaresResult fit;
    if (linear) {
        for (int i = 0; i < n; ++i) b[i] = ys[i] - f0[i];
        fit = leastSquares(A, b, n, k);
    } else {
        fit = levenbergMarquardt(n, k, model, ys);
    }
    if (!fit.ok) throw ParseError("Regression failed: the data doesn't determine every parameter");
    std::vector<double> pred(n);
    model(fit.params.data(), pred.data());
    double mean = 0, ssTot = 0, ssRes = 0;
    for (double y : ys) mean += y;
    mean /= n;
    for (int i = 0; i < n; ++i) {
        ssRes += (ys[i] - pred[i]) * (ys[i] - pred[i]);
        ssTot += (ys[i] - mean) * (ys[i] - mean);
    }
    out.rSquared = ssTot > 0 ? 1 - ssRes / ssTot : 1;
    out.regressionUsedMps = fit.usedMps;
    for (int j = 0; j < k; ++j) {
        out.regressionParams.push_back({names[j], fit.params[j]});
        defs.vars[names[j]] = {&paramPool, paramPool.num(fit.params[j])};
    }

    // Graph the model against x: the data list variable(s) in the model are renamed to x.
    std::set<std::string> rawVars;
    collectVars(row.pool, st.exprs[1], rawVars);
    std::map<std::string, std::string> rename;
    for (auto& v : rawVars) {
        auto d = defs.vars.find(v);
        if (d == defs.vars.end()) continue;
        const AstNode& def = (*d->second.pool)[d->second.root];
        if (def.op == AstOp::List || def.op == AstOp::Range) rename[v] = "x";
    }
    out.kind = RowKind::Graph;
    if (rename.empty()) { out.kind = RowKind::Value; return; }
    Resolver gr(defs, settings.degrees);
    gr.setRename(rename);
    AstPool G;
    int curve = gr.resolve(row.pool, st.exprs[1], G);
    GraphPiece pc;
    pc.f = emit(G, fold(G, curve), kParamsX);
    out.graph = GraphKind::ExplicitY;
    out.pieces.push_back(std::move(pc));
}

// A side field (slider bound, domain, image size): a bare constant expression or point.
std::vector<double> evalField(const MathTree* t, const Definitions& defs, const AnalyzerSettings& settings,
                              const std::set<std::string>& fnNames) {
    if (!t || t->empty()) return {};
    try {
        ParsedRow pr;
        pr.st = parseStatement(t->root(), pr.pool, fnNames);
        if (!pr.st.rels.empty() || pr.st.regression || pr.st.exprs.size() != 1) return {};
        RowCompiler rc(defs, settings);
        int r = rc.resolve(pr, pr.st.exprs[0]);
        rc.checkVars(r, {});
        if (!rc.missing().empty()) return {};
        const AstNode& n = rc.pool()[r];
        std::vector<double> out;
        if (n.op == AstOp::Tuple) for (int k : n.kids) out.push_back(evalConstant(rc.pool(), k));
        else out.push_back(evalConstant(rc.pool(), r));
        return out;
    } catch (const ParseError&) {
        return {};
    }
}

} // namespace

std::vector<RowAnalysis> analyzeGraph(const std::vector<RowInput>& rows, const AnalyzerSettings& settings) {
    size_t n = rows.size();
    std::vector<ParsedRow> parsed(n);
    std::vector<RowAnalysis> out(n);

    std::set<std::string> fnNames;
    for (auto& r : rows) {
        if (!r.tree) continue;
        std::string f = peekFunctionDefinitionName(r.tree->root());
        if (!f.empty()) fnNames.insert(f);
    }

    // Parse and detect definitions.
    for (size_t i = 0; i < n; ++i) {
        ParsedRow& pr = parsed[i];
        pr.empty = !rows[i].tree || rows[i].tree->empty();
        if (pr.empty) continue;
        try {
            pr.st = parseStatement(rows[i].tree->root(), pr.pool, fnNames);
        } catch (const ParseError& e) {
            pr.error = e.what();
            continue;
        }
        const Statement& st = pr.st;
        if (st.rels.size() != 1 || st.rels[0] != Cmp::Eq) continue;
        const AstNode& lhs = pr.pool[st.exprs[0]];
        if (lhs.op == AstOp::UserCall && fnNames.count(lhs.name)) {
            std::vector<std::string> params;
            bool ok = true;
            for (int k : lhs.kids) {
                if (pr.pool[k].op != AstOp::Var) { ok = false; break; }
                params.push_back(pr.pool[k].name);
            }
            if (ok) {
                pr.def = ParsedRow::Def::Fn;
                pr.name = lhs.name;
                pr.params = params;
                pr.defRoot = st.exprs[1];
            }
        } else if (lhs.op == AstOp::Var && !isCoordinateName(lhs.name)) {
            pr.def = ParsedRow::Def::Var;
            pr.name = lhs.name;
            pr.defRoot = st.exprs[1];
        }
    }

    // Duplicate definitions are errors on every row involved.
    std::map<std::string, int> defCount;
    for (auto& pr : parsed)
        if (pr.def != ParsedRow::Def::None) defCount[pr.name]++;
    Definitions defs;
    for (size_t i = 0; i < n; ++i) {
        ParsedRow& pr = parsed[i];
        if (pr.def == ParsedRow::Def::None) continue;
        if (defCount[pr.name] > 1) {
            pr.error = "You've defined '" + pr.name + "' in more than one place";
            pr.def = ParsedRow::Def::None;
            continue;
        }
        if (pr.def == ParsedRow::Def::Var) defs.vars[pr.name] = {&pr.pool, pr.defRoot};
        else defs.fns[pr.name] = {&pr.pool, pr.defRoot, pr.params};
    }

    // Slider variables (defined as a plain number) can be driven by dragging points.
    std::set<std::string> sliders;
    for (auto& pr : parsed) {
        double v;
        if (pr.def == ParsedRow::Def::Var && isPlainNumber(pr.pool, pr.defRoot, v)) sliders.insert(pr.name);
    }

    // Regressions first: their fitted parameters become definitions other rows can use.
    AstPool paramPool;  // fitted regression parameters (defs refer to it by pointer + index)
    std::vector<bool> done(n, false);
    for (size_t i = 0; i < n; ++i) {
        ParsedRow& pr = parsed[i];
        if (pr.empty || !pr.error.empty() || !pr.st.regression) continue;
        try {
            analyzeRegression(pr, defs, paramPool, settings, out[i]);
        } catch (const ParseError& e) {
            out[i] = RowAnalysis{};
            out[i].kind = RowKind::Error;
            out[i].error = e.what();
        }
        finishHash(out[i]);
        done[i] = true;
    }

    for (size_t i = 0; i < n; ++i) {
        ParsedRow& pr = parsed[i];
        RowAnalysis& ra = out[i];
        if (done[i]) continue;
        if (pr.empty) { ra.kind = RowKind::Empty; finishHash(ra); continue; }
        if (!pr.error.empty()) { ra.kind = RowKind::Error; ra.error = pr.error; finishHash(ra); continue; }
        try {
            if (pr.def == ParsedRow::Def::Var) {
                ra.definedName = pr.name;
                double v;
                if (isPlainNumber(pr.pool, pr.defRoot, v)) {
                    ra.kind = RowKind::Slider;
                    ra.sliderValue = v;
                } else {
                    RowCompiler rc(defs, settings);
                    int r = rc.resolve(pr, pr.defRoot);
                    rc.checkVars(r, {});
                    auto m = rc.missing();
                    if (!m.empty()) setMissingError(ra, m);
                    else {
                        ra.kind = RowKind::Definition;
                        auto items = rc.broadcast(r);
                        if (items.size() == 1 && rc.pool()[items[0]].op != AstOp::Tuple) {
                            ra.showValue = true;
                            ra.value = evalConstant(rc.pool(), items[0]);
                        }
                    }
                }
            } else if (pr.def == ParsedRow::Def::Fn) {
                ra.kind = RowKind::FunctionDef;
                ra.definedName = pr.name;
                RowCompiler rc(defs, settings);
                int body = rc.resolve(pr, pr.defRoot);
                if (pr.params.size() == 1 && pr.params[0] == "x" && !rc.uses(body, "y")) {
                    // f(x) = ... is also graphed as y = f(x)
                    rc.checkVars(body, {"x"});
                    auto m = rc.missing();
                    if (!m.empty()) setMissingError(ra, m);
                    else {
                        ra.graph = GraphKind::ExplicitY;
                        for (int b : rc.broadcast(body)) {
                            GraphPiece pc;
                            pc.f = rc.program(b, kParamsX);
                            ra.pieces.push_back(std::move(pc));
                        }
                    }
                } else {
                    std::set<std::string> m = rc.missing();
                    for (auto& p : pr.params) m.erase(p);
                    if (!m.empty()) setMissingError(ra, m);
                }
            } else {
                analyzeGraphRow(pr, defs, settings, sliders, ra);
            }
        } catch (const ParseError& e) {
            ra = RowAnalysis{};
            ra.kind = RowKind::Error;
            ra.error = e.what();
        }
        if (ra.graph == GraphKind::Parametric || ra.graph == GraphKind::Polar) {
            auto lo = evalField(rows[i].domainMin, defs, settings, fnNames);
            auto hi = evalField(rows[i].domainMax, defs, settings, fnNames);
            if (lo.size() == 1 && std::isfinite(lo[0])) ra.tMin = lo[0];
            if (hi.size() == 1 && std::isfinite(hi[0])) ra.tMax = hi[0];
        }
        finishHash(ra);
    }
    for (size_t i = 0; i < n; ++i)
        for (const MathTree* f : rows[i].fields) out[i].fieldValues.push_back(evalField(f, defs, settings, fnNames));
    return out;
}

bool fractionLatex(double v, std::string& out) {
    if (!std::isfinite(v) || v == std::floor(v)) return false;
    // Continued fractions; stop at the first convergent within relative 1e-12.
    double x = std::fabs(v);
    long long h0 = 0, h1 = 1, k0 = 1, k1 = 0;
    double r = x;
    for (int it = 0; it < 40; ++it) {
        double a = std::floor(r);
        long long h2 = (long long)a * h1 + h0, k2 = (long long)a * k1 + k0;
        if (k2 > 1000000) return false;
        h0 = h1; h1 = h2; k0 = k1; k1 = k2;
        // Only fractions the double represents (to a few ulps): 1/3 yes, pi's convergents no.
        if (std::fabs(double(h1) / double(k1) - x) <= 4 * std::numeric_limits<double>::epsilon() * std::max(1.0, x)) {
            if (k1 <= 1) return false;
            out = std::string(v < 0 ? "-" : "") + "\\frac{" + std::to_string(h1) + "}{" + std::to_string(k1) + "}";
            return true;
        }
        double frac = r - a;
        if (frac < 1e-15) return false;
        r = 1.0 / frac;
    }
    return false;
}

std::string formatNumber(double v) {
    if (std::isnan(v)) return "\\operatorname{undefined}";
    if (std::isinf(v)) return v > 0 ? "\\infty" : "-\\infty";
    if (std::fabs(v) < 1e-15) return "0";
    double a = std::fabs(v);
    char buf[64];
    if (a >= 1e12 || a < 1e-6) {
        int e = int(std::floor(std::log10(a)));
        double m = v / std::pow(10.0, e);
        if (std::fabs(m) >= 9.9999999999) { m /= 10; ++e; }
        std::snprintf(buf, sizeof buf, "%.10g", m);
        return std::string(buf) + "\\times10^{" + std::to_string(e) + "}";
    }
    // 12 significant digits, never in exponent form.
    int digitsBefore = a >= 1 ? int(std::floor(std::log10(a))) + 1 : 0;
    int decimals = std::max(0, 12 - digitsBefore);
    if (a < 1) decimals = std::min(17, 12 + int(-std::floor(std::log10(a))) - 1);
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

} // namespace calc
