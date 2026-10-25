#pragma once
// Whole-graph analysis: parses every row, builds the definition table, classifies each row
// (graph / slider / value / definition / error), and compiles graphable rows to Programs.
#include "math/Ast.hpp"
#include "math/Vm.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace calc {

class MathTree;

enum class RowKind : uint8_t { Empty, Error, Value, Slider, Definition, FunctionDef, Graph, None };

enum class GraphKind : uint8_t {
    None,
    ExplicitY,   // y = f(x)            f: program in x
    ExplicitX,   // x = f(y)            f: program in y (OP_X slot holds y, see Sampler)
    Polar,       // r = f(θ)            f: program in t (= θ)
    Parametric,  // (f(t), g(t))
    Implicit,    // F(x, y) = 0         f: program in x, y
    Point,       // (a, b)              px, py
};

struct GraphPiece {
    Program f, g;          // see GraphKind
    Program region;        // inequalities: region where region(x, y) > 0 (or >= 0)
    double px = 0, py = 0; // points
    std::string dragX, dragY;  // points: slider variables a drag on the graph updates ("" = fixed)
};

struct RowAnalysis {
    RowKind kind = RowKind::Empty;
    std::string error;
    std::vector<std::string> sliderSuggestions;  // undefined variables that could be sliders

    // Value display ("= 5") for constant expressions and non-slider definitions.
    bool showValue = false;
    double value = 0;

    // Slider / definition
    std::string definedName;
    double sliderValue = 0;

    // Graph
    GraphKind graph = GraphKind::None;
    Cmp rel = Cmp::Eq;            // Eq for curves; otherwise an inequality (shaded)
    bool strict = false;          // dashed boundary
    bool shadeOnly = false;       // chained inequalities: boundary comes from the region contour
    double tMin = 0, tMax = 1;    // parametric / polar parameter domain
    std::vector<GraphPiece> pieces;
    uint64_t hash = 0;            // changes iff anything that affects drawing changes

    // Regression (y_1 ~ m x_1 + b)
    bool regression = false;
    std::vector<std::pair<std::string, double>> regressionParams;
    double rSquared = 0;
    bool regressionUsedMps = false;

    // RowInput::fields evaluated (a point gives two numbers; errors give an empty vector)
    std::vector<std::vector<double>> fieldValues;
};

struct RowInput {
    const MathTree* tree = nullptr;           // null: not an expression (contributes nothing)
    const MathTree* domainMin = nullptr;      // parametric/polar domain overrides (may be empty)
    const MathTree* domainMax = nullptr;
    std::vector<const MathTree*> fields;      // evaluated with the graph's definitions
};

struct AnalyzerSettings {
    bool degrees = false;
};

std::vector<RowAnalysis> analyzeGraph(const std::vector<RowInput>& rows, const AnalyzerSettings& s);

// Number formatting in the style of Desmos's evaluation display.
std::string formatNumber(double v);
// If v is (to double precision) a fraction p/q with 1 < q <= 10^6, writes "\frac{p}{q}" LaTeX.
bool fractionLatex(double v, std::string& out);

} // namespace calc
