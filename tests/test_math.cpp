#include "Test.hpp"

#include "math/Analyzer.hpp"
#include "math/Latex.hpp"
#include "math/MathTree.hpp"
#include "math/Vm.hpp"

using namespace calc;

namespace {

// Simulates typing `keys` into an empty editor.
MathTree typed(const char* keys) {
    MathTree t;
    t.typeText(fromUtf8(keys));
    return t;
}

std::string latexOf(const MathTree& t) { return blockToLatex(t.root()); }

MathTree fromLatex(const std::string& l) {
    MathTree t;
    t.insertLatex(l);
    return t;
}

std::vector<RowAnalysis> analyze(std::initializer_list<const char*> latexRows, bool degrees = false) {
    std::vector<MathTree> trees;
    for (auto* l : latexRows) trees.push_back(fromLatex(l));
    std::vector<RowInput> in;
    for (auto& t : trees) in.push_back({&t});
    return analyzeGraph(in, {degrees});
}

RowAnalysis one(const char* latex, bool degrees = false) { return analyze({latex}, degrees)[0]; }

double valueOf(const char* latex, bool degrees = false) {
    RowAnalysis r = one(latex, degrees);
    if (!r.showValue) {
        std::printf("    (no value for %s: %s)\n", latex, r.error.c_str());
        return -12345.678;
    }
    return r.value;
}

} // namespace

// ---- editor behaviour (MathQuill parity) -------------------------------------------------------

TEST(typing_fraction_takes_previous_term) {
    CHECK_EQ(latexOf(typed("1+2/3")), std::string("1+\\frac{2}{3}"));
    // Like Desmos, "/" typed while still in the exponent makes the fraction inside the exponent.
    CHECK_EQ(latexOf(typed("x^2/4")), std::string("x^{\\frac{2}{4}}"));
    MathTree t = typed("x^2");
    t.moveRight();
    t.typeText(U"/4");
    CHECK_EQ(latexOf(t), std::string("\\frac{x^{2}}{4}"));
    CHECK_EQ(latexOf(typed("/")), std::string("\\frac{}{}"));
}

TEST(typing_exponent_and_exit) {
    MathTree t = typed("x^2");
    t.moveRight();  // leave the exponent
    t.typeText(U"+1");
    CHECK_EQ(latexOf(t), std::string("x^{2}+1"));
}

TEST(typing_autocommands) {
    CHECK_EQ(latexOf(typed("2pi")), std::string("2\\pi"));
    CHECK_EQ(latexOf(typed("sqrt2")), std::string("\\sqrt{2}"));
    CHECK_EQ(latexOf(typed("theta")), std::string("\\theta"));
    CHECK_EQ(latexOf(typed("x<=3")), std::string("x\\le 3"));
    CHECK_EQ(latexOf(typed("2*3")), std::string("2\\cdot 3"));
    CHECK_EQ(latexOf(typed("sin(x)")), std::string("\\sin\\left(x\\right)"));
}

TEST(typing_brackets_ghost_and_close) {
    MathTree t = typed("(x+1");
    CHECK_EQ(latexOf(t), std::string("\\left(x+1\\right)"));
    t.typeText(U")2");
    CHECK_EQ(latexOf(t), std::string("\\left(x+1\\right)2"));
    MathTree a = typed("|x|");
    CHECK_EQ(latexOf(a), std::string("\\left|x\\right|"));
}

TEST(backspace_semantics) {
    MathTree t = typed("x^2");
    t.moveRight();
    t.backspace();  // enters the exponent
    t.backspace();  // deletes "2"
    t.backspace();  // exponent now empty: removes it
    CHECK_EQ(latexOf(t), std::string("x"));
    MathTree s = typed("sqrt");
    s.backspace();  // empty sqrt removed
    CHECK(s.empty());
}

TEST(latex_roundtrip) {
    const char* cases[] = {
        "y=x^{2}", "\\frac{1}{2}", "y=\\sin\\left(x\\right)", "\\sqrt[3]{x}", "a_{1}=2",
        "y<\\left|x\\right|", "f\\left(x\\right)=x^{2}+1", "\\left\\{x>0:x,-x\\right\\}",
        "\\left[1,2,3\\right]", "x^{2}+y^{2}\\le 9", "\\operatorname{mod}\\left(x,3\\right)",
    };
    for (auto* c : cases) CHECK_EQ(latexOf(fromLatex(c)), std::string(c));
}

