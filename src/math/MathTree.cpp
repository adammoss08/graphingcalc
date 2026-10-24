#include "math/MathTree.hpp"
#include "math/Latex.hpp"

#include <algorithm>
#include <cstring>

namespace calc {

// ---- Node / Block ------------------------------------------------------------------------------

std::unique_ptr<Node> Node::symbol(char32_t c) {
    auto n = std::make_unique<Node>();
    n->kind = NodeKind::Symbol;
    n->ch = c;
    return n;
}

static int blockCount(NodeKind k) {
    switch (k) {
    case NodeKind::Symbol: return 0;
    case NodeKind::Frac: case NodeKind::NthRoot: case NodeKind::BigOp: return 2;
    default: return 1;
    }
}

std::unique_ptr<Node> Node::structure(NodeKind k) {
    auto n = std::make_unique<Node>();
    n->kind = k;
    for (int i = 0; i < blockCount(k); ++i) {
        auto b = std::make_unique<Block>();
        b->owner = n.get();
        n->blocks.push_back(std::move(b));
    }
    return n;
}

std::unique_ptr<Node> Node::paren(char32_t open, char32_t close) {
    auto n = structure(NodeKind::Paren);
    n->openCh = open;
    n->closeCh = close;
    return n;
}

std::unique_ptr<Node> Node::clone() const {
    auto n = std::make_unique<Node>();
    n->kind = kind; n->ch = ch; n->openCh = openCh; n->closeCh = closeCh;
    n->ghostOpen = ghostOpen; n->ghostClose = ghostClose;
    for (auto& b : blocks) {
        auto cb = b->clone();
        cb->owner = n.get();
        n->blocks.push_back(std::move(cb));
    }
    return n;
}

bool Node::isEmpty() const {
    for (auto& b : blocks)
        if (!b->nodes.empty()) return false;
    return true;
}

int Block::indexOf(const Node* n) const {
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].get() == n) return int(i);
    return -1;
}

void Block::insert(int at, std::unique_ptr<Node> n) {
    n->parent = this;
    nodes.insert(nodes.begin() + at, std::move(n));
}

std::unique_ptr<Node> Block::take(int at) {
    auto n = std::move(nodes[at]);
    nodes.erase(nodes.begin() + at);
    n->parent = nullptr;
    return n;
}

std::unique_ptr<Block> Block::clone() const {
    auto b = std::make_unique<Block>();
    for (auto& n : nodes) b->insert(int(b->nodes.size()), n->clone());
    return b;
}

int Block::depth() const {
    int d = 0;
    for (const Block* b = this; b->owner; b = b->owner->parent) ++d;
    return d;
}

// ---- tables ------------------------------------------------------------------------------------

struct Greek { const char* name; char32_t cp; };
static const Greek kGreek[] = {
    {"alpha", U'α'}, {"beta", U'β'}, {"gamma", U'γ'}, {"delta", U'δ'}, {"epsilon", U'ε'},
    {"zeta", U'ζ'}, {"eta", U'η'}, {"theta", U'θ'}, {"iota", U'ι'}, {"kappa", U'κ'},
    {"lambda", U'λ'}, {"mu", U'μ'}, {"nu", U'ν'}, {"xi", U'ξ'}, {"pi", U'π'}, {"rho", U'ρ'},
    {"sigma", U'σ'}, {"tau", U'τ'}, {"upsilon", U'υ'}, {"phi", U'φ'}, {"chi", U'χ'},
    {"psi", U'ψ'}, {"omega", U'ω'}, {"Gamma", U'Γ'}, {"Delta", U'Δ'}, {"Theta", U'Θ'},
    {"Lambda", U'Λ'}, {"Xi", U'Ξ'}, {"Pi", U'Π'}, {"Sigma", U'Σ'}, {"Phi", U'Φ'},
    {"Psi", U'Ψ'}, {"Omega", U'Ω'},
};

char32_t greekFromName(const std::string& name) {
    for (auto& g : kGreek) if (name == g.name) return g.cp;
    return 0;
}

