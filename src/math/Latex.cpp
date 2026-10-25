#include "math/Latex.hpp"
#include "math/MathTree.hpp"

#include <cctype>

namespace calc {

// Operator names LaTeX knows natively; others are written as \operatorname{...} (as Desmos does).
static bool isNativeLatexOp(const std::string& s) {
    static const char* kNative[] = {"sin", "cos", "tan", "sec", "csc", "cot", "sinh", "cosh",
                                    "tanh", "coth", "arcsin", "arccos", "arctan", "ln", "log",
                                    "exp", "min", "max", "gcd"};
    for (auto* n : kNative) if (s == n) return true;
    return false;
}

static void emitBlock(const Block& b, std::string& out);

static void emitSymbol(char32_t c, std::string& out) {
    switch (c) {
    case sym::CDot: out += "\\cdot "; return;
    case sym::Times: out += "\\times "; return;
    case sym::Le: out += "\\le "; return;
    case sym::Ge: out += "\\ge "; return;
    case sym::Infinity: out += "\\infty "; return;
    case sym::Minus: out += '-'; return;
    case '{': out += "\\{"; return;
    case '}': out += "\\}"; return;
    case '%': out += "\\%"; return;
    case '~': out += "\\sim "; return;
    default: break;
    }
    if (const char* g = greekName(c)) {
        out += '\\';
        out += g;
        out += ' ';
        return;
    }
    out += toUtf8(std::u32string(1, c));
}

static void emitBracket(char32_t c, bool open, std::string& out) {
    out += open ? "\\left" : "\\right";
    if (c == '{') out += "\\{";
    else if (c == '}') out += "\\}";
    else out += char(c);
}

static void emitBlock(const Block& b, std::string& out) {
    std::vector<int> span;
    operatorSpans(b, span);
    for (size_t i = 0; i < b.nodes.size(); ++i) {
        const Node& n = *b.nodes[i];
        if (span[i] > 0) {
            std::string name;
            for (int k = 0; k < span[i]; ++k) name += char(b.nodes[i + k]->ch);
            if (isNativeLatexOp(name)) out += "\\" + name;
            else out += "\\operatorname{" + name + "}";
            i += span[i] - 1;
            // A following letter/digit needs a separating space.
            if (i + 1 < b.nodes.size() && b.nodes[i + 1]->kind == NodeKind::Symbol &&
                std::isalnum(int(b.nodes[i + 1]->ch < 128 ? b.nodes[i + 1]->ch : 0)))
                out += ' ';
            continue;
        }
        switch (n.kind) {
        case NodeKind::Symbol: {
            // Greek/commands ending in a letter followed by a non-letter don't need the space.
            emitSymbol(n.ch, out);
            break;
        }
        case NodeKind::Frac:
            out += "\\frac{"; emitBlock(*n.blocks[0], out);
            out += "}{"; emitBlock(*n.blocks[1], out); out += "}";
            break;
        case NodeKind::Sup: out += "^{"; emitBlock(*n.blocks[0], out); out += "}"; break;
        case NodeKind::Sub: out += "_{"; emitBlock(*n.blocks[0], out); out += "}"; break;
        case NodeKind::Sqrt: out += "\\sqrt{"; emitBlock(*n.blocks[0], out); out += "}"; break;
        case NodeKind::NthRoot:
            out += "\\sqrt["; emitBlock(*n.blocks[0], out);
            out += "]{"; emitBlock(*n.blocks[1], out); out += "}";
            break;
        case NodeKind::BigOp:
            out += n.ch == sym::Sum ? "\\sum" : n.ch == sym::Prod ? "\\prod" : "\\int";
            out += "_{"; emitBlock(*n.blocks[0], out);
            out += "}^{"; emitBlock(*n.blocks[1], out); out += "}";
            break;
        case NodeKind::Paren:
            emitBracket(n.openCh, true, out);
            emitBlock(*n.blocks[0], out);
            emitBracket(n.closeCh, false, out);
            break;
        }
    }
}

std::string blockToLatex(const Block& b) {
    std::string out;
    emitBlock(b, out);
    // Trim trailing spaces left by commands at the end.
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// ---- parsing -----------------------------------------------------------------------------------

namespace {

struct LatexParser {
    std::u32string s;
    size_t i = 0;

    bool eof() const { return i >= s.size(); }
    void skipSpace() { while (!eof() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) ++i; }

    // Reads "\name" (letters) or "\c" (single non-letter). Assumes s[i] == '\\'.
    std::string readCommand() {
        ++i;
        std::string name;
        if (!eof() && !std::isalpha(int(s[i] < 128 ? s[i] : 0))) {
            name += char(s[i++]);
            return name;
        }
        while (!eof() && s[i] < 128 && std::isalpha(int(s[i]))) name += char(s[i++]);
        return name;
    }

    // Parses either a {group} or a single token into `out`.
    void parseArgument(Block& out) {
        skipSpace();
        if (eof()) return;
        if (s[i] == '{') {
            ++i;
            parseSequence(out, U'}');
            if (!eof() && s[i] == '}') ++i;
            return;
        }
        Block tmp;
        parseOne(tmp, 0);
        while (!tmp.nodes.empty()) out.insert(int(out.nodes.size()), tmp.take(0));
    }

    std::string readBraceText() {
        skipSpace();
        std::string t;
        if (eof() || s[i] != '{') return t;
        ++i;
        while (!eof() && s[i] != '}') t += char(s[i++]);
        if (!eof()) ++i;
        return t;
    }

    char32_t readDelimiter() {
        skipSpace();
        if (eof()) return '(';
        if (s[i] == '\\') {
            std::string c = readCommand();
            if (c == "{" || c == "lbrace") return '{';
            if (c == "}" || c == "rbrace") return '}';
            if (c == "|" || c == "vert") return '|';
            if (c == "langle") return '(';
            if (c == "rangle") return ')';
            return '(';
        }
        return s[i++];
    }

    static char32_t closerOf(char32_t o) { return o == '(' ? ')' : o == '[' ? ']' : o == '{' ? '}' : o; }

    void appendLetters(Block& out, const std::string& name) {
        for (char c : name) out.insert(int(out.nodes.size()), Node::symbol(char32_t(c)));
    }

    // Parse one element; returns false if it hit the terminator.
    bool parseOne(Block& out, char32_t terminator) {
        skipSpace();
        if (eof()) return false;
        char32_t c = s[i];
        if (terminator && c == terminator) return false;
        if (c == '\\') {
            size_t save = i;
            std::string cmd = readCommand();
            if (cmd == "right") { i = save; return false; }
            if (cmd == "frac" || cmd == "dfrac" || cmd == "tfrac") {
                auto n = Node::structure(NodeKind::Frac);
                parseArgument(*n->blocks[0]);
                parseArgument(*n->blocks[1]);
                out.insert(int(out.nodes.size()), std::move(n));
            } else if (cmd == "sqrt") {
                skipSpace();
                if (!eof() && s[i] == '[') {
                    ++i;
                    auto n = Node::structure(NodeKind::NthRoot);
                    parseSequence(*n->blocks[0], U']');
                    if (!eof() && s[i] == ']') ++i;
                    parseArgument(*n->blocks[1]);
                    out.insert(int(out.nodes.size()), std::move(n));
                } else {
                    auto n = Node::structure(NodeKind::Sqrt);
                    parseArgument(*n->blocks[0]);
                    out.insert(int(out.nodes.size()), std::move(n));
                }
            } else if (cmd == "sum" || cmd == "prod" || cmd == "int") {
                auto n = Node::structure(NodeKind::BigOp);
                n->ch = cmd == "sum" ? sym::Sum : cmd == "prod" ? sym::Prod : sym::Integral;
                for (int k = 0; k < 2; ++k) {
                    skipSpace();
                    if (!eof() && (s[i] == '_' || s[i] == '^')) {
                        Block& target = *n->blocks[s[i] == '_' ? 0 : 1];
                        ++i;
                        parseArgument(target);
                    }
                }
                out.insert(int(out.nodes.size()), std::move(n));
            } else if (cmd == "left") {
                char32_t open = readDelimiter();
                if (open == '.') open = '(';
                auto n = Node::paren(open, closerOf(open));
                parseSequence(*n->blocks[0], 0);
                skipSpace();
                if (!eof() && s[i] == '\\') {
                    size_t save2 = i;
                    if (readCommand() == "right") n->closeCh = readDelimiter();
                    else i = save2;
                }
                if (n->closeCh == '.') n->closeCh = closerOf(open);
                out.insert(int(out.nodes.size()), std::move(n));
            } else if (cmd == "operatorname" || cmd == "mathrm" || cmd == "text") {
                appendLetters(out, readBraceText());
            } else if (cmd == "cdot") out.insert(int(out.nodes.size()), Node::symbol(sym::CDot));
            else if (cmd == "times") out.insert(int(out.nodes.size()), Node::symbol(sym::Times));
            else if (cmd == "le" || cmd == "leq") out.insert(int(out.nodes.size()), Node::symbol(sym::Le));
            else if (cmd == "ge" || cmd == "geq") out.insert(int(out.nodes.size()), Node::symbol(sym::Ge));
            else if (cmd == "infty") out.insert(int(out.nodes.size()), Node::symbol(sym::Infinity));
            else if (cmd == "sim") out.insert(int(out.nodes.size()), Node::symbol('~'));
            else if (cmd == "ldots" || cmd == "dots") appendLetters(out, "...");
            else if (cmd == "{" || cmd == "}" || cmd == "lbrace" || cmd == "rbrace") {
                // Bare \{ ... \} or \lbrace ... \rbrace (no \left): treat as a brace bracket.
                if (cmd == "{" || cmd == "lbrace") {
                    auto n = Node::paren('{', '}');
                    parseSequence(*n->blocks[0], 0, true);
                    out.insert(int(out.nodes.size()), std::move(n));
                } else {
                    i = save;
                    return false;
                }
            } else if (cmd == " " || cmd == "," || cmd == ";" || cmd == ":" || cmd == "!") {
                // spacing: ignore
            } else if (cmd == "%") out.insert(int(out.nodes.size()), Node::symbol('%'));
            else if (char32_t g = greekFromName(cmd)) out.insert(int(out.nodes.size()), Node::symbol(g));
            else if (isOperatorName(cmd)) appendLetters(out, cmd);
            else appendLetters(out, cmd);  // unknown command: keep its letters
            return true;
        }
        if (c == '{') {
            // A bare group. In Desmos braces only mean a piecewise or a restriction, so typed text
            // like "y={x>0:x,-x}" keeps them as a brace bracket; other groups ({x}^{2}) are transparent.
            ++i;
            auto n = Node::paren('{', '}');
            parseSequence(*n->blocks[0], U'}');
            if (!eof() && s[i] == '}') ++i;
            if (isCondition(*n->blocks[0])) {
                out.insert(int(out.nodes.size()), std::move(n));
            } else {
                for (auto& k : n->blocks[0]->nodes) out.insert(int(out.nodes.size()), std::move(k));
            }
            return true;
        }
        if (c == '}') return false;
        if (c == '^' || c == '_') {
            ++i;
            auto n = Node::structure(c == '^' ? NodeKind::Sup : NodeKind::Sub);
            parseArgument(*n->blocks[0]);
            out.insert(int(out.nodes.size()), std::move(n));
            return true;
        }
        if (c == '(' || c == '[') {
            ++i;
            auto n = Node::paren(c, closerOf(c));
            parseSequence(*n->blocks[0], closerOf(c));
            if (!eof() && s[i] == closerOf(c)) ++i;
            out.insert(int(out.nodes.size()), std::move(n));
            return true;
        }
        if (c == ')' || c == ']') { ++i; return true; }  // stray closer: drop
        if (c == '/') {
            // Plain-text division (pasted "x^3/10"): a fraction of the operands either side, as
            // typing '/' would make. Operands stop at + − = < > , : · (implicit products bind tighter).
            ++i;
            auto n = Node::structure(NodeKind::Frac);
            size_t start = out.nodes.size();
            while (start > 0 && !isOperandBreak(*out.nodes[start - 1])) --start;
            for (size_t k = start; k < out.nodes.size(); ++k)
                n->blocks[0]->insert(int(n->blocks[0]->nodes.size()), std::move(out.nodes[k]));
            out.nodes.erase(out.nodes.begin() + long(start), out.nodes.end());
            while (!atDenominatorEnd(terminator)) {
                if (!parseOne(*n->blocks[1], terminator)) break;
            }
            out.insert(int(out.nodes.size()), std::move(n));
            return true;
        }
        if (c == '|') {
            ++i;
            auto n = Node::paren('|', '|');
            parseSequence(*n->blocks[0], U'|');
            if (!eof() && s[i] == '|') ++i;
            out.insert(int(out.nodes.size()), std::move(n));
            return true;
        }
        ++i;
        if (c == '*') c = sym::CDot;
        out.insert(int(out.nodes.size()), Node::symbol(c));
        return true;
    }

    static bool isBreakChar(char32_t c) {
        return c == '+' || c == '-' || c == sym::Minus || c == '=' || c == '<' || c == '>' || c == ',' || c == ':' ||
               c == sym::Le || c == sym::Ge || c == sym::CDot || c == sym::Times || c == '/';
    }
    static bool isOperandBreak(const Node& n) { return n.kind == NodeKind::Symbol && isBreakChar(n.ch); }

    // True where a plain-text denominator ends: an operator, a closer, or the end of the group.
    bool atDenominatorEnd(char32_t terminator) {
        skipSpace();
        if (eof()) return true;
        char32_t c = s[i];
        if ((terminator && c == terminator) || c == '}' || c == ')' || c == ']' || c == '*') return true;
        if (isBreakChar(c)) return true;
        if (c == '\\') {
            size_t save = i;
            std::string cmd = readCommand();
            i = save;
            return cmd == "right" || cmd == "}" || cmd == "rbrace" || cmd == "cdot" || cmd == "times" ||
                   cmd == "le" || cmd == "leq" || cmd == "ge" || cmd == "geq";
        }
        return false;
    }

    // Top-level ':' or comparison: the contents of a Desmos piecewise / restriction.
    static bool isCondition(const Block& b) {
        for (auto& n : b.nodes) {
            if (n->kind != NodeKind::Symbol) continue;
            char32_t c = n->ch;
            if (c == ':' || c == '<' || c == '>' || c == '=' || c == sym::Le || c == sym::Ge) return true;
        }
        return false;
    }

    bool atRbrace() const {
        if (s.compare(i, 7, U"\\rbrace") != 0) return false;
        return i + 7 >= s.size() || s[i + 7] > 127 || !std::isalpha(int(s[i + 7]));
    }

    void parseSequence(Block& out, char32_t terminator, bool untilBraceClose = false) {
        while (!eof()) {
            if (untilBraceClose) {
                skipSpace();
                if (i + 1 < s.size() && s[i] == '\\' && s[i + 1] == '}') { i += 2; return; }
                if (atRbrace()) { i += 7; return; }
            }
            if (!parseOne(out, terminator)) return;
        }
    }
};

} // namespace

void latexToBlock(const std::string& latex, Block& out) {
    LatexParser p;
    p.s = fromUtf8(latex);
    while (!p.eof()) {
        p.parseSequence(out, 0);
        if (!p.eof()) ++p.i;  // skip an unmatched terminator and continue
    }
}

} // namespace calc