TEST(latex_import_desmos_variants) {
    CHECK_EQ(latexOf(fromLatex("y=\\sin x")), std::string("y=\\sin x"));
    CHECK_EQ(latexOf(fromLatex("x^2")), std::string("x^{2}"));
    CHECK_EQ(latexOf(fromLatex("\\frac12")), std::string("\\frac{1}{2}"));
    CHECK_EQ(latexOf(fromLatex("x\\leq 2")), std::string("x\\le 2"));
}

// ---- evaluation --------------------------------------------------------------------------------

TEST(values_basic) {
    CHECK_NEAR(valueOf("2+3\\cdot4"), 14, 1e-12);
    CHECK_NEAR(valueOf("2^{3^{2}}"), 512, 1e-9);
    CHECK_NEAR(valueOf("-2^{2}"), -4, 1e-12);
    CHECK_NEAR(valueOf("\\frac{1}{3}"), 1.0 / 3, 1e-15);
    CHECK_NEAR(valueOf("2\\pi"), 2 * M_PI, 1e-12);
    CHECK_NEAR(valueOf("\\sin\\frac{\\pi}{2}"), 1, 1e-12);
    CHECK_NEAR(valueOf("\\sqrt[3]{-8}"), -2, 1e-12);
    CHECK_NEAR(valueOf("\\left(-8\\right)^{\\frac{1}{3}}"), -2, 1e-12);
    CHECK_NEAR(valueOf("5!"), 120, 0);
    CHECK_NEAR(valueOf("\\operatorname{mod}\\left(-1,3\\right)"), 2, 0);
    CHECK_NEAR(valueOf("\\left|-3\\right|"), 3, 0);
    CHECK_NEAR(valueOf("\\log_{2}8"), 3, 1e-12);
    CHECK_NEAR(valueOf("\\ln e"), 1, 1e-12);
    CHECK_NEAR(valueOf("\\max\\left(1,5,3\\right)"), 5, 0);
}

TEST(values_implicit_multiplication_and_functions) {
    CHECK_NEAR(valueOf("2\\sin\\frac{\\pi}{6}\\cos0"), 1, 1e-12);
    CHECK_NEAR(valueOf("\\sin^{2}\\frac{\\pi}{4}"), 0.5, 1e-12);
    CHECK_NEAR(valueOf("\\sin^{-1}1"), M_PI / 2, 1e-12);
    CHECK_NEAR(valueOf("3\\left(2+1\\right)"), 9, 0);
}

TEST(values_degrees) {
    CHECK_NEAR(valueOf("\\sin 90", true), 1, 1e-12);
    CHECK_NEAR(valueOf("\\arctan 1", true), 45, 1e-12);
}

TEST(values_lists) {
    CHECK_NEAR(valueOf("\\operatorname{total}\\left(\\left[1,2,3\\right]\\right)"), 6, 0);
    CHECK_NEAR(valueOf("\\operatorname{mean}\\left(\\left[1...5\\right]\\right)"), 3, 1e-12);
    CHECK_NEAR(valueOf("\\operatorname{length}\\left(\\left[1,3,...,9\\right]\\right)"), 5, 0);
}

TEST(piecewise) {
    CHECK_NEAR(valueOf("\\left\\{1>0:5,7\\right\\}"), 5, 0);
    CHECK_NEAR(valueOf("\\left\\{1<0:5,7\\right\\}"), 7, 0);
    CHECK(std::isnan(valueOf("\\left\\{1<0:5\\right\\}")));
}

// ---- classification ----------------------------------------------------------------------------

TEST(classify_graphs) {
    CHECK(one("y=x^{2}").graph == GraphKind::ExplicitY);
    CHECK(one("x^{2}").graph == GraphKind::ExplicitY);
    CHECK(one("x=y^{2}").graph == GraphKind::ExplicitX);
    CHECK(one("x^{2}+y^{2}=4").graph == GraphKind::Implicit);
    CHECK(one("r=\\theta").graph == GraphKind::Polar);
    CHECK(one("\\left(1,2\\right)").graph == GraphKind::Point);
    CHECK(one("\\left(\\cos t,\\sin t\\right)").graph == GraphKind::Parametric);
    RowAnalysis ineq = one("y>x");
    CHECK(ineq.graph == GraphKind::ExplicitY);
    CHECK(ineq.rel == Cmp::Gt && ineq.strict);
    RowAnalysis flipped = one("x^{2}\\le y");
    CHECK(flipped.graph == GraphKind::ExplicitY && flipped.rel == Cmp::Ge && !flipped.strict);
}

