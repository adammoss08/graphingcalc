#pragma once
// Editable structured math (the MathQuill model): a Block is a sequence of Nodes; structural nodes
// (fractions, exponents, roots, brackets) own child Blocks. The Cursor sits between nodes of a block.
#include <memory>
#include <string>
#include <vector>

namespace calc {

struct Block;

enum class NodeKind : uint8_t {
    Symbol,   // one code point: digit, letter, operator, greek, punctuation
    Frac,     // blocks: [numerator, denominator]
    Sup,      // blocks: [exponent]
    Sub,      // blocks: [subscript]
    Sqrt,     // blocks: [radicand]
    NthRoot,  // blocks: [index, radicand]
    Paren,    // blocks: [inner]; bracket chars in openCh/closeCh: ( [ { |
    BigOp,    // blocks: [lower, upper]; ch = ∑ ∏ ∫ (the body follows the node)
};

struct Node {
    NodeKind kind = NodeKind::Symbol;
    char32_t ch = 0;               // Symbol code point
    char32_t openCh = 0, closeCh = 0;
    bool ghostOpen = false;        // bracket side not typed yet (MathQuill "ghost" bracket)
    bool ghostClose = false;
    std::vector<std::unique_ptr<Block>> blocks;
    Block* parent = nullptr;

    static std::unique_ptr<Node> symbol(char32_t c);
    static std::unique_ptr<Node> structure(NodeKind k);
    static std::unique_ptr<Node> paren(char32_t open, char32_t close);
    std::unique_ptr<Node> clone() const;
    bool isEmpty() const;          // all child blocks empty
};

struct Block {
    std::vector<std::unique_ptr<Node>> nodes;
    Node* owner = nullptr;         // nullptr for the root block

    int indexOf(const Node* n) const;
    void insert(int at, std::unique_ptr<Node> n);
    std::unique_ptr<Node> take(int at);
    std::unique_ptr<Block> clone() const;
    int depth() const;
};

struct Cursor {
    Block* block = nullptr;
    int index = 0;                 // insertion point: 0..block->nodes.size()
    bool operator==(const Cursor& o) const { return block == o.block && index == o.index; }
};

// The editor: root block + cursor + selection, plus all MathQuill-style editing commands.
class MathTree {
public:
    MathTree();
    MathTree(const MathTree& o);
    MathTree& operator=(const MathTree& o);

    Block& root() { return *m_root; }
    const Block& root() const { return *m_root; }
    Cursor cursor() const { return m_cursor; }
    void setCursor(Cursor c) { m_cursor = c; m_anchor = {}; }
    bool empty() const { return m_root->nodes.empty(); }
    void clear();

    // Selection: anchor and cursor are normalised to the same block.
    bool hasSelection() const { return m_anchor.block != nullptr && !(m_anchor == m_cursor); }
    void selectionRange(Block*& blk, int& from, int& to) const;
    void clearSelection() { m_anchor = {}; }
    void selectAll();
    // Mouse selection: anchor and cursor may be in different blocks; both are lifted to their
    // common ancestor block so whole structures get selected (MathQuill behaviour).
    void select(Cursor anchor, Cursor cursor);
    Cursor anchorOrCursor() const { return m_anchor.block ? m_anchor : m_cursor; }

    // Typing. Returns true if the text changed.
    bool typeChar(char32_t c);           // handles auto-commands, '/', '^', '_', brackets, etc.
    bool typeText(const std::u32string& s);
    bool backspace();
    bool deleteForward();
    void insertFrac();
    void insertSup();
    void insertSub();
    void insertSqrt();
    void insertNthRoot();
    void insertBigOp(char32_t op);   // sym::Sum, sym::Prod, sym::Integral
    void insertParen(char32_t open);
    bool closeParen(char32_t close);

    // Navigation. moveUp/moveDown return false when there is nowhere to go inside this tree
    // (the expression list then moves to the neighbouring row).
    void moveLeft(bool select = false);
    void moveRight(bool select = false);
    bool moveUp();
    bool moveDown();
    void moveHome(bool select = false);
    void moveEnd(bool select = false);

    // Clipboard helpers (LaTeX).
    std::string selectionLatex() const;
    bool deleteSelection();
    void insertLatex(const std::string& latex);

    // Called when the editor loses focus: ghost brackets solidify.
    void solidifyBrackets();

    static bool isOperatorSymbol(char32_t c);

private:
    std::unique_ptr<Block> m_root;
    Cursor m_cursor;
    Cursor m_anchor;                     // selection anchor, block==nullptr if none

    void beginSelect(bool select);
    void normaliseSelection();
    void enterBlockStart(Block* b);
    void enterBlockEnd(Block* b);
    void exitRight(Block* b);            // cursor to just after b's owner
    void exitLeft(Block* b);
    void unwrapOwner(Block* b);          // remove owner node, splice all its blocks' contents in place
    std::vector<std::unique_ptr<Node>> takeSelection();
    bool tryAutoCommand();
    int termStart() const;               // start index of the "previous term" for '/'
};

// Code points used throughout.
namespace sym {
constexpr char32_t CDot = U'⋅';      // ⋅
constexpr char32_t Times = U'×';     // ×
constexpr char32_t Minus = U'−';     // − (display only; stored as '-')
constexpr char32_t Le = U'≤';
constexpr char32_t Ge = U'≥';
constexpr char32_t Pi = U'π';
constexpr char32_t Theta = U'θ';
constexpr char32_t Tau = U'τ';
constexpr char32_t Infinity = U'∞';
constexpr char32_t Ellipsis = U'…';
constexpr char32_t Sum = U'\u2211';       // ∑
constexpr char32_t Prod = U'\u220F';      // ∏
constexpr char32_t Integral = U'\u222B';  // ∫
}

// Greek letter names <-> code points (alpha..omega and capitals that differ from Latin).
char32_t greekFromName(const std::string& name);
const char* greekName(char32_t c);

// Known operator names rendered upright and parsed as functions (sin, cos, ln, ...).
// For each node of `b`: span[i] = length of an operator name starting at node i, -1 if node i is
// inside one, 0 otherwise. Letter runs are matched greedily left to right, longest name first.
void operatorSpans(const Block& b, std::vector<int>& span);
bool isOperatorName(const std::string& s);

std::string toUtf8(const std::u32string& s);
std::u32string fromUtf8(const std::string& s);

} // namespace calc
