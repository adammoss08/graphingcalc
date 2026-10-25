#pragma once
// Data model for the expression list: row types (expression, note, folder, table, image), their
// editable fields, per-row style, and the focus target that keyboard/keypad input goes to.
#include "math/Analyzer.hpp"
#include "math/MathTree.hpp"
#include "render/Batch2D.hpp"
#include "ui/MathView.hpp"
#include "ui/Theme.hpp"

#include <string>
#include <vector>

namespace calc {

enum class RowType : uint8_t { Expression, Note, Folder, Table, Image };
enum class LineStyle : uint8_t { Solid, Dashed, Dotted };
enum class PointStyle : uint8_t { Point, Open, Cross };

// Everything the style menu edits (Desmos: color, lineStyle, lineWidth, lineOpacity, fillOpacity,
// pointStyle, pointSize, lines, points).
struct Style {
    Color color;
    bool hidden = false;
    LineStyle line = LineStyle::Solid;
    float lineWidth = theme::CurveWidth;
    float lineOpacity = theme::CurveOpacity;
    float fillOpacity = theme::FillOpacity;
    PointStyle point = PointStyle::Point;
    float pointSize = theme::PointDiameter;
    bool showLines = false;   // tables: connect points (Desmos default off)
    bool showPoints = true;
    bool showLabel = false;   // points: draw a label (coordinates, or `label` with ${var} values)
    std::string label;
};

// A small editable math box with its own cached layout (slider bounds, domains, table cells...).
struct MathField {
    MathTree tree;
    MathLayout layout;
    bool dirty = true;

    MathField() = default;
    explicit MathField(const std::string& latex) { set(latex); }
    void set(const std::string& latex) { tree.clear(); tree.insertLatex(latex); dirty = true; }
    std::string latex() const;
    bool empty() const { return tree.empty(); }
    void relayout(TextAtlas& a, float em) {
        if (dirty) { layout.layout(tree.root(), a, em); dirty = false; }
    }
};

// Plain-text editing (notes, folder titles).
struct TextField {
    std::u32string text;
    int cursor = 0;
    int anchor = -1;   // selection anchor; -1 = none

    bool hasSelection() const { return anchor >= 0 && anchor != cursor; }
    void insert(const std::u32string& s);
    bool backspace();
    bool deleteForward();
    void move(int delta, bool select);
    void home(bool select);
    void end(bool select);
    void selectAll() { anchor = 0; cursor = int(text.size()); }
    std::string utf8() const { return toUtf8(text); }
    void setUtf8(const std::string& s) { text = fromUtf8(s); cursor = int(text.size()); anchor = -1; }
};

struct TableColumn {
    uint32_t id = 0;
    MathField header;
    std::vector<MathField> cells;
    Style style;
    // derived each analysis
    bool computed = false;              // header is an expression of the first column
    std::vector<double> computedValues; // values shown in computed columns
    RowAnalysis analysis;               // points plotted against the first column
};

// Which editable box has keyboard focus.
enum class Field : uint8_t {
    Main, SliderMin, SliderMax, SliderStep, DomainMin, DomainMax, Text, TableHeader, TableCell,
    ImageCenter, ImageWidth, ImageHeight, Label,
};

struct ExpressionRow {
    uint32_t id = 0;
    RowType type = RowType::Expression;
    uint32_t folderId = 0;          // 0 = top level, else the id of the containing folder row

    // Expression (main field kept as tree/layout/layoutDirty for brevity)
    MathTree tree;
    MathLayout layout;
    bool layoutDirty = true;
    Color color;                    // == style.color (kept in sync)
    bool hidden = false;            // == style.hidden
    Style style;
    MathField sliderMinField{"-10"}, sliderMaxField{"10"}, sliderStepField;
    double sliderMin = -10, sliderMax = 10, sliderStep = 0;  // evaluated from the fields
    bool playing = false;
    int playDirection = 1;
    bool evalAsFraction = false;    // "= value" shown as a fraction (toggle on rational results)
    MathField domainMin, domainMax; // parametric / polar domain (empty = default)

    TextField label;                // point label text (shown when style.showLabel)

    // Note / folder
    TextField text;
    bool collapsed = false;

    // Table
    std::vector<TableColumn> columns;

    // Image
    std::string imagePath;
    int imageTexture = -1;          // Renderer image handle
    float imageAspect = 1;          // height / width of the bitmap
    MathField imageCenter{"\\left(0,0\\right)"}, imageWidth{"10"}, imageHeight{"10"};
    float imageOpacity = 1;

    RowAnalysis analysis;
    bool effectiveHidden = false;   // hidden itself or inside a hidden folder

    // layout (relative to the list content top)
    float y = 0, h = 0;
    float mathBaseline = 0;         // relative to row top
    float indent = 0;               // inside a folder
    bool visibleInList = true;      // false when inside a collapsed folder

    void syncStyle() { style.color = color; style.hidden = hidden; }
};

struct Focus {
    int row = -1;
    Field field = Field::Main;
    int col = 0, cell = 0;
    bool operator==(const Focus& o) const {
        return row == o.row && field == o.field && col == o.col && cell == o.cell;
    }
};

// Desmos JSON names for styles.
const char* lineStyleName(LineStyle s);
LineStyle lineStyleFromName(const std::string& s);
const char* pointStyleName(PointStyle s);
PointStyle pointStyleFromName(const std::string& s);

} // namespace calc