TEST(classify_definitions_and_sliders) {
    auto r = analyze({"y=ax+b", "a=2", "b=-1", "c=a+b"});
    CHECK(r[0].kind == RowKind::Graph);
    CHECK(r[1].kind == RowKind::Slider);
    CHECK_NEAR(r[2].sliderValue, -1, 0);
    CHECK(r[3].kind == RowKind::Definition && r[3].showValue);
    CHECK_NEAR(r[3].value, 1, 0);
    CHECK_NEAR(evaluate(r[0].pieces[0].f, 3), 5, 1e-12);
}

TEST(undefined_variables_suggest_sliders) {
    RowAnalysis r = one("y=mx+b");
    CHECK(r.kind == RowKind::Error);
    CHECK_EQ(r.sliderSuggestions.size(), size_t(2));
}

TEST(functions) {
    auto r = analyze({"f\\left(x\\right)=x^{2}", "f\\left(3\\right)", "y=f\\left(x-1\\right)",
                      "g\\left(a,b\\right)=a-b", "g\\left(5,2\\right)"});
    CHECK(r[0].kind == RowKind::FunctionDef && r[0].graph == GraphKind::ExplicitY);
    CHECK_NEAR(r[1].value, 9, 0);
    CHECK_NEAR(evaluate(r[2].pieces[0].f, 4), 9, 0);
    CHECK_NEAR(r[4].value, 3, 0);
}

TEST(errors) {
    CHECK(one("2+").kind == RowKind::Error);
    auto dup = analyze({"a=1", "a=2"});
    CHECK(dup[0].kind == RowKind::Error && dup[1].kind == RowKind::Error);
    auto cyc = analyze({"a=b", "b=a"});
    CHECK(cyc[0].kind == RowKind::Error);
}

TEST(broadcast_lists_make_multiple_curves) {
    RowAnalysis r = one("y=x+\\left[1,2,3\\right]");
    CHECK_EQ(r.pieces.size(), size_t(3));
    CHECK_NEAR(evaluate(r.pieces[2].f, 1), 4, 0);
    RowAnalysis pts = one("\\left(1,2\\right),\\left(3,4\\right)");
    CHECK_EQ(pts.pieces.size(), size_t(2));
}

TEST(hash_changes_only_with_dependencies) {
    auto a = analyze({"y=ax", "a=1", "y=x^{2}"});
    auto b = analyze({"y=ax", "a=2", "y=x^{2}"});
    CHECK(a[0].hash != b[0].hash);
    CHECK(a[2].hash == b[2].hash);
}

TEST(batch_matches_scalar) {
    RowAnalysis r = one("y=\\frac{\\sin x}{x}+\\left|x\\right|^{\\frac{1}{3}}");
    const Program& p = r.pieces[0].f;
    std::vector<double> xs, out(1000);
    for (int i = 0; i < 1000; ++i) xs.push_back(-10 + i * 0.02);
    evaluateBatch(p, 1000, xs.data(), nullptr, nullptr, out.data());
    for (int i = 0; i < 1000; i += 37) CHECK_NEAR(out[i], evaluate(p, xs[i]), 1e-12);
}

TEST(number_format) {
    CHECK_EQ(formatNumber(14), std::string("14"));
    CHECK_EQ(formatNumber(1.0 / 3), std::string("0.333333333333"));
    CHECK_EQ(formatNumber(-2.5), std::string("-2.5"));
    CHECK_EQ(formatNumber(1e15), std::string("1\\times10^{15}"));
    CHECK_EQ(formatNumber(std::sin(M_PI)), std::string("0"));
}

// ---- regression (MPS for large data) -------------------------------------------------------------

