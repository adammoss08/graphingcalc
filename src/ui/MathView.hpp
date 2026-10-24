#pragma once
// Typesetting for MathTree (MathQuill look): layout, drawing, cursor/selection, and hit testing.
// Coordinates are points relative to the layout origin (x = left edge, y = baseline, y down).
#include "math/MathTree.hpp"
#include "render/Batch2D.hpp"

#include <unordered_map>
#include <vector>

namespace calc {

struct BlockBox {
    float x = 0, baseline = 0, em = 0;
    float w = 0, asc = 0, desc = 0;
    std::vector<float> carets;         // x of each insertion point (size = nodes + 1)
    std::vector<float> nodeX, nodeW;   // per node
};

class MathLayout {
public:
    void layout(const Block& root, TextAtlas& atlas, float emSize);
    float width() const { return m_root.w; }
    float ascent() const { return m_root.asc; }
    float descent() const { return m_root.desc; }

    // Draws at (x, baseline). If `tree` is given and `focused`, draws its cursor/selection.
    void draw(Batch2D& b, float x, float baseline, Color color, const MathTree* tree, bool focused,
              bool cursorVisible) const;

    // Insertion point nearest to (px, py) (layout coordinates).
    Cursor hitTest(float px, float py) const;
    // Caret position (layout coordinates) for a cursor; returns false if unknown.
    bool caretRect(const Cursor& c, float& x, float& top, float& bottom) const;

    struct Cmd {
        enum Kind : uint8_t { Glyph, Line, Box } kind;
        Font font;
        bool ghost;
        char32_t ch;
        float size, x, y, x1, y1, thick;
    };

private:
    std::vector<Cmd> m_cmds;
    std::unordered_map<const Block*, BlockBox> m_blocks;
    const Block* m_rootBlock = nullptr;
    BlockBox m_root;
    TextAtlas* m_atlas = nullptr;

    BlockBox layoutBlock(const Block& b, float em, float x, float baseline, bool record);
    void place(const Block& b, float em, float x, float baseline);
};

// Convenience: lay out and draw LaTeX (evaluation values, labels).
float drawLatex(Batch2D& b, const std::string& latex, Font, float em, float x, float baseline, Color c);
float measureLatex(TextAtlas& a, const std::string& latex, float em);

} // namespace calc
