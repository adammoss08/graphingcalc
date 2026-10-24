#include "ui/MathView.hpp"
#include "math/Latex.hpp"

#include <algorithm>
#include <cmath>

namespace calc {

namespace {

constexpr float kAsc = 0.74f;      // em: typical cap/letter height for line metrics
constexpr float kDesc = 0.24f;
constexpr float kAxis = 0.26f;     // em: maths axis (fraction bar, minus sign) above baseline
constexpr Color kEmptyBlock = Color::hex(0x000000, 0.1f);

bool isLetter(char32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isGreek(char32_t c) { return c >= 0x391 && c <= 0x3C9; }

bool isBinaryOp(char32_t c) {
    switch (c) {
    case '+': case '-': case '=': case '<': case '>': case sym::Le: case sym::Ge: case sym::CDot:
    case sym::Times: case sym::Minus: case '~': case ':':
        return true;
    default:
        return false;
    }
}

struct Frag {
    float w = 0, asc = 0, desc = 0;
};

} // namespace

// The layout is built in one recursive pass that records everything relative to the root origin.
// Each node is laid out into a temporary command range and translated once its position is known.
struct LayoutBuilder {
    TextAtlas& atlas;
    std::vector<MathLayout::Cmd>& cmds;
    std::unordered_map<const Block*, BlockBox>& blocks;
    const Block* rootBlock;

    void translate(size_t cmdFrom, const std::vector<const Block*>& blks, float dx, float dy) {
        for (size_t i = cmdFrom; i < cmds.size(); ++i) {
            cmds[i].x += dx; cmds[i].x1 += dx;
            cmds[i].y += dy; cmds[i].y1 += dy;
        }
        for (const Block* b : blks) {
            BlockBox& bb = blocks[b];
            bb.x += dx;
            bb.baseline += dy;
            for (float& c : bb.carets) c += dx;
            for (float& c : bb.nodeX) c += dx;
        }
    }

    void collectBlocks(const Block& b, std::vector<const Block*>& out) {
        out.push_back(&b);
        for (auto& n : b.nodes)
            for (auto& cb : n->blocks) collectBlocks(*cb, out);
    }

    void glyph(Font f, char32_t ch, float em, float x, bool ghost = false, float y = 0) {
        MathLayout::Cmd c{MathLayout::Cmd::Glyph, f, ghost, ch, em, x, y, 0, 0, 0};
        cmds.push_back(c);
    }
    void line(float x0, float y0, float x1, float y1, float thick, bool ghost = false) {
        MathLayout::Cmd c{MathLayout::Cmd::Line, Font::MathRoman, ghost, 0, 0, x0, y0, x1, y1, thick};
        cmds.push_back(c);
    }
    void box(float x, float y, float w, float h) {
        MathLayout::Cmd c{MathLayout::Cmd::Box, Font::MathRoman, false, 0, 0, x, y, x + w, y + h, 0};
        cmds.push_back(c);
    }

    // Lays out `b` with its origin at (0, 0); returns its extents.
    Frag block(const Block& b, float em) {
        BlockBox bb;
        bb.em = em;
        int n = int(b.nodes.size());
        std::vector<int> span;
        operatorSpans(b, span);
        float pen = 0;
        float asc = kAsc * em, desc = kDesc * em;
        for (int i = 0; i < n; ++i) {
            const Node& nd = *b.nodes[i];
            bb.carets.push_back(pen);
            bb.nodeX.push_back(pen);
            Frag f;
            size_t from = cmds.size();
            std::vector<const Block*> childBlocks;
            if (nd.kind == NodeKind::Symbol) {
                f = symbol(b, i, span, em);
            } else {
                for (auto& cb : nd.blocks) collectBlocks(*cb, childBlocks);
                f = structure(nd, b, i, em);
            }
            translate(from, childBlocks, pen, 0);
            pen += f.w;
            bb.nodeW.push_back(f.w);
            asc = std::max(asc, f.asc);
            desc = std::max(desc, f.desc);
        }
        bb.carets.push_back(pen);
        if (n == 0 && &b != rootBlock) {
            // MathQuill's grey placeholder for an empty block.
            float w = 0.55f * em;
            box(0.05f * em, -0.72f * em, w - 0.1f * em, 0.9f * em);
            pen = w;
            bb.carets.back() = w * 0.5f;
            bb.carets.front() = w * 0.5f;
        }
        bb.w = pen;
        bb.asc = asc;
        bb.desc = desc;
        blocks[&b] = bb;
        return {pen, asc, desc};
    }