TEST(regression_linear_fit) {
    auto r = analyze({"x_{1}=\\left[1,2,3,4,5\\right]", "y_{1}=\\left[3.1,4.9,7.2,8.8,11.1\\right]",
                      "y_{1}\\sim mx_{1}+b", "m+b"});
    CHECK(r[2].regression && r[2].kind == RowKind::Graph);
    CHECK_EQ(r[2].regressionParams.size(), size_t(2));
    double b = r[2].regressionParams[0].second, m = r[2].regressionParams[1].second;  // sorted: b, m
    CHECK_NEAR(m, 1.99, 1e-9);
    CHECK_NEAR(b, 1.05, 1e-9);
    CHECK(r[2].rSquared > 0.99);
    CHECK_NEAR(evaluate(r[2].pieces[0].f, 10), 10 * 1.99 + 1.05, 1e-9);
    CHECK_NEAR(r[3].value, 3.04, 1e-9);  // fitted parameters are usable elsewhere
}

TEST(regression_quadratic_and_nonlinear_rejected) {
    auto q = analyze({"x_{1}=\\left[-2,-1,0,1,2\\right]", "y_{1}=\\left[9,3,1,3,9\\right]",
                      "y_{1}\\sim ax_{1}^{2}+bx_{1}+c"});
    CHECK(q[2].kind == RowKind::Graph);
    CHECK_NEAR(q[2].regressionParams[0].second, 2, 1e-9);   // a
    CHECK_NEAR(q[2].regressionParams[2].second, 1, 1e-9);   // c
}

TEST(regression_nonlinear_levenberg_marquardt) {
    // y = 2 e^{0.5 x} sampled exactly: LM must recover a = 2, b = 0.5.
    std::string xs = "x_{1}=\\left[0,1,2,3,4\\right]", ys = "y_{1}=\\left[";
    for (int i = 0; i <= 4; ++i) {
        char buf[48];
        std::snprintf(buf, sizeof buf, "%s%.15g", i ? "," : "", 2 * std::exp(0.5 * i));
        ys += buf;
    }
    ys += "\\right]";
    auto r = analyze({xs.c_str(), ys.c_str(), "y_{1}\\sim ae^{bx_{1}}"});
    CHECK(r[2].kind == RowKind::Graph);
    if (r[2].regressionParams.size() == 2) {
        CHECK_NEAR(r[2].regressionParams[0].second, 2, 1e-6);    // a
        CHECK_NEAR(r[2].regressionParams[1].second, 0.5, 1e-6);  // b
        CHECK(r[2].rSquared > 0.999999);
    }
    // Sinusoid with phase: y = 3 sin(x + 0.4)
    std::string sx = "x_{2}=\\left[0,0.5,1,1.5,2,2.5,3,3.5\\right]", sy = "y_{2}=\\left[";
    for (int i = 0; i < 8; ++i) {
        char buf[48];
        std::snprintf(buf, sizeof buf, "%s%.15g", i ? "," : "", 3 * std::sin(0.5 * i + 0.4));
        sy += buf;
    }
    sy += "\\right]";
    auto s2 = analyze({sx.c_str(), sy.c_str(), "y_{2}\\sim A\\sin\\left(x_{2}+c\\right)"});
    CHECK(s2[2].kind == RowKind::Graph && s2[2].rSquared > 0.999999);
}

// ---- derivatives -----------------------------------------------------------------------------------

TEST(derivative_operator) {
    auto d = [](const char* latex, double x) {
        RowAnalysis r = one(latex);
        if (r.pieces.empty()) { std::printf("    (%s: %s)\n", latex, r.error.c_str()); return -999.0; }
        return evaluate(r.pieces[0].f, x);
    };
    CHECK_NEAR(d("y=\\frac{d}{dx}x^{3}", 2), 12, 1e-12);
    CHECK_NEAR(d("y=\\frac{d}{dx}\\sin x", 0.3), std::cos(0.3), 1e-12);
    CHECK_NEAR(d("y=\\frac{d}{dx}\\left(x^{2}\\sin x\\right)", 1), 2 * std::sin(1.0) + std::cos(1.0), 1e-12);
    CHECK_NEAR(d("y=\\frac{d}{dx}\\frac{d}{dx}x^{4}", 2), 48, 1e-12);
    CHECK_NEAR(d("y=\\frac{d}{dx}x^{x}", 2), 4 * (std::log(2.0) + 1), 1e-12);
    CHECK_NEAR(d("y=\\frac{d}{dx}\\ln\\left(x^{2}+1\\right)", 1), 1, 1e-12);
    CHECK_NEAR(d("y=\\frac{d}{dx}x^{\\frac{1}{3}}", -8), 1.0 / 12, 1e-12);
    CHECK_NEAR(d("y=\\frac{d}{dx}\\arctan\\left(2x\\right)", 0), 2, 1e-12);
    CHECK_NEAR(d("y=\\frac{d}{dx}\\left|x\\right|", -3), -1, 0);
}

