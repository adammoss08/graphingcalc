#include "math/Parser.hpp"
#include "math/Latex.hpp"
#include "math/MathTree.hpp"

#include <cmath>

namespace calc {

namespace {

enum class Tk : uint8_t { Num, Ident, Const, OpName, Sym, Struct, Ellipsis, End };

struct Token {
    Tk kind = Tk::End;
    double num = 0;
    std::string name;
    char32_t ch = 0;
    const Node* node = nullptr;
};

bool isLetter(char32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isDigit(char32_t c) { return c >= '0' && c <= '9'; }
bool isGreek(char32_t c) { return c >= 0x391 && c <= 0x3C9; }

std::string symText(char32_t c) { return toUtf8(std::u32string(1, c)); }

std::vector<Token> lex(const Block& b) {
    std::vector<Token> out;
    std::vector<int> span;
    operatorSpans(b, span);
    const auto& nodes = b.nodes;
    int n = int(nodes.size());
    auto symAt = [&](int i) -> char32_t {
        return i < n && nodes[i]->kind == NodeKind::Symbol ? nodes[i]->ch : 0;
    };
    for (int i = 0; i < n;) {
        const Node* nd = nodes[i].get();
        if (nd->kind != NodeKind::Symbol) {
            Token t; t.kind = Tk::Struct; t.node = nd;
            out.push_back(t);
            ++i;
            continue;
        }
        char32_t c = nd->ch;
        if (c == '.' && symAt(i + 1) == '.' && symAt(i + 2) == '.') {
            Token t; t.kind = Tk::Ellipsis;
            out.push_back(t);
            i += 3;
            continue;
        }
        if (c == sym::Ellipsis) {
            Token t; t.kind = Tk::Ellipsis;
            out.push_back(t);
            ++i;
            continue;
        }
        if (isDigit(c) || (c == '.' && isDigit(symAt(i + 1)))) {
            std::string digits;
            bool dot = false;
            while (i < n) {
                char32_t d = symAt(i);
                if (isDigit(d)) digits += char(d);
                else if (d == '.' && !dot && symAt(i + 1) != '.') { dot = true; digits += '.'; }
                else break;
                ++i;
            }
            Token t; t.kind = Tk::Num; t.num = std::strtod(digits.c_str(), nullptr);
            out.push_back(t);
            continue;
        }
        if (span[i] > 0) {
            Token t; t.kind = Tk::OpName;
            for (int k = 0; k < span[i]; ++k) t.name += char(nodes[i + k]->ch);
            out.push_back(t);
            i += span[i];
            continue;
        }
        if (isLetter(c) || isGreek(c)) {
            Token t;
            if (c == sym::Pi) { t.kind = Tk::Const; t.name = "pi"; }
            else if (c == sym::Tau) { t.kind = Tk::Const; t.name = "tau"; }
            else {
                t.kind = Tk::Ident;
                const Block* sub = nullptr;
                if (i + 1 < n && nodes[i + 1]->kind == NodeKind::Sub) {
                    sub = nodes[i + 1]->blocks[0].get();
                    ++i;
                }
                t.name = identifierName(c, sub);
                while (i + 1 < n && symAt(i + 1) == '\'') { t.num += 1; ++i; }  // f'(x)
            }
            out.push_back(t);
            ++i;
            continue;
        }
        if (c == sym::Infinity) {
            Token t; t.kind = Tk::Const; t.name = "infinity";
            out.push_back(t);
            ++i;
            continue;
        }
        Token t; t.kind = Tk::Sym; t.ch = c == sym::Minus ? U'-' : c;
        out.push_back(t);
        ++i;
    }
    out.push_back(Token{});
    return out;
}

bool isRelation(char32_t c) { return c == '=' || c == '<' || c == '>' || c == sym::Le || c == sym::Ge; }

Cmp toCmp(char32_t c) {
    switch (c) {
    case '<': return Cmp::Lt;
    case '>': return Cmp::Gt;
    case sym::Le: return Cmp::Le;
    case sym::Ge: return Cmp::Ge;
    default: return Cmp::Eq;
    }
}

bool isTrig(const std::string& n) {
    return n == "sin" || n == "cos" || n == "tan" || n == "sec" || n == "csc" || n == "cot" ||
           n == "sinh" || n == "cosh" || n == "tanh" || n == "sech" || n == "csch" || n == "coth";
}

class Parser {
public:
    Parser(const Block& b, AstPool& pool, const std::set<std::string>& fns)
        : m_toks(lex(b)), m_pool(pool), m_fns(fns) {}
    Parser(std::vector<Token> toks, AstPool& pool, const std::set<std::string>& fns)
        : m_toks(std::move(toks)), m_pool(pool), m_fns(fns) {
        if (m_toks.empty() || m_toks.back().kind != Tk::End) m_toks.push_back(Token{});
    }

    Statement statement() {
        Statement st;
        st.exprs.push_back(expr());
        if (peek().kind == Tk::Sym && peek().ch == '~') {
            next();
            st.regression = true;
            st.exprs.push_back(expr());
            expectEnd();
            return st;
        }
        while (peek().kind == Tk::Sym && isRelation(peek().ch)) {
            st.rels.push_back(toCmp(next().ch));
            st.exprs.push_back(expr());
        }
        if (st.rels.empty() && peek().kind == Tk::Sym && peek().ch == ',') {
            st.topLevelList = true;
            while (peek().kind == Tk::Sym && peek().ch == ',') {
                next();
                st.exprs.push_back(expr());
            }
        }
        expectEnd();
        return st;
    }

    int wholeExpr() {
        int e = expr();
        expectEnd();
        return e;
    }

    // Comma-separated expressions filling the whole token stream.
    std::vector<int> commaList() {
        std::vector<int> items;
        if (peek().kind == Tk::End) return items;
        items.push_back(expr());
        while (peek().kind == Tk::Sym && peek().ch == ',') {
            next();
            items.push_back(expr());
        }
        expectEnd();
        return items;
    }

    int listLiteral() {
        // [a, b, c] or [a ... b] or [a, b, ..., c]
        std::vector<int> items;
        int ellipsisAt = -1;
        AstNode list;
        if (peek().kind == Tk::End) { list.op = AstOp::List; return m_pool.add(list); }
        while (true) {
            if (peek().kind == Tk::Ellipsis) {
                if (ellipsisAt >= 0) throw ParseError("Only one '...' allowed in a list");
                next();
                ellipsisAt = int(items.size());
                if (peek().kind == Tk::Sym && peek().ch == ',') next();
                continue;
            }
            items.push_back(expr());
            if (peek().kind == Tk::Ellipsis) continue;
            if (peek().kind == Tk::Sym && peek().ch == ',') { next(); continue; }
            break;
        }
        expectEnd();
        if (ellipsisAt < 0) {
            list.op = AstOp::List;
            list.kids = items;
            return m_pool.add(list);
        }
        if (ellipsisAt == 0 || ellipsisAt >= int(items.size()))
            throw ParseError("A list range needs a start and an end");
        AstNode r;
        r.op = AstOp::Range;
        r.kids = {items[0], ellipsisAt >= 2 ? items[1] : -1, items.back()};
        return m_pool.add(r);
    }

    int piecewise() {
        AstNode pw;
        pw.op = AstOp::Piecewise;
        if (peek().kind == Tk::End) throw ParseError("Empty piecewise");
        while (true) {
            int first = expr();
            if (peek().kind == Tk::Sym && isRelation(peek().ch)) {
                int cond = conditionChain(first);
                int val;
                if (peek().kind == Tk::Sym && peek().ch == ':') { next(); val = expr(); }
                else val = m_pool.num(1);
                if (pw.hasDefault) throw ParseError("The default value must come last");
                pw.kids.push_back(cond);
                pw.kids.push_back(val);
            } else {
                if (pw.hasDefault) throw ParseError("Piecewise has more than one default");
                pw.kids.push_back(first);
                pw.hasDefault = true;
            }
            if (peek().kind == Tk::Sym && peek().ch == ',') { next(); continue; }
            break;
        }
        expectEnd();
        return m_pool.add(pw);
    }

private:
    std::vector<Token> m_toks;
    size_t m_pos = 0;
    AstPool& m_pool;
    const std::set<std::string>& m_fns;

    const Token& peek(int ahead = 0) const { return m_toks[std::min(m_pos + ahead, m_toks.size() - 1)]; }
    const Token& next() { const Token& t = m_toks[m_pos]; if (m_pos + 1 < m_toks.size()) ++m_pos; return t; }

    void expectEnd() {
        const Token& t = peek();
        if (t.kind == Tk::End) return;
        if (t.kind == Tk::Sym) throw ParseError("Unexpected '" + symText(t.ch) + "'");
        throw ParseError("Unexpected input");
    }

    int conditionChain(int first) {
        std::vector<int> terms{first};
        std::vector<Cmp> rels;
        while (peek().kind == Tk::Sym && isRelation(peek().ch)) {
            rels.push_back(toCmp(next().ch));
            terms.push_back(expr());
        }
        if (rels.size() == 1) {
            AstNode c; c.op = AstOp::Compare; c.cmp = rels[0]; c.kids = {terms[0], terms[1]};
            return m_pool.add(c);
        }
        AstNode a; a.op = AstOp::And;
        for (size_t k = 0; k < rels.size(); ++k) {
            AstNode c; c.op = AstOp::Compare; c.cmp = rels[k]; c.kids = {terms[k], terms[k + 1]};
            a.kids.push_back(m_pool.add(c));
        }
        return m_pool.add(a);
    }

    int expr() { return sum(); }

    int sum() {
        int left = product(false);
        while (peek().kind == Tk::Sym && (peek().ch == '+' || peek().ch == '-')) {
            char32_t op = next().ch;
            int right = product(false);
            left = m_pool.binary(op == '+' ? AstOp::Add : AstOp::Sub, left, right);
        }
        return left;
    }

    bool startsFactor(const Token& t) const {
        switch (t.kind) {
        case Tk::Num: case Tk::Ident: case Tk::Const: case Tk::OpName: return true;
        case Tk::Struct: return t.node->kind != NodeKind::Sup && t.node->kind != NodeKind::Sub;
        default: return false;
        }
    }

    int product(bool stopAtOpName) {
        int left = unary(stopAtOpName);
        while (true) {
            const Token& t = peek();
            if (t.kind == Tk::Sym && (t.ch == sym::CDot || t.ch == sym::Times)) {
                next();
                left = m_pool.binary(AstOp::Mul, left, unary(stopAtOpName));
                continue;
            }
            if (startsFactor(t) && !(stopAtOpName && t.kind == Tk::OpName)) {
                left = m_pool.binary(AstOp::Mul, left, power());
                continue;
            }
            return left;
        }
    }

    int unary(bool stopAtOpName) {
        if (peek().kind == Tk::Sym && peek().ch == '-') {
            next();
            return m_pool.unary(AstOp::Neg, unary(stopAtOpName));
        }
        if (peek().kind == Tk::Sym && peek().ch == '+') {
            next();
            return unary(stopAtOpName);
        }
        return power();
    }

    int power() {
        int base = atom();
        while (true) {
            const Token& t = peek();
            if (t.kind == Tk::Struct && t.node->kind == NodeKind::Sup) {
                next();
                base = m_pool.binary(AstOp::Pow, base, block(*t.node->blocks[0], "exponent"));
                continue;
            }
            if (t.kind == Tk::Sym && t.ch == '!') {
                next();
                base = m_pool.unary(AstOp::Factorial, base);
                continue;
            }
            return base;
        }
    }

    int block(const Block& b, const char* what) {
        if (b.nodes.empty()) throw ParseError(std::string("Missing ") + what);
        Parser p(b, m_pool, m_fns);
        return p.wholeExpr();
    }

    std::vector<int> argsOf(const Node* paren) {
        Parser p(*paren->blocks[0], m_pool, m_fns);
        return p.commaList();
    }

    int atom() {
        Token t = next();
        switch (t.kind) {
        case Tk::Num: return m_pool.num(t.num);
        case Tk::Const: {
            AstNode n; n.op = AstOp::Const; n.name = t.name;
            return m_pool.add(n);
        }
        case Tk::Ident: {
            const Token& nx = peek();
            if (m_fns.count(t.name) && nx.kind == Tk::Struct && nx.node->kind == NodeKind::Paren &&
                nx.node->openCh == '(') {
                next();
                auto args = argsOf(nx.node);
                if (args.empty()) throw ParseError("Function '" + t.name + "' needs an argument");
                int c = m_pool.call(t.name, args, AstOp::UserCall);
                m_pool[c].num = t.num;  // primes
                return c;
            }
            if (t.num > 0) throw ParseError("Only functions can be differentiated with '");
            int v = m_pool.var(t.name);
            if (nx.kind == Tk::Struct && nx.node->kind == NodeKind::Paren && nx.node->openCh == '[') {
                next();
                Parser p(*nx.node->blocks[0], m_pool, m_fns);
                int idx = p.wholeExpr();
                return m_pool.binary(AstOp::Index, v, idx);
            }
            return v;
        }
        case Tk::OpName: return operatorCall(t.name);
        case Tk::Struct: return structure(t.node);
        case Tk::Sym:
            if (t.ch == ',') throw ParseError("Unexpected ','");
            throw ParseError("Unexpected '" + symText(t.ch) + "'");
        case Tk::Ellipsis: throw ParseError("'...' is only allowed in a list");
        case Tk::End: throw ParseError("Incomplete expression");
        }
        return -1;
    }

    int operatorCall(std::string name) {
        int supExpr = -1;
        int logBase = -1;
        // sin^2 x, log_2 x
        while (peek().kind == Tk::Struct) {
            const Node* n = peek().node;
            if (n->kind == NodeKind::Sup && supExpr < 0) { next(); supExpr = block(*n->blocks[0], "exponent"); continue; }
            if (n->kind == NodeKind::Sub && name == "log" && logBase < 0) { next(); logBase = block(*n->blocks[0], "log base"); continue; }
            break;
        }
        if (supExpr >= 0 && isTrig(name)) {
            const AstNode& s = m_pool[supExpr];
            if (s.op == AstOp::Neg && m_pool[s.kids[0]].op == AstOp::Num && m_pool[s.kids[0]].num == 1) {
                name = "arc" + name;
                supExpr = -1;
            }
        }
        std::vector<int> args;
        const Token& nx = peek();
        if (nx.kind == Tk::Struct && nx.node->kind == NodeKind::Paren && nx.node->openCh == '(') {
            next();
            args = argsOf(nx.node);
        } else if (startsFactor(nx) || (nx.kind == Tk::Sym && nx.ch == '-')) {
            args.push_back(product(true));
        }
        if (args.empty()) throw ParseError("Function '" + name + "' needs an argument");
        int call;
        if (logBase >= 0) {
            args.insert(args.begin(), logBase);
            call = m_pool.call("logb", args);
        } else {
            call = m_pool.call(name, args);
        }
        if (supExpr >= 0) call = m_pool.binary(AstOp::Pow, call, supExpr);
        return call;
    }

    // \frac{d}{dx}: numerator is the letter d, denominator is d followed by one variable.
    static bool isDerivativeOperator(const Node* frac, std::string& var) {
        const Block& num = *frac->blocks[0];
        const Block& den = *frac->blocks[1];
        if (num.nodes.size() != 1 || num.nodes[0]->kind != NodeKind::Symbol || num.nodes[0]->ch != 'd') return false;
        if (den.nodes.size() < 2 || den.nodes.size() > 3) return false;
        if (den.nodes[0]->kind != NodeKind::Symbol || den.nodes[0]->ch != 'd') return false;
        const Node* v = den.nodes[1].get();
        if (v->kind != NodeKind::Symbol || !(isLetter(v->ch) || isGreek(v->ch))) return false;
        const Block* sub = nullptr;
        if (den.nodes.size() == 3) {
            if (den.nodes[2]->kind != NodeKind::Sub) return false;
            sub = den.nodes[2]->blocks[0].get();
        }
        var = identifierName(v->ch, sub);
        return true;
    }

    static bool endsIntegrand(const Token& t) {
        if (t.kind == Tk::End) return true;
        if (t.kind != Tk::Sym) return false;
        return t.ch == '+' || t.ch == '-' || t.ch == ',' || t.ch == ':' || isRelation(t.ch);
    }

    int bigOp(const Node* n) {
        AstNode op;
        const Block& lower = *n->blocks[0];
        const Block& upper = *n->blocks[1];
        if (n->ch == sym::Integral) {
            op.op = AstOp::Integral;
            int lo = block(lower, "lower limit"), hi = block(upper, "upper limit");
            // The integrand runs up to a trailing "d<variable>".
            size_t k = m_pos;
            for (; k + 1 < m_toks.size(); ++k) {
                if (m_toks[k].kind == Tk::Ident && m_toks[k].name == "d" && m_toks[k + 1].kind == Tk::Ident &&
                    endsIntegrand(m_toks[std::min(k + 2, m_toks.size() - 1)]))
                    break;
            }
            if (k + 1 >= m_toks.size()) throw ParseError("An integral needs d followed by its variable (e.g. dt)");
            op.name = m_toks[k + 1].name;
            std::vector<Token> body(m_toks.begin() + m_pos, m_toks.begin() + k);
            if (body.empty()) throw ParseError("Missing integrand");
            Parser bp(std::move(body), m_pool, m_fns);
            int b = bp.wholeExpr();
            m_pos = k + 2;
            op.kids = {lo, hi, b};
            return m_pool.add(op);
        }
        op.op = n->ch == sym::Sum ? AstOp::Sum : AstOp::Prod;
        // lower block: <index> = <start>
        Parser lp(lower, m_pool, m_fns);
        const Token& idx = lp.next();
        if (idx.kind != Tk::Ident) throw ParseError("Write the lower limit like n=1");
        op.name = idx.name;
        const Token& eq = lp.next();
        if (eq.kind != Tk::Sym || eq.ch != '=') throw ParseError("Write the lower limit like n=1");
        int lo = lp.wholeExpr();
        int hi = block(upper, "upper limit");
        const Token& nx = peek();
        if (!(startsFactor(nx) || (nx.kind == Tk::Sym && nx.ch == '-')))
            throw ParseError(std::string(n->ch == sym::Sum ? "Sum" : "Product") + " needs something to add up");
        int body = product(false);
        op.kids = {lo, hi, body};
        return m_pool.add(op);
    }

    int structure(const Node* n) {
        switch (n->kind) {
        case NodeKind::Frac: {
            std::string var;
            if (isDerivativeOperator(n, var)) {
                // d/dx applies to the following term, like an operator name.
                int arg;
                const Token& nx = peek();
                if (nx.kind == Tk::Struct && nx.node->kind == NodeKind::Paren && nx.node->openCh == '(') {
                    next();
                    Parser p(*nx.node->blocks[0], m_pool, m_fns);
                    arg = p.wholeExpr();
                } else if (startsFactor(nx) || (nx.kind == Tk::Sym && nx.ch == '-')) {
                    arg = product(false);
                } else {
                    throw ParseError("d/d" + var + " needs an expression to differentiate");
                }
                AstNode d; d.op = AstOp::Deriv; d.name = var; d.kids = {arg};
                return m_pool.add(d);
            }
            return m_pool.binary(AstOp::Div, block(*n->blocks[0], "numerator"),
                                 block(*n->blocks[1], "denominator"));
        }
        case NodeKind::Sqrt: return m_pool.call("sqrt", {block(*n->blocks[0], "radicand")});
        case NodeKind::NthRoot: {
            int idx = n->blocks[0]->nodes.empty() ? m_pool.num(2) : block(*n->blocks[0], "root index");
            return m_pool.call("nthroot", {idx, block(*n->blocks[1], "radicand")});
        }
        case NodeKind::Paren: {
            Parser p(*n->blocks[0], m_pool, m_fns);
            switch (n->openCh) {
            case '[': return p.listLiteral();
            case '{': return p.piecewise();
            case '|': return m_pool.call("abs", {p.wholeExpr()});
            default: {
                auto items = p.commaList();
                if (items.empty()) throw ParseError("Empty parentheses");
                if (items.size() == 1) return items[0];
                if (items.size() > 3) throw ParseError("Points can have at most 3 coordinates");
                AstNode tup; tup.op = AstOp::Tuple; tup.kids = items;
                return m_pool.add(tup);
            }
            }
        }
        case NodeKind::BigOp: return bigOp(n);
        case NodeKind::Sup: throw ParseError("Exponent needs a base");
        case NodeKind::Sub: throw ParseError("Subscript needs a variable");
        default: throw ParseError("Unexpected input");
        }
    }
};

} // namespace

std::string identifierName(char32_t letter, const Block* subscript) {
    std::string name = toUtf8(std::u32string(1, letter));
    if (subscript && !subscript->nodes.empty()) name += "_{" + blockToLatex(*subscript) + "}";
    return name;
}

Statement parseStatement(const Block& root, AstPool& pool, const std::set<std::string>& functionNames) {
    Parser p(root, pool, functionNames);
    return p.statement();
}

std::string peekFunctionDefinitionName(const Block& root) {
    const auto& n = root.nodes;
    if (n.size() < 3) return {};
    size_t i = 0;
    if (n[0]->kind != NodeKind::Symbol || !(isLetter(n[0]->ch) || isGreek(n[0]->ch))) return {};
    const Block* sub = nullptr;
    i = 1;
    if (n[i]->kind == NodeKind::Sub) { sub = n[i]->blocks[0].get(); ++i; }
    if (i + 1 >= n.size()) return {};
    if (n[i]->kind != NodeKind::Paren || n[i]->openCh != '(') return {};
    if (n[i + 1]->kind != NodeKind::Symbol || n[i + 1]->ch != '=') return {};
    // Operator names (e.g. "ln(x)=...") can't be redefined.
    std::vector<int> span;
    operatorSpans(root, span);
    if (span[0] > 0) return {};
    // Parameters must be single identifiers separated by commas.
    for (auto& c : n[i]->blocks[0]->nodes) {
        if (c->kind == NodeKind::Sub) continue;
        if (c->kind != NodeKind::Symbol) return {};
        if (!(isLetter(c->ch) || isGreek(c->ch) || c->ch == ',')) return {};
    }
    return identifierName(n[0]->ch, sub);
}

int AstPool::import(const AstPool& src, int root) {
    AstNode n = src[root];
    for (int& k : n.kids)
        if (k >= 0) k = import(src, k);
    return add(std::move(n));
}

std::string AstPool::debugString(int root) const {
    const AstNode& n = nodes[root];
    auto kid = [&](int k) { return n.kids[k] >= 0 ? debugString(n.kids[k]) : std::string("_"); };
    auto join = [&](const char* sep) {
        std::string s;
        for (size_t k = 0; k < n.kids.size(); ++k) { if (k) s += sep; s += kid(int(k)); }
        return s;
    };
    switch (n.op) {
    case AstOp::Num: {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", n.num);
        return buf;
    }
    case AstOp::Var: case AstOp::Const: return n.name;
    case AstOp::Add: return "(" + kid(0) + "+" + kid(1) + ")";
    case AstOp::Sub: return "(" + kid(0) + "-" + kid(1) + ")";
    case AstOp::Mul: return "(" + kid(0) + "*" + kid(1) + ")";
    case AstOp::Div: return "(" + kid(0) + "/" + kid(1) + ")";
    case AstOp::Pow: return "(" + kid(0) + "^" + kid(1) + ")";
    case AstOp::Neg: return "(-" + kid(0) + ")";
    case AstOp::Factorial: return "(" + kid(0) + "!)";
    case AstOp::Call: case AstOp::UserCall: return n.name + "(" + join(",") + ")";
    case AstOp::Tuple: return "(" + join(",") + ")";
    case AstOp::List: return "[" + join(",") + "]";
    case AstOp::Range: return "[" + kid(0) + "," + kid(1) + "..." + kid(2) + "]";
    case AstOp::Index: return kid(0) + "[" + kid(1) + "]";
    case AstOp::Compare: {
        static const char* ops[] = {"?", "<", "<=", ">", ">=", "="};
        return "(" + kid(0) + ops[int(n.cmp)] + kid(1) + ")";
    }
    case AstOp::And: return "(" + join("&") + ")";
    case AstOp::Piecewise: return "{" + join(";") + (n.hasDefault ? "|d" : "") + "}";
    case AstOp::Deriv: return "d/d" + n.name + "(" + kid(0) + ")";
    case AstOp::Sum: case AstOp::Prod: case AstOp::Integral: {
        const char* s = n.op == AstOp::Sum ? "sum" : n.op == AstOp::Prod ? "prod" : "int";
        return std::string(s) + "[" + n.name + "=" + kid(0) + ".." + kid(1) + "](" + kid(2) + ")";
    }
    }
    return "?";
}

} // namespace calc