    Frag symbol(const Block& b, int i, const std::vector<int>& span, float em) {
        const Node& nd = *b.nodes[i];
        char32_t c = nd.ch;
        Frag f{0, kAsc * em, kDesc * em};
        // Operator-name letters: upright, with a thin space around the name.
        if (span[i] != 0) {
            bool first = span[i] > 0;
            int start = i;
            while (start > 0 && span[start] < 0) --start;
            int len = span[start];
            bool last = i == start + len - 1;
            float x = 0;
            if (first && start > 0) {
                const Node* prev = b.nodes[start - 1].get();
                bool afterOp = prev->kind == NodeKind::Symbol && isBinaryOp(prev->ch);
                if (!afterOp) x += 0.2f * em;
            }
            glyph(Font::MathRoman, c, em, x);
            x += atlas.advance(Font::MathRoman, c, em);
            if (last && i + 1 < int(b.nodes.size())) {
                const Node* next = b.nodes[i + 1].get();
                bool paren = next->kind == NodeKind::Paren || next->kind == NodeKind::Sup ||
                             next->kind == NodeKind::Sub;
                if (!paren) x += 0.2f * em;
            }
            f.w = x;
            return f;
        }
        if (isBinaryOp(c)) {
            char32_t shown = c == '-' ? sym::Minus : c;
            bool unary = false;
            if (c == '-' || c == '+') {
                if (i == 0) unary = true;
                else {
                    const Node* prev = b.nodes[i - 1].get();
                    unary = prev->kind == NodeKind::Symbol && (isBinaryOp(prev->ch) || prev->ch == ',');
                }
            }
            float pad = unary ? 0 : theme::OpPad * em;
            if (c == ':') pad = 0.15f * em;
            glyph(Font::MathRoman, shown, em, pad);
            f.w = pad * 2 + atlas.advance(Font::MathRoman, shown, em);
            return f;
        }
        if (c == ',') {
            glyph(Font::MathRoman, c, em, 0);
            f.w = atlas.advance(Font::MathRoman, c, em) + 0.2f * em;
            return f;
        }
        Font font = Font::MathRoman;
        if (isLetter(c) || (isGreek(c) && c != sym::Pi)) font = Font::MathItalic;
        glyph(font, c, em, 0);
        f.w = atlas.advance(font, c, em);
        if (font == Font::MathItalic && c == 'f') f.w += 0.1f * em;  // MathQuill var.f margin
        return f;
    }

    Frag structure(const Node& nd, const Block& parent, int index, float em) {
        switch (nd.kind) {
        case NodeKind::Frac: return frac(nd, em);
        case NodeKind::Sup: return supsub(nd, em, true);
        case NodeKind::Sub: return supsub(nd, em, false);
        case NodeKind::Sqrt: return radical(nullptr, *nd.blocks[0], em);
        case NodeKind::NthRoot: return radical(nd.blocks[0].get(), *nd.blocks[1], em);
        case NodeKind::Paren: return paren(nd, em);
        case NodeKind::BigOp: return bigOp(nd, em);
        default: return {};
        }
    }

    Frag frac(const Node& nd, float em) {
        float cem = em * theme::FracScale;
        size_t numFrom = cmds.size();
        std::vector<const Block*> numBlocks, denBlocks;
        collectBlocks(*nd.blocks[0], numBlocks);
        Frag num = block(*nd.blocks[0], cem);
        size_t denFrom = cmds.size();
        collectBlocks(*nd.blocks[1], denBlocks);
        Frag den = block(*nd.blocks[1], cem);
        float pad = theme::FracPadX * em;
        float w = std::max(num.w, den.w) + 2 * pad;
        float bar = -kAxis * em;
        float gap = 0.14f * em;
        float numBase = bar - gap - num.desc;
        float denBase = bar + gap + den.asc;
        // Translate numerator (commands numFrom..denFrom) and denominator (denFrom..end).
        std::vector<MathLayout::Cmd> denCmds(cmds.begin() + denFrom, cmds.end());
        cmds.resize(denFrom);
        translate(numFrom, numBlocks, (w - num.w) * 0.5f, numBase);
        size_t dFrom = cmds.size();
        cmds.insert(cmds.end(), denCmds.begin(), denCmds.end());
        translate(dFrom, denBlocks, (w - den.w) * 0.5f, denBase);
        line(0.08f * em, bar, w - 0.08f * em, bar, std::max(theme::FracBarThickness, 0.05f * em));
        return {w, -(numBase - num.asc), denBase + den.desc};
    }