TEST(derivative_prime_notation) {
    auto r = analyze({"f\\left(x\\right)=x^{3}-2x", "y=f'\\left(x\\right)", "f''\\left(2\\right)",
                      "g\\left(t\\right)=\\cos t", "g'\\left(0\\right)"});
    CHECK_NEAR(evaluate(r[1].pieces[0].f, 2), 10, 1e-12);
    CHECK_NEAR(r[2].value, 12, 1e-12);
    CHECK_NEAR(r[4].value, 0, 1e-12);
    CHECK_EQ(latexOf(typed("f'(x)")), std::string("f'\\left(x\\right)"));
}

TEST(derivative_with_slider_variable) {
    auto r = analyze({"a=3", "y=\\frac{d}{dx}\\left(ax^{2}\\right)", "\\frac{d}{da}a^{2}"});
    CHECK_NEAR(evaluate(r[1].pieces[0].f, 1), 6, 1e-12);
    CHECK_NEAR(r[2].value, 6, 1e-12);
}

// ---- sums, products, integrals ----------------------------------------------------------------------

TEST(sum_product_integral_values) {
    CHECK_NEAR(valueOf("\\sum_{n=1}^{10}n^{2}"), 385, 1e-9);
    CHECK_NEAR(valueOf("\\sum_{n=1}^{3}n+1"), 7, 1e-12);          // body is one term
    CHECK_NEAR(valueOf("\\prod_{k=1}^{5}k"), 120, 1e-9);
    CHECK_NEAR(valueOf("\\sum_{n=5}^{1}n"), 0, 0);                 // empty sum
    CHECK_NEAR(valueOf("\\sum_{i=1}^{3}\\sum_{j=1}^{i}j"), 10, 1e-12);  // nested
    CHECK_NEAR(valueOf("\\int_{0}^{\\pi}\\sin tdt"), 2, 1e-9);
    CHECK_NEAR(valueOf("\\int_{0}^{1}x^{2}dx+1"), 1.0 / 3 + 1, 1e-12);
    CHECK_NEAR(valueOf("\\int_{0}^{2}\\int_{0}^{1}xydxdy"), 1, 1e-9);
}

TEST(sum_integral_graphs_and_derivatives) {
    RowAnalysis s = one("y=\\sum_{n=1}^{5}\\frac{\\sin\\left(nx\\right)}{n}");
    CHECK(s.graph == GraphKind::ExplicitY);
    double ref = 0;
    for (int n = 1; n <= 5; ++n) ref += std::sin(n * 0.7) / n;
    CHECK_NEAR(evaluate(s.pieces[0].f, 0.7), ref, 1e-12);
    RowAnalysis area = one("y=\\int_{0}^{x}t^{2}dt");
    CHECK_NEAR(evaluate(area.pieces[0].f, 3), 9, 1e-9);
    RowAnalysis ftc = one("y=\\frac{d}{dx}\\int_{0}^{x}\\cos tdt");  // Leibniz rule
    CHECK_NEAR(evaluate(ftc.pieces[0].f, 0.4), std::cos(0.4), 1e-12);
    RowAnalysis vb = one("y=\\sum_{n=1}^{x}n");                  // variable bound
    CHECK_NEAR(evaluate(vb.pieces[0].f, 4), 10, 0);
    CHECK_EQ(latexOf(typed("sum")), std::string("\\sum_{}^{}"));
}

#include "graph/Grid.hpp"
#include "graph/ImplicitCpu.hpp"

TEST(axis_labels_formatting) {
    CHECK_EQ(formatAxisLabel(1000000.01, 0.01).mantissa, std::string("1000000.01"));
    AxisLabel big = formatAxisLabel(3e7, 1e7);
    CHECK(big.scientific && big.mantissa == "3" && big.exponent == 7);
    CHECK_EQ(formatAxisLabel(2.5, 0.5).mantissa, std::string("2.5"));
    CHECK_EQ(formatAxisLabel(-4, 2).mantissa, std::string("-4"));
}