const char* greekName(char32_t c) {
    for (auto& g : kGreek) if (g.cp == c) return g.name;
    return nullptr;
}

// Typed letter sequences that convert immediately (Desmos autoCommands). Short Greek names that
// collide with ordinary products (xi, nu, eta, mu) are left out.
static const char* kAutoCommands[] = {
    "sqrt", "cbrt", "nthroot", "pi", "tau", "theta", "phi", "alpha", "beta", "gamma", "delta",
    "epsilon", "zeta", "kappa", "lambda", "rho", "sigma", "omega", "psi", "chi", "infinity",
    "infty", "sum", "prod", "int",
};

static const char* kOperatorNames[] = {
    "arcsinh", "arccosh", "arctanh", "arcsech", "arccsch", "arccoth",
    "arcsin", "arccos", "arctan", "arcsec", "arccsc", "arccot",
    "sinh", "cosh", "tanh", "sech", "csch", "coth",
    "sin", "cos", "tan", "sec", "csc", "cot",
    "ln", "log", "exp", "min", "max", "floor", "ceil", "round", "abs", "sign", "mod", "gcd", "lcm",
    "mean", "median", "total", "length", "nCr", "nPr", "sort", "stdev", "var", "erf",
};

bool isOperatorName(const std::string& s) {
    for (auto* n : kOperatorNames) if (s == n) return true;
    return false;
}

static bool isLetter(char32_t c);

void operatorSpans(const Block& b, std::vector<int>& span) {
    int n = int(b.nodes.size());
    span.assign(n, 0);
    for (int i = 0; i < n;) {
        const Node* nd = b.nodes[i].get();
        if (nd->kind != NodeKind::Symbol || !isLetter(nd->ch)) { ++i; continue; }
        int best = 0;
        for (auto* name : kOperatorNames) {
            int len = int(std::strlen(name));
            if (len <= best || i + len > n) continue;
            bool ok = true;
            for (int k = 0; k < len && ok; ++k) {
                const Node* c = b.nodes[i + k].get();
                ok = c->kind == NodeKind::Symbol && c->ch == char32_t(name[k]);
            }
            if (ok) best = len;
        }
        if (best) {
            span[i] = best;
            for (int k = 1; k < best; ++k) span[i + k] = -1;
            i += best;
        } else {
            ++i;
        }
    }
}

std::string toUtf8(const std::u32string& s) {
    std::string out;
    for (char32_t c : s) {
        if (c < 0x80) out += char(c);
        else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) {
            out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        } else {
            out += char(0xF0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 0x3F));
            out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F));
        }
    }
    return out;
}

std::u32string fromUtf8(const std::string& s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        char32_t cp; int len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; len = 3; }
        else { cp = c & 0x07; len = 4; }
        for (int k = 1; k < len && i + k < s.size(); ++k) cp = (cp << 6) | (s[i + k] & 0x3F);
        out += cp;
        i += len;
    }
    return out;
}