    Frag supsub(const Node& nd, float em, bool sup) {
        float cem = em * theme::SupScale;
        size_t from = cmds.size();
        std::vector<const Block*> blks;
        collectBlocks(*nd.blocks[0], blks);
        Frag c = block(*nd.blocks[0], cem);
        // Exponents rise ~0.42em; a tall exponent (a fraction) rises only enough for its bottom to
        // clear the base's x-height, as in MathQuill.
        float shift = sup ? -std::max(0.42f * em, c.desc + 0.02f * em) : std::max(0.22f * em, c.asc - 0.5f * em);
        float lead = 0.04f * em;
        translate(from, blks, lead, shift);
        Frag f;
        f.w = c.w + lead + 0.04f * em;
        f.asc = std::max(kAsc * em, -shift + c.asc);
        f.desc = std::max(kDesc * em, shift + c.desc);
        return f;
    }

    Frag radical(const Block* index, const Block& radicand, float em) {
        size_t from = cmds.size();
        std::vector<const Block*> blks;
        collectBlocks(radicand, blks);
        Frag c = block(radicand, em);
        float idxShift = 0;
        Frag idx{};
        size_t idxFrom = cmds.size();
        std::vector<const Block*> idxBlocks;
        if (index) {
            collectBlocks(*index, idxBlocks);
            idx = block(*index, em * 0.6f);
            idxShift = std::max(0.0f, idx.w - 0.3f * em);
        }
        float top = -(c.asc + 0.14f * em);
        float bottom = c.desc + 0.06f * em;
        float h = bottom - top;
        float rw = 0.72f * em;
        float x0 = idxShift;
        float contentX = x0 + rw + 0.08f * em;
        float thin = std::max(1.0f, 0.055f * em), thick = std::max(1.2f, 0.085f * em);
        // Content commands were emitted first; move them, then draw the radical.
        std::vector<MathLayout::Cmd> idxCmds(cmds.begin() + idxFrom, cmds.end());
        cmds.resize(idxFrom);
        translate(from, blks, contentX, 0);
        if (index) {
            size_t f2 = cmds.size();
            cmds.insert(cmds.end(), idxCmds.begin(), idxCmds.end());
            translate(f2, idxBlocks, 0, top + 0.45f * h - idx.desc);
        }
        float yTick = top + 0.6f * h;
        line(x0 + 0.02f * em, yTick + 0.04f * em, x0 + 0.17f * em, yTick - 0.02f * em, thin);
        line(x0 + 0.17f * em, yTick - 0.02f * em, x0 + 0.38f * em, bottom, thick);
        line(x0 + 0.38f * em, bottom, x0 + rw, top, thin);
        float right = contentX + c.w + 0.12f * em;
        line(x0 + rw, top, right, top, thin);
        Frag f;
        f.w = right + 0.04f * em;
        f.asc = -top + thin;
        if (index) f.asc = std::max(f.asc, -(top + 0.45f * h - idx.desc) + idx.asc);
        f.desc = bottom + thin;
        return f;
    }

    void bracketShape(char32_t ch, bool open, float x, float w, float top, float bottom, float em, bool ghost) {
        float thick = std::max(1.0f, 0.06f * em);
        float h = bottom - top;
        auto L = [&](float x0, float y0, float x1, float y1, float t) { line(x0, y0, x1, y1, t, ghost); };
        // Mirror for the closing side: draw as if opening, flipping x about the bracket box.
        auto X = [&](float fx) { return open ? x + fx : x + w - fx; };
        switch (ch) {
        case '(': case ')': {
            const int seg = 18;
            float bulge = std::min(w * 0.75f, 0.12f * em + 0.04f * h);
            float px = 0, py = 0;
            for (int k = 0; k <= seg; ++k) {
                float t = -1.0f + 2.0f * k / seg;
                float yy = top + (t + 1) * 0.5f * h;
                float xx = w * 0.85f - bulge * std::sqrt(std::max(0.0f, 1 - t * t));
                // taper: thicker in the middle like a real glyph
                float tk = thick * (0.75f + 0.5f * (1 - t * t));
                if (k) L(X(px), py, X(xx), yy, tk);
                px = xx; py = yy;
            }
            break;
        }
        case '[': case ']': {
            float xs = w * 0.3f, xe = w * 0.8f;
            L(X(xs), top, X(xs), bottom, thick);
            L(X(xs), top, X(xe), top, thick);
            L(X(xs), bottom, X(xe), bottom, thick);
            break;
        }
        case '{': case '}': {
            float mid = 0.5f * (top + bottom);
            float xr = w * 0.85f, xm = w * 0.5f, xl = w * 0.15f;
            float r = std::min(0.18f * em, h * 0.12f);
            auto curve = [&](float x0, float y0, float cx, float cy, float x1, float y1) {
                float lx = x0, ly = y0;
                for (int k = 1; k <= 6; ++k) {
                    float t = k / 6.0f, u = 1 - t;
                    float nx = u * u * x0 + 2 * u * t * cx + t * t * x1;
                    float ny = u * u * y0 + 2 * u * t * cy + t * t * y1;
                    L(X(lx), ly, X(nx), ny, thick);
                    lx = nx; ly = ny;
                }
            };
            curve(xr, top, xm, top, xm, top + r);
            L(X(xm), top + r, X(xm), mid - r, thick);
            curve(xm, mid - r, xm, mid, xl, mid);
            curve(xl, mid, xm, mid, xm, mid + r);
            L(X(xm), mid + r, X(xm), bottom - r, thick);
            curve(xm, bottom - r, xm, bottom, xr, bottom);
            break;
        }
        default:  // '|'
            L(X(w * 0.5f), top, X(w * 0.5f), bottom, thick);
            break;
        }
    }