TEST(implicit_double_path_resolves_deep_zoom) {
    // Circle of radius 0.01 at x = 1e6: float can't resolve the cells, the double path must.
    ImplicitGrid g{1e6, 0, 30000, 600, 600, 2.5, 242, 242};
    CHECK(implicitNeedsDouble(g));
    ImplicitGrid shallow{0, 0, 50, 600, 600, 2.5, 242, 242};
    CHECK(!implicitNeedsDouble(shallow));
    RowAnalysis r = one("\\left(x-1000000\\right)^{2}+y^{2}=0.0001");
    std::vector<float> f, h, v;
    implicitFieldCpu(r.pieces[0].f, g, f, h, v);
    int crossings = 0;
    for (float t : h) crossings += !std::isnan(t);
    // The circle spans 600 points across → each of ~240 rows it touches has two crossings.
    CHECK(crossings > 300 && crossings < 600);
    CHECK(f[121 * 242 + 121] < 0);   // centre is inside
    CHECK(f[0] > 0);                 // corner is outside
}

TEST(list_index_with_variable) {
    auto r = analyze({"L=\\left[10,20,30\\right]", "\\sum_{k=1}^{3}L\\left[k\\right]",
                      "\\sum_{k=1}^{4}L\\left[k\\right]", "y=\\sum_{k=1}^{3}L\\left[k\\right]x^{k}",
                      "L\\left[2\\right]"});
    CHECK_NEAR(r[1].value, 60, 0);
    CHECK(std::isnan(r[2].value));                       // L[4] is out of range: undefined
    CHECK_EQ(r[3].pieces.size(), size_t(1));             // one curve, not broadcast over L
    CHECK_NEAR(evaluate(r[3].pieces[0].f, 2), 10 * 2 + 20 * 4 + 30 * 8, 1e-9);
    CHECK_NEAR(r[4].value, 20, 0);                       // constant index still folds
    // The Fourier-drawing shape from a user's test file, scaled down.
    auto f = analyze({"N=3", "a=\\left[1,0.5,0.25\\right]", "b=\\left[0,1,0\\right]",
                      "f\\left(t\\right)=\\sum_{k=1}^{N}\\left(a\\left[k\\right]\\cos\\left(kt\\right)+b\\left[k\\right]\\sin\\left(kt\\right)\\right)",
                      "\\left(f\\left(t\\right),f\\left(t\\right)\\right)"});
    CHECK(f[4].graph == GraphKind::Parametric);
    double t = 0.7, want = std::cos(t) + 0.5 * std::cos(2 * t) + 0.25 * std::cos(3 * t) + std::sin(2 * t);
    CHECK_NEAR(evaluate(f[4].pieces[0].f, 0, 0, t), want, 1e-12);
}

TEST(batched_loops_match_scalar) {
    const char* cases[] = {
        "y=\\sum_{k=1}^{40}\\frac{\\sin\\left(kx\\right)}{k}",           // uniform bounds: batched
        "y=\\sum_{k=1}^{x}k",                                             // per-lane bounds: fallback
        "y=\\int_{0}^{x}\\cos\\left(t^{2}\\right)dt",                     // per-lane integral limits
        "y=\\prod_{k=1}^{4}\\left(x-k\\right)+\\int_{-1}^{1}e^{tx}dt",
    };
    std::vector<double> xs, out(700);
    for (int i = 0; i < 700; ++i) xs.push_back(-6 + i * 0.0173);
    for (const char* c : cases) {
        RowAnalysis r = one(c);
        if (r.pieces.empty()) { test::fail(__FILE__, __LINE__, std::string("no program: ") + c); continue; }
        const Program& p = r.pieces[0].f;
        evaluateBatchParallel(p, 700, xs.data(), nullptr, nullptr, out.data());
        for (int i = 0; i < 700; i += 7) {
            double want = evaluate(p, xs[i]);
            bool same = (std::isnan(want) && std::isnan(out[i])) || std::fabs(want - out[i]) <= 1e-12 * (1 + std::fabs(want));
            if (!same) {
                char buf[160];
                std::snprintf(buf, sizeof buf, "%s at x=%g: batch %.15g scalar %.15g", c, xs[i], out[i], want);
                test::fail(__FILE__, __LINE__, buf);
                break;
            }
        }
    }
}