static bool isLetter(char32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool MathTree::isOperatorSymbol(char32_t c) {
    switch (c) {
    case '+': case '-': case '=': case '<': case '>': case ',': case ':': case sym::CDot:
    case sym::Times: case sym::Le: case sym::Ge: case sym::Minus: case '~':
        return true;
    default:
        return false;
    }
}

// ---- MathTree ----------------------------------------------------------------------------------

MathTree::MathTree() : m_root(std::make_unique<Block>()) { m_cursor = {m_root.get(), 0}; }

MathTree::MathTree(const MathTree& o) : m_root(o.m_root->clone()) {
    m_cursor = {m_root.get(), int(m_root->nodes.size())};
}

MathTree& MathTree::operator=(const MathTree& o) {
    if (this != &o) {
        m_root = o.m_root->clone();
        m_cursor = {m_root.get(), int(m_root->nodes.size())};
        m_anchor = {};
    }
    return *this;
}

void MathTree::clear() {
    m_root = std::make_unique<Block>();
    m_cursor = {m_root.get(), 0};
    m_anchor = {};
}

void MathTree::selectAll() {
    m_anchor = {m_root.get(), 0};
    m_cursor = {m_root.get(), int(m_root->nodes.size())};
}

void MathTree::select(Cursor a, Cursor c) {
    if (!a.block || !c.block) return;
    if (a.block == c.block) {
        m_anchor = a;
        m_cursor = c;
        if (a == c) m_anchor = {};
        return;
    }
    // Find the common ancestor block.
    std::vector<Block*> chainA;
    for (Block* b = a.block; b; b = b->owner ? b->owner->parent : nullptr) chainA.push_back(b);
    Block* common = nullptr;
    for (Block* b = c.block; b && !common; b = b->owner ? b->owner->parent : nullptr)
        if (std::find(chainA.begin(), chainA.end(), b) != chainA.end()) common = b;
    if (!common) return;
    auto lift = [&](Cursor x, bool& lifted) {
        lifted = false;
        while (x.block != common) {
            Node* owner = x.block->owner;
            x = {owner->parent, owner->parent->indexOf(owner)};
            lifted = true;
        }
        return x;
    };
    bool la, lc;
    Cursor A = lift(a, la), C = lift(c, lc);
    if (A.index == C.index) {
        // Both inside the same structure: select that whole node, oriented like the drag.
        if (la && lc) { m_anchor = {common, A.index}; m_cursor = {common, A.index + 1}; return; }
    }
    if (A.index <= C.index) { if (lc) C.index++; }
    else if (la) A.index++;
    m_anchor = A;
    m_cursor = C;
}

void MathTree::selectionRange(Block*& blk, int& from, int& to) const {
    blk = m_cursor.block;
    from = std::min(m_anchor.index, m_cursor.index);
    to = std::max(m_anchor.index, m_cursor.index);
}

void MathTree::beginSelect(bool select) {
    if (select) {
        if (!m_anchor.block) m_anchor = m_cursor;
    } else {
        m_anchor = {};
    }
}

std::vector<std::unique_ptr<Node>> MathTree::takeSelection() {
    std::vector<std::unique_ptr<Node>> out;
    if (!hasSelection()) return out;
    Block* b; int from, to;
    selectionRange(b, from, to);
    for (int i = from; i < to; ++i) out.push_back(b->take(from));
    m_cursor = {b, from};
    m_anchor = {};
    return out;
}

bool MathTree::deleteSelection() {
    if (!hasSelection()) return false;
    takeSelection();
    return true;
}

void MathTree::enterBlockStart(Block* b) { m_cursor = {b, 0}; }
void MathTree::enterBlockEnd(Block* b) { m_cursor = {b, int(b->nodes.size())}; }

void MathTree::exitRight(Block* b) {
    Node* owner = b->owner;
    Block* pb = owner->parent;
    m_cursor = {pb, pb->indexOf(owner) + 1};
}

void MathTree::exitLeft(Block* b) {
    Node* owner = b->owner;
    Block* pb = owner->parent;
    m_cursor = {pb, pb->indexOf(owner)};
}

void MathTree::unwrapOwner(Block* b) {
    Node* owner = b->owner;
    Block* pb = owner->parent;
    int at = pb->indexOf(owner);
    // Cursor lands where this block's content begins once spliced.
    int cursorAt = at;
    auto ownerPtr = pb->take(at);
    int insertAt = at;
    for (auto& blk : ownerPtr->blocks) {
        if (blk.get() == b) cursorAt = insertAt;
        while (!blk->nodes.empty()) pb->insert(insertAt++, blk->take(0));
    }
    m_cursor = {pb, cursorAt};
}

int MathTree::termStart() const {
    Block* b = m_cursor.block;
    int i = m_cursor.index;
    while (i > 0) {
        Node* n = b->nodes[i - 1].get();
        if (n->kind == NodeKind::Symbol && isOperatorSymbol(n->ch)) break;
        --i;
    }
    return i;
}

bool MathTree::tryAutoCommand() {
    Block* b = m_cursor.block;
    int end = m_cursor.index;
    std::string letters;
    for (int i = end - 1; i >= 0 && end - i <= 8; --i) {
        Node* n = b->nodes[i].get();
        if (n->kind != NodeKind::Symbol || !isLetter(n->ch)) break;
        letters.insert(letters.begin(), char(n->ch));
    }
    const char* match = nullptr;
    size_t bestLen = 0;
    for (auto* cmd : kAutoCommands) {
        size_t len = std::strlen(cmd);
        if (len > letters.size() || len <= bestLen) continue;
        if (letters.compare(letters.size() - len, len, cmd) == 0) { match = cmd; bestLen = len; }
    }
    if (!match) return false;
    for (size_t k = 0; k < bestLen; ++k) b->take(end - int(bestLen));
    m_cursor.index = end - int(bestLen);
    std::string m = match;
    if (m == "sum") insertBigOp(sym::Sum);
    else if (m == "prod") insertBigOp(sym::Prod);
    else if (m == "int") insertBigOp(sym::Integral);
    else if (m == "sqrt") insertSqrt();
    else if (m == "nthroot") insertNthRoot();
    else if (m == "cbrt") {
        insertNthRoot();  // cursor now in index block
        typeChar('3');
        moveRight();      // into radicand
    } else if (m == "infinity" || m == "infty") {
        b->insert(m_cursor.index++, Node::symbol(sym::Infinity));
    } else {
        b->insert(m_cursor.index++, Node::symbol(greekFromName(m)));
    }
    return true;
}

bool MathTree::typeText(const std::u32string& s) {
    bool changed = false;
    for (char32_t c : s) changed |= typeChar(c);
    return changed;
}

bool MathTree::typeChar(char32_t c) {
    switch (c) {
    case '/': insertFrac(); return true;
    case '^': insertSup(); return true;
    case '_': insertSub(); return true;
    case '(': insertParen('('); return true;
    case '[': insertParen('['); return true;
    case '{': insertParen('{'); return true;
    case ')': case ']': case '}': return closeParen(c);
    case '|': {
        // Inside an abs whose close is still ghost: close it. Otherwise open a new one.
        Node* owner = m_cursor.block->owner;
        if (owner && owner->kind == NodeKind::Paren && owner->openCh == '|' && owner->ghostClose)
            return closeParen('|');
        insertParen('|');
        return true;
    }
    case ' ': return false;
    case '*': c = sym::CDot; break;
    default: break;
    }
    deleteSelection();
    Block* b = m_cursor.block;
    // "<=" / ">=" combine into ≤ / ≥.
    if (c == '=' && m_cursor.index > 0) {
        Node* prev = b->nodes[m_cursor.index - 1].get();
        if (prev->kind == NodeKind::Symbol && (prev->ch == '<' || prev->ch == '>')) {
            prev->ch = prev->ch == '<' ? sym::Le : sym::Ge;
            return true;
        }
    }
    b->insert(m_cursor.index++, Node::symbol(c));
    if (isLetter(c)) tryAutoCommand();
    return true;
}

void MathTree::insertFrac() {
    auto frac = Node::structure(NodeKind::Frac);
    Block* num = frac->blocks[0].get();
    Block* den = frac->blocks[1].get();
    Block* b = m_cursor.block;
    bool hadNumerator;
    int at;
    if (hasSelection()) {
        auto sel = takeSelection();
        at = m_cursor.index;
        for (auto& n : sel) num->insert(int(num->nodes.size()), std::move(n));
        hadNumerator = true;
    } else {
        int start = termStart();
        at = start;
        for (int i = start; i < m_cursor.index; ++i) num->insert(int(num->nodes.size()), b->take(start));
        hadNumerator = !num->nodes.empty();
    }
    b->insert(at, std::move(frac));
    m_cursor = hadNumerator ? Cursor{den, 0} : Cursor{num, 0};
}

void MathTree::insertSup() {
    Block* b = m_cursor.block;
    if (!hasSelection() && m_cursor.index < int(b->nodes.size()) &&
        b->nodes[m_cursor.index]->kind == NodeKind::Sup) {
        enterBlockStart(b->nodes[m_cursor.index]->blocks[0].get());
        return;
    }
    auto sel = takeSelection();
    auto sup = Node::structure(NodeKind::Sup);
    Block* inner = sup->blocks[0].get();
    for (auto& n : sel) inner->insert(int(inner->nodes.size()), std::move(n));
    b = m_cursor.block;
    b->insert(m_cursor.index, std::move(sup));
    enterBlockEnd(inner);
}

void MathTree::insertSub() {
    auto sel = takeSelection();
    auto sub = Node::structure(NodeKind::Sub);
    Block* inner = sub->blocks[0].get();
    for (auto& n : sel) inner->insert(int(inner->nodes.size()), std::move(n));
    m_cursor.block->insert(m_cursor.index, std::move(sub));
    enterBlockEnd(inner);
}

void MathTree::insertSqrt() {
    auto sel = takeSelection();
    auto sq = Node::structure(NodeKind::Sqrt);
    Block* inner = sq->blocks[0].get();
    for (auto& n : sel) inner->insert(int(inner->nodes.size()), std::move(n));
    m_cursor.block->insert(m_cursor.index, std::move(sq));
    enterBlockEnd(inner);
}

void MathTree::insertBigOp(char32_t op) {
    deleteSelection();
    auto n = Node::structure(NodeKind::BigOp);
    n->ch = op;
    Block* lower = n->blocks[0].get();
    m_cursor.block->insert(m_cursor.index, std::move(n));
    enterBlockStart(lower);
}

void MathTree::insertNthRoot() {
    auto sel = takeSelection();
    auto r = Node::structure(NodeKind::NthRoot);
    Block* rad = r->blocks[1].get();
    for (auto& n : sel) rad->insert(int(rad->nodes.size()), std::move(n));
    Block* idx = r->blocks[0].get();
    m_cursor.block->insert(m_cursor.index, std::move(r));
    enterBlockStart(idx);
}

static char32_t closerFor(char32_t open) {
    switch (open) {
    case '(': return ')';
    case '[': return ']';
    case '{': return '}';
    default: return open;
    }
}

void MathTree::insertParen(char32_t open) {
    auto p = Node::paren(open, closerFor(open));
    Block* inner = p->blocks[0].get();
    Block* b = m_cursor.block;
    if (hasSelection()) {
        auto sel = takeSelection();
        for (auto& n : sel) inner->insert(int(inner->nodes.size()), std::move(n));
        b->insert(m_cursor.index, std::move(p));
        m_cursor.index += 1;  // after the closed bracket
        return;
    }
    // MathQuill: an open bracket swallows everything to its right, closed by a ghost.
    p->ghostClose = true;
    int at = m_cursor.index;
    while (int(b->nodes.size()) > at) inner->insert(int(inner->nodes.size()), b->take(at));
    b->insert(at, std::move(p));
    enterBlockStart(inner);
}

bool MathTree::closeParen(char32_t close) {
    deleteSelection();
    // Walk outwards looking for an enclosing bracket with a ghost close of the right kind.
    for (Block* b = m_cursor.block; b->owner; b = b->owner->parent) {
        Node* owner = b->owner;
        if (owner->kind != NodeKind::Paren || owner->closeCh != close) continue;
        if (owner->ghostClose) {
            // Move everything right of the cursor (in this block) out after the bracket.
            Block* pb = owner->parent;
            int after = pb->indexOf(owner) + 1;
            if (b == m_cursor.block) {
                int moving = int(b->nodes.size()) - m_cursor.index;
                for (int k = 0; k < moving; ++k) pb->insert(after + k, b->take(m_cursor.index));
            }
            owner->ghostClose = false;
            m_cursor = {pb, after};
            return true;
        }
        // Typing the closer right at the end of a closed bracket steps over it.
        if (b == m_cursor.block && m_cursor.index == int(b->nodes.size())) {
            exitRight(b);
            return true;
        }
        break;
    }
    // No open bracket to close: MathQuill wraps everything to the left in a bracket with a ghost open.
    Block* b = m_cursor.block;
    char32_t open = close == ')' ? '(' : close == ']' ? '[' : close == '}' ? '{' : close;
    auto p = Node::paren(open, close);
    p->ghostOpen = true;
    Block* inner = p->blocks[0].get();
    for (int i = 0; i < m_cursor.index; ++i) inner->insert(int(inner->nodes.size()), b->take(0));
    b->insert(0, std::move(p));
    m_cursor = {b, 1};
    return true;
}

void MathTree::solidifyBrackets() {
    std::vector<Block*> stack{m_root.get()};
    while (!stack.empty()) {
        Block* b = stack.back();
        stack.pop_back();
        for (auto& n : b->nodes) {
            n->ghostOpen = n->ghostClose = false;
            for (auto& cb : n->blocks) stack.push_back(cb.get());
        }
    }
}

bool MathTree::backspace() {
    if (deleteSelection()) return true;
    Block* b = m_cursor.block;
    if (m_cursor.index > 0) {
        Node* prev = b->nodes[m_cursor.index - 1].get();
        if (prev->kind == NodeKind::Symbol) {
            b->take(--m_cursor.index);
            return true;
        }
        if (prev->isEmpty()) {
            b->take(--m_cursor.index);
            return true;
        }
        if (prev->kind == NodeKind::Paren) prev->ghostClose = true;
        enterBlockEnd(prev->blocks.back().get());
        return prev->kind == NodeKind::Paren;
    }
    if (!b->owner) return false;
    Node* owner = b->owner;
    if (owner->isEmpty()) {
        Block* pb = owner->parent;
        int at = pb->indexOf(owner);
        pb->take(at);
        m_cursor = {pb, at};
        return true;
    }
    // In the second block of a fraction/root, backspace at the start moves to the end of the first
    // block when the first block is empty; otherwise the structure is unwrapped.
    unwrapOwner(b);
    return true;
}

bool MathTree::deleteForward() {
    if (deleteSelection()) return true;
    Block* b = m_cursor.block;
    if (m_cursor.index < int(b->nodes.size())) {
        Node* next = b->nodes[m_cursor.index].get();
        if (next->kind == NodeKind::Symbol || next->isEmpty()) {
            b->take(m_cursor.index);
            return true;
        }
        enterBlockStart(next->blocks.front().get());
        return false;
    }
    if (!b->owner) return false;
    unwrapOwner(b);
    return true;
}

void MathTree::moveRight(bool select) {
    if (hasSelection() && !select) {
        Block* blk; int from, to;
        selectionRange(blk, from, to);
        m_cursor = {blk, to};
        m_anchor = {};
        return;
    }
    beginSelect(select);
    Block* b = m_cursor.block;
    if (select) {
        if (m_cursor.index < int(b->nodes.size())) { m_cursor.index++; return; }
        if (b->owner) {
            Block* pb = b->owner->parent;
            int at = pb->indexOf(b->owner);
            m_anchor = {pb, at};
            m_cursor = {pb, at + 1};
        }
        return;
    }
    if (m_cursor.index < int(b->nodes.size())) {
        Node* next = b->nodes[m_cursor.index].get();
        if (next->kind == NodeKind::Symbol) m_cursor.index++;
        else enterBlockStart(next->blocks.front().get());
        return;
    }
    if (!b->owner) return;
    Node* owner = b->owner;
    for (size_t i = 0; i + 1 < owner->blocks.size(); ++i)
        if (owner->blocks[i].get() == b) { enterBlockStart(owner->blocks[i + 1].get()); return; }
    exitRight(b);
}

void MathTree::moveLeft(bool select) {
    if (hasSelection() && !select) {
        Block* blk; int from, to;
        selectionRange(blk, from, to);
        m_cursor = {blk, from};
        m_anchor = {};
        return;
    }
    beginSelect(select);
    Block* b = m_cursor.block;
    if (select) {
        if (m_cursor.index > 0) { m_cursor.index--; return; }
        if (b->owner) {
            Block* pb = b->owner->parent;
            int at = pb->indexOf(b->owner);
            m_anchor = {pb, at + 1};
            m_cursor = {pb, at};
        }
        return;
    }
    if (m_cursor.index > 0) {
        Node* prev = b->nodes[m_cursor.index - 1].get();
        if (prev->kind == NodeKind::Symbol) m_cursor.index--;
        else enterBlockEnd(prev->blocks.back().get());
        return;
    }
    if (!b->owner) return;
    Node* owner = b->owner;
    for (size_t i = 1; i < owner->blocks.size(); ++i)
        if (owner->blocks[i].get() == b) { enterBlockEnd(owner->blocks[i - 1].get()); return; }
    exitLeft(b);
}

bool MathTree::moveUp() {
    m_anchor = {};
    Block* b = m_cursor.block;
    // Directly beside an exponent: enter it.
    if (m_cursor.index < int(b->nodes.size()) && b->nodes[m_cursor.index]->kind == NodeKind::Sup) {
        enterBlockStart(b->nodes[m_cursor.index]->blocks[0].get());
        return true;
    }
    if (m_cursor.index > 0 && b->nodes[m_cursor.index - 1]->kind == NodeKind::Sup) {
        enterBlockEnd(b->nodes[m_cursor.index - 1]->blocks[0].get());
        return true;
    }
    for (Block* blk = b; blk->owner; blk = blk->owner->parent) {
        Node* o = blk->owner;
        if (o->kind == NodeKind::Frac && o->blocks[1].get() == blk) {
            Block* num = o->blocks[0].get();
            int idx = blk == b ? std::min<int>(m_cursor.index, int(num->nodes.size()))
                               : int(num->nodes.size());
            m_cursor = {num, idx};
            return true;
        }
        if (o->kind == NodeKind::BigOp && o->blocks[0].get() == blk) {
            enterBlockEnd(o->blocks[1].get());
            return true;
        }
        if (o->kind == NodeKind::Sub) { exitRight(blk); return true; }
    }
    return false;
}

bool MathTree::moveDown() {
    m_anchor = {};
    Block* b = m_cursor.block;
    for (Block* blk = b; blk->owner; blk = blk->owner->parent) {
        Node* o = blk->owner;
        if (o->kind == NodeKind::Frac && o->blocks[0].get() == blk) {
            Block* den = o->blocks[1].get();
            int idx = blk == b ? std::min<int>(m_cursor.index, int(den->nodes.size()))
                               : int(den->nodes.size());
            m_cursor = {den, idx};
            return true;
        }
        if (o->kind == NodeKind::BigOp && o->blocks[1].get() == blk) {
            enterBlockEnd(o->blocks[0].get());
            return true;
        }
        if (o->kind == NodeKind::Sup) { exitRight(blk); return true; }
    }
    return false;
}

void MathTree::moveHome(bool select) {
    beginSelect(select);
    if (select) { m_anchor.block = m_root.get(); m_anchor.index = int(m_root->nodes.size()); }
    m_cursor = {m_root.get(), 0};
    if (select && m_anchor.index == 0) m_anchor = {};
}

void MathTree::moveEnd(bool select) {
    beginSelect(select);
    if (select) { m_anchor.block = m_root.get(); m_anchor.index = 0; }
    m_cursor = {m_root.get(), int(m_root->nodes.size())};
}

std::string MathTree::selectionLatex() const {
    if (!hasSelection()) return blockToLatex(*m_root);
    Block* b; int from, to;
    selectionRange(b, from, to);
    Block tmp;
    for (int i = from; i < to; ++i) tmp.nodes.push_back(b->nodes[i]->clone());
    return blockToLatex(tmp);
}

void MathTree::insertLatex(const std::string& latex) {
    deleteSelection();
    Block parsed;
    latexToBlock(latex, parsed);
    Block* b = m_cursor.block;
    while (!parsed.nodes.empty()) b->insert(m_cursor.index++, parsed.take(0));
}

} // namespace calc