    // ∑ / ∏: limits stacked above and below. ∫: tall sign with limits to its right (MathQuill).
    Frag bigOp(const Node& nd, float em) {
        bool integral = nd.ch == sym::Integral;
        float symSize = (integral ? 1.75f : 1.45f) * em;
        float limEm = em * 0.72f;
        const Glyph& g = atlas.glyph(Font::MathRoman, nd.ch, symSize);
        float sc = atlas.scale();
        float gh = g.h / sc, gtop = g.top / sc;          // glyph bitmap extent (points)
        float centerAbove = gtop - gh * 0.5f;              // visual centre above the pen baseline
        float yShift = centerAbove - kAxis * em;           // move so the centre sits on the axis
        float symTop = -(kAxis * em) - gh * 0.5f + TextAtlas::kPad / sc;
        float symBottom = -(kAxis * em) + gh * 0.5f - TextAtlas::kPad / sc;
        float symW = g.advance / sc;

        size_t lowFrom = cmds.size();
        std::vector<const Block*> lowBlocks, upBlocks;
        collectBlocks(*nd.blocks[0], lowBlocks);
        Frag lo = block(*nd.blocks[0], limEm);
        size_t upFrom = cmds.size();
        collectBlocks(*nd.blocks[1], upBlocks);
        Frag up = block(*nd.blocks[1], limEm);
        std::vector<MathLayout::Cmd> upCmds(cmds.begin() + upFrom, cmds.end());
        cmds.resize(upFrom);

        Frag f;
        if (integral) {
            float limX = symW * 0.85f;
            float upBase = symTop + up.asc * 0.6f;
            float loBase = symBottom + lo.asc * 0.1f;
            translate(lowFrom, lowBlocks, limX, loBase);
            size_t u2 = cmds.size();
            cmds.insert(cmds.end(), upCmds.begin(), upCmds.end());
            translate(u2, upBlocks, limX + 0.15f * em, upBase);
            glyph(Font::MathRoman, nd.ch, symSize, 0.02f * em, false, yShift);
            f.w = limX + std::max(lo.w, up.w + 0.15f * em) + 0.12f * em;
            f.asc = std::max(-symTop, -(upBase - up.asc));
            f.desc = std::max(symBottom, loBase + lo.desc);
        } else {
            float w = std::max({symW, lo.w, up.w}) + 0.16f * em;
            float upBase = symTop - 0.06f * em - up.desc;
            float loBase = symBottom + 0.06f * em + lo.asc;
            translate(lowFrom, lowBlocks, (w - lo.w) * 0.5f, loBase);
            size_t u2 = cmds.size();
            cmds.insert(cmds.end(), upCmds.begin(), upCmds.end());
            translate(u2, upBlocks, (w - up.w) * 0.5f, upBase);
            glyph(Font::MathRoman, nd.ch, symSize, (w - symW) * 0.5f, false, yShift);
            f.w = w + 0.08f * em;
            f.asc = -(upBase - up.asc);
            f.desc = loBase + lo.desc;
        }
        return f;
    }