// ---- 3D mode -------------------------------------------------------------------------------------

TEST(three_d_classification) {
    auto a3 = [](std::initializer_list<const char*> rows) {
        std::vector<MathTree> trees;
        for (auto* l : rows) { trees.emplace_back(); trees.back().insertLatex(l); }
        std::vector<RowInput> in;
        for (auto& t : trees) in.push_back({&t});
        AnalyzerSettings s;
        s.threeD = true;
        return analyzeGraph(in, s);
    };
    auto r = a3({"z=x^{2}-y^{2}", "\\sin x\\cos y", "\\left(1,2,3\\right)", "\\left(\\cos t,\\sin t,t\\right)",
                 "x^{2}+y^{2}+z^{2}=9", "y=x", "a=2", "z=a\\cdot x"});
    CHECK(r[0].graph == GraphKind::Surface);
    CHECK_NEAR(evaluate(r[0].pieces[0].f, 3, 2), 5, 0);
    CHECK(r[1].graph == GraphKind::Surface);
    CHECK(r[2].graph == GraphKind::Point3D && r[2].pieces[0].pz == 3);
    CHECK(r[3].graph == GraphKind::Parametric3D);
    CHECK_NEAR(evaluate(r[3].pieces[0].h, 0, 0, 1.5), 1.5, 0);
    CHECK(r[4].graph == GraphKind::Implicit3D);    // implicit surfaces (1.3.0)
    CHECK(r[5].graph == GraphKind::Implicit3D);    // y = x is a plane in 3D, as in Desmos 3D
    CHECK(r[6].kind == RowKind::Slider);
    CHECK_NEAR(evaluate(r[7].pieces[0].f, 2, 0), 4, 0);
    // In 2D mode, z stays an ordinary variable.
    auto two = analyze({"z=5", "y=zx"});
    CHECK(two[0].kind == RowKind::Slider);
}

// Pasted braces: Desmos LaTeX, typed-text braces and \lbrace all become a brace bracket; plain
// LaTeX groups stay transparent.
TEST(latex_paste_keeps_piecewise_braces) {
    const char* piece = "y=\\left\\{x>0:x,-x\\right\\}";
    CHECK_EQ(latexOf(fromLatex("y=\\left\\{x>0:x,-x\\right\\}")), std::string(piece));
    CHECK_EQ(latexOf(fromLatex("y=\\{x>0:x,-x\\}")), std::string(piece));
    CHECK_EQ(latexOf(fromLatex("y={x>0:x,-x}")), std::string(piece));
    CHECK_EQ(latexOf(fromLatex("y=\\lbrace x>0:x,-x\\rbrace")), std::string(piece));
    CHECK_EQ(latexOf(fromLatex("y=x{x<3}")), std::string("y=x\\left\\{x<3\\right\\}"));
    CHECK_EQ(latexOf(fromLatex("{x}^{2}+{a}")), std::string("x^{2}+a"));
    auto res = analyze({"y={x>0:x,-x}"});
    CHECK(res[0].kind == RowKind::Graph);
}

// Plain-text '/' (pasted "x^3/10") becomes a fraction of the neighbouring operands, as typing '/'.
TEST(latex_paste_slash_makes_fraction) {
    CHECK_EQ(latexOf(fromLatex("y=x^3/10")), std::string("y=\\frac{x^{3}}{10}"));
    CHECK_EQ(latexOf(fromLatex("y=1/2+3")), std::string("y=\\frac{1}{2}+3"));
    CHECK_EQ(latexOf(fromLatex("y=a/bc")), std::string("y=\\frac{a}{bc}"));
    CHECK_EQ(latexOf(fromLatex("y=|x/2|")), std::string("y=\\left|\\frac{x}{2}\\right|"));
    CHECK_EQ(latexOf(fromLatex("y=-x/2")), std::string("y=-\\frac{x}{2}"));
    auto res = analyze({"y=x^3/10", "a=6/3*2"});
    CHECK(res[0].kind == RowKind::Graph);
    CHECK_NEAR(res[1].value, 4.0, 1e-12);
}