    Frag paren(const Node& nd, float em) {
        size_t from = cmds.size();
        std::vector<const Block*> blks;
        collectBlocks(*nd.blocks[0], blks);
        Frag c = block(*nd.blocks[0], em);
        float top = -std::max(c.asc + 0.06f * em, 0.8f * em);
        float bottom = std::max(c.desc + 0.06f * em, 0.26f * em);
        float h = bottom - top;
        float bw = (nd.openCh == '|' ? 0.28f : nd.openCh == '{' ? 0.42f : 0.36f) * em +
                   std::min(0.15f * em, 0.02f * h);
        float innerPad = 0.06f * em;
        translate(from, blks, bw + innerPad, 0);
        bracketShape(nd.openCh, true, 0, bw, top, bottom, em, nd.ghostOpen);
        float rx = bw + innerPad + c.w + innerPad;
        bracketShape(nd.closeCh, false, rx, bw, top, bottom, em, nd.ghostClose);
        return {rx + bw, -top, bottom};
    }
};

void MathLayout::layout(const Block& root, TextAtlas& atlas, float emSize) {
    m_cmds.clear();
    m_blocks.clear();
    m_atlas = &atlas;
    m_rootBlock = &root;
    LayoutBuilder lb{atlas, m_cmds, m_blocks, &root};
    lb.block(root, emSize);
    m_root = m_blocks[&root];
}

void MathLayout::draw(Batch2D& b, float x, float baseline, Color color, const MathTree* tree,
                      bool focused, bool cursorVisible) const {
    // Selection highlight first, beneath the glyphs.
    if (tree && focused && tree->hasSelection()) {
        Block* blk; int from, to;
        tree->selectionRange(blk, from, to);
        auto it = m_blocks.find(blk);
        if (it != m_blocks.end()) {
            const BlockBox& bb = it->second;
            float x0 = bb.carets[from], x1 = bb.carets[to];
            b.rect({x + x0, baseline + bb.baseline - bb.asc, x1 - x0, bb.asc + bb.desc}, theme::SelectionColor);
        }
    }
    Color ghost = theme::GhostBracket;
    for (const Cmd& c : m_cmds) {
        Color col = c.ghost ? ghost : color;
        switch (c.kind) {
        case Cmd::Glyph: b.glyph(c.font, c.size, x + c.x, baseline + c.y, c.ch, col); break;
        case Cmd::Line: b.line(x + c.x, baseline + c.y, x + c.x1, baseline + c.y1, c.thick, col); break;
        case Cmd::Box: b.rect({x + c.x, baseline + c.y, c.x1 - c.x, c.y1 - c.y}, kEmptyBlock); break;
        }
    }
    if (tree && focused && cursorVisible && !tree->hasSelection()) {
        float cx, top, bottom;
        if (caretRect(tree->cursor(), cx, top, bottom))
            b.line(x + cx, baseline + top, x + cx, baseline + bottom, 1.0f, theme::CursorColor);
    }
}

bool MathLayout::caretRect(const Cursor& c, float& x, float& top, float& bottom) const {
    auto it = m_blocks.find(c.block);
    if (it == m_blocks.end()) return false;
    const BlockBox& bb = it->second;
    int idx = std::clamp(c.index, 0, int(bb.carets.size()) - 1);
    x = bb.carets[idx];
    top = bb.baseline - bb.asc;
    bottom = bb.baseline + bb.desc;
    return true;
}

Cursor MathLayout::hitTest(float px, float py) const {
    const Block* best = m_rootBlock;
    float bestArea = INFINITY;
    for (auto& [blk, bb] : m_blocks) {
        float x0 = bb.x - 2, x1 = bb.x + bb.w + 2;
        if (!bb.carets.empty()) {
            x0 = std::min(x0, bb.carets.front() - 2);
            x1 = std::max(x1, bb.carets.back() + 2);
        }
        float y0 = bb.baseline - bb.asc, y1 = bb.baseline + bb.desc;
        if (px < x0 || px > x1 || py < y0 || py > y1) continue;
        float area = (x1 - x0) * (y1 - y0);
        if (area < bestArea) { bestArea = area; best = blk; }
    }
    const BlockBox& bb = m_blocks.at(best);
    int bestIdx = 0;
    float bestD = INFINITY;
    for (size_t i = 0; i < bb.carets.size(); ++i) {
        float d = std::fabs(bb.carets[i] - px);
        if (d < bestD) { bestD = d; bestIdx = int(i); }
    }
    return {const_cast<Block*>(best), bestIdx};
}

float drawLatex(Batch2D& b, const std::string& latex, Font, float em, float x, float baseline, Color c) {
    MathTree t;
    t.insertLatex(latex);
    MathLayout l;
    l.layout(t.root(), b.atlas(), em);
    l.draw(b, x, baseline, c, nullptr, false, false);
    return l.width();
}

float measureLatex(TextAtlas& a, const std::string& latex, float em) {
    MathTree t;
    t.insertLatex(latex);
    MathLayout l;
    l.layout(t.root(), a, em);
    return l.width();
}

} // namespace calc
