// Expression rows: main math, "= value" (decimal/fraction), regression results, sliders (play button,
// track, inline bound editor "min ≤ a ≤ max  Step"), parametric/polar domain fields, and
// "add slider" suggestions.
#include "ui/ExpressionList.hpp"
#include "math/Latex.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace calc {

using namespace theme;

namespace {

constexpr float kSliderSidePad = 34;                   // room for min/max labels left/right of the track

bool rowHasCurveIcon(const ExpressionRow& r) {
    return r.analysis.kind == RowKind::Graph ||
           (r.analysis.kind == RowKind::FunctionDef && r.analysis.graph != GraphKind::None);
}

bool rowShowsValue(const ExpressionRow& r) {
    return r.analysis.showValue && r.analysis.kind != RowKind::Slider;
}

bool hasDomain(const ExpressionRow& r) {
    return r.analysis.graph == GraphKind::Parametric || r.analysis.graph == GraphKind::Polar ||
           r.analysis.graph == GraphKind::Parametric3D;
}

std::string formatSliderValue(double v, double range) {
    int decimals = std::clamp(int(std::ceil(-std::log10(range / 400.0))), 0, 10);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

std::string evalLatex(const ExpressionRow& r, bool& isFraction) {
    std::string frac;
    isFraction = fractionLatex(r.analysis.value, frac);
    if (isFraction && r.evalAsFraction) return "=" + frac;
    return "=" + formatNumber(r.analysis.value);
}

bool editingBounds(const Focus& f, int row) {
    return f.row == row && (f.field == Field::SliderMin || f.field == Field::SliderMax || f.field == Field::SliderStep);
}

} // namespace

// ---- layout ------------------------------------------------------------------------------------

float ExpressionList::layoutExpression(int i, std::vector<FieldBox>& boxes) {
    ExpressionRow& r = rows[i];
    if (r.layoutDirty) {
        r.layout.layout(r.tree.root(), *atlas, MathSize);
        r.layoutDirty = false;
    }
    float asc = std::max(r.layout.ascent(), 0.74f * MathSize);
    float desc = std::max(r.layout.descent(), 0.24f * MathSize);
    float baseline = std::max(IconCenterY + (asc - desc) * 0.5f, 12 + asc);
    float x0 = contentX(r);
    r.mathBaseline = baseline;
    boxes.push_back({Field::Main, 0, 0, Rect{x0, baseline - asc, contentRight() - x0, asc + desc}, baseline, MathSize});

    float under = baseline + desc;  // content bottom so far (row-relative)
    float bottom = std::max(RowMinHeight, under + 16);
    if (rowShowsValue(r)) bottom = std::max(bottom, under + EvalRowHeight + 6);
    if (r.analysis.regression && r.analysis.kind != RowKind::Error) bottom += 2 * RegressionLineHeight;

    auto inlineField = [&](Field f, MathField& mf, float x, float base, bool underline, float em) {
        mf.relayout(*atlas, em);
        float w = std::max(InlineFieldMinWidth, mf.layout.width() + 6);
        float a = std::max(mf.layout.ascent(), 0.74f * em), d = std::max(mf.layout.descent(), 0.24f * em);
        boxes.push_back({f, 0, 0, Rect{x, base - a - 2, w, a + d + 4}, base, em, underline});
        return w;
    };

    if (r.analysis.kind == RowKind::Slider) {
        float base = under + SliderRowHeight * 0.45f + SliderLabelSize * 0.35f;
        if (editingBounds(focus, i)) {
            // "min ≤ a ≤ max   Step: [ ]"
            float x = x0;
            x += inlineField(Field::SliderMin, r.sliderMinField, x, base, true, InlineFieldSize) + 6;
            std::string mid = "\\le " + r.analysis.definedName + "\\le ";
            x += measureLatex(*atlas, mid, InlineFieldSize) + 6;
            x += inlineField(Field::SliderMax, r.sliderMaxField, x, base, true, InlineFieldSize) + 18;
            x += atlas->measure(Font::Ui, std::string("Step:"), SliderLabelSize) + 6;
            inlineField(Field::SliderStep, r.sliderStepField, x, base, true, InlineFieldSize);
        } else {
            Rect tr = Rect{x0 + 2 * PlayButtonRadius + 8 + kSliderSidePad, 0, 0, 0};
            float trackRight = contentRight() - kSliderSidePad + 10;
            r.sliderMinField.relayout(*atlas, SliderLabelSize);
            float lw = r.sliderMinField.layout.width();
            inlineField(Field::SliderMin, r.sliderMinField, tr.x - 10 - lw - 2, base, false, SliderLabelSize);
            inlineField(Field::SliderMax, r.sliderMaxField, trackRight + 8, base, false, SliderLabelSize);
        }
        bottom = std::max(bottom, under + SliderRowHeight);
    }
    if (hasDomain(r)) {
        float base = std::max(bottom, under + 8) + DomainRowHeight * 0.62f;
        float x = x0;
        x += inlineField(Field::DomainMin, r.domainMin, x, base, true, InlineFieldSize) + 6;
        std::string var = r.analysis.graph == GraphKind::Polar ? "\\theta" : "t";
        x += measureLatex(*atlas, "\\le " + var + "\\le ", InlineFieldSize) + 6;
        inlineField(Field::DomainMax, r.domainMax, x, base, true, InlineFieldSize);
        bottom = std::max(bottom, under + 8) + DomainRowHeight;
    }
    if (r.style.showLabel && r.analysis.graph == GraphKind::Point) {
        float base = std::max(bottom, under + 8) + LabelRowHeight * 0.62f;
        float lx = x0 + atlas->measure(Font::Ui, std::string("label"), SliderLabelSize) + 10;
        boxes.push_back({Field::Label, 0, 0, Rect{lx, base - NoteTextSize - 2, contentRight() - lx, NoteTextSize * 1.3f + 4},
                         base, NoteTextSize, true});
        bottom = std::max(bottom, under + 8) + LabelRowHeight;
    }
    if (r.analysis.kind == RowKind::Error && !r.analysis.sliderSuggestions.empty()) bottom += SuggestionHeight;
    return bottom;
}

Rect ExpressionList::sliderTrack(int i) const {
    const auto& r = rows[i];
    float top = rowTopOnScreen(i);
    float y = top + r.mathBaseline + std::max(r.layout.descent(), 0.24f * MathSize) + SliderRowHeight * 0.45f;
    float x0 = contentX(r) + 2 * PlayButtonRadius + 8 + kSliderSidePad;
    float x1 = contentRight() - kSliderSidePad + 10;
    return {x0, y - 10, x1 - x0, 20};
}

Rect ExpressionList::playButton(int i) const {
    Rect tr = sliderTrack(i);
    float cx = contentX(rows[i]) + PlayButtonRadius, cy = tr.y + tr.h * 0.5f;
    return {cx - PlayButtonRadius, cy - PlayButtonRadius, 2 * PlayButtonRadius, 2 * PlayButtonRadius};
}

std::vector<Rect> ExpressionList::suggestionRects(int i) const {
    std::vector<Rect> out;
    const auto& r = rows[i];
    if (!atlas) return out;
    float top = rowTopOnScreen(i);
    float y = top + r.h - SuggestionHeight - 8;
    float x = contentX(r) + atlas->measure(Font::Ui, std::string("add slider:"), SliderLabelSize) + 8;
    Batch2D measure(*atlas);
    for (auto& name : r.analysis.sliderSuggestions) {
        float w = widgets::pillButton(measure, x, y, name, widgets::Interaction::Normal, true);
        out.push_back({x, y, w, SuggestionHeight - 4});
        x += w + 6;
    }
    if (r.analysis.sliderSuggestions.size() > 1) {
        float w = widgets::pillButton(measure, x, y, "all", widgets::Interaction::Normal, true);
        out.push_back({x, y, w, SuggestionHeight - 4});
    }
    return out;
}

// ---- drawing -----------------------------------------------------------------------------------

void ExpressionList::drawExpression(Batch2D& b, int i, float top, bool sel, bool hover, bool cursorVisible) const {
    const ExpressionRow& r = rows[i];
    // Colour icon / warning in the tab.
    float icx = area.x + TabWidth * 0.5f, icy = top + IconCenterY;
    if (r.analysis.kind == RowKind::Error) {
        widgets::drawIcon(b, widgets::Icon::Warning, icx, icy, ErrorIconSize, sel ? White : WarningIcon);
    } else if (rowHasCurveIcon(r)) {
        float rad = 10;
        Color ic = onPaper(r.color);
        if (r.hidden) b.circle(icx, icy, rad, Paper, 1.5f, ic.withAlpha(0.6f));
        else b.circle(icx, icy, rad, ic, sel ? 1.5f : 0, White);
        // Line-style hint inside the icon: two dashes / three dots (dark on a light icon).
        Color hint = r.hidden ? ic : (ic.r + ic.g + ic.b > 2.2f ? Black : White);
        if (r.style.line == LineStyle::Dashed) {
            b.line(icx - 6, icy, icx - 1.5f, icy, 2, hint);
            b.line(icx + 1.5f, icy, icx + 6, icy, 2, hint);
        } else if (r.style.line == LineStyle::Dotted) {
            for (float dx : {-4.5f, 0.0f, 4.5f}) b.circle(icx + dx, icy, 1.3f, hint);
        }
        if (hover && m_hover.kind == ListHit::Icon) b.circle(icx, icy, rad + 3, Color{0, 0, 0, 0}, 1, Border);
    }

    for (const FieldBox& fb : m_boxes[i]) {
        if (fb.field == Field::Label) {
            bool focused = focus.row == i && focus.field == Field::Label;
            float uy = top + fb.rect.bottom();
            if (focused) b.rect({fb.rect.x, uy - 2, fb.rect.w, 2}, Accent);
            else b.line(fb.rect.x, uy - 0.5f, fb.rect.right(), uy - 0.5f, 1, Border);
            b.text(Font::Ui, SliderLabelSize, contentX(r), top + fb.baseline, std::string("label"), TextSecondary);
            drawTextLine(b, r.label, fb.rect.x + 2, top + fb.baseline, focused, cursorVisible, "coordinates");
            continue;
        }
        Color c = Text;
        if (r.analysis.kind == RowKind::Slider && !editingBounds(focus, i) &&
            (fb.field == Field::SliderMin || fb.field == Field::SliderMax))
            c = TextSecondary;
        drawFieldBox(b, i, fb, top, cursorVisible, c);
    }

    const float x0 = contentX(r);
    float under = top + r.mathBaseline + std::max(r.layout.descent(), 0.24f * MathSize);
    if (rowShowsValue(r)) {
        bool isFraction;
        std::string latex = evalLatex(r, isFraction);
        float w = measureLatex(*atlas, latex, EvalSize);
        float x = contentRight() + 20 - w - 8;
        float base = under + EvalRowHeight * 0.75f;
        bool hot = isFraction && hover && m_hover.kind == ListHit::EvalToggle;
        b.rect({x - 6, base - EvalSize * 1.05f, w + 12, EvalSize * 1.5f}, hot ? HoverOverlay : EvalBg, 4);
        drawLatex(b, latex, Font::MathRoman, EvalSize, x, base, Text);
    }

    if (r.analysis.regression && r.analysis.kind != RowKind::Error) {
        float y = top + r.h - 2 * RegressionLineHeight - 4;
        b.text(Font::Ui, RegressionLabelSize, x0, y + RegressionLabelSize, std::string("PARAMETERS"), TextSecondary);
        float px = x0 + atlas->measure(Font::Ui, std::string("PARAMETERS"), RegressionLabelSize) + 12;
        for (auto& [name, value] : r.analysis.regressionParams) {
            std::string latex = name + "=" + formatNumber(value);
            px += drawLatex(b, latex, Font::MathRoman, EvalSize, px, y + RegressionLabelSize + 1, Text) + 16;
        }
        y += RegressionLineHeight;
        b.text(Font::Ui, RegressionLabelSize, x0, y + RegressionLabelSize, std::string("STATISTICS"), TextSecondary);
        float sx = x0 + atlas->measure(Font::Ui, std::string("STATISTICS"), RegressionLabelSize) + 12;
        drawLatex(b, "R^{2}=" + formatNumber(r.analysis.rSquared), Font::MathRoman, EvalSize, sx,
                  y + RegressionLabelSize + 1, Text);
    }

    if (r.analysis.kind == RowKind::Slider) {
        if (editingBounds(focus, i)) {
            const FieldBox* minB = boxFor(i, {i, Field::SliderMin});
            const FieldBox* stepB = boxFor(i, {i, Field::SliderStep});
            if (minB) {
                std::string mid = "\\le " + r.analysis.definedName + "\\le ";
                drawLatex(b, mid, Font::MathRoman, InlineFieldSize, minB->rect.right() + 6, top + minB->baseline, Text);
            }
            if (stepB)
                b.text(Font::Ui, SliderLabelSize, stepB->rect.x - 6 - atlas->measure(Font::Ui, std::string("Step:"), SliderLabelSize),
                       top + stepB->baseline, std::string("Step:"), TextSecondary);
        } else {
            Rect tr = sliderTrack(i);
            float cy = tr.y + tr.h * 0.5f;
            b.rect({tr.x, cy - SliderTrackHeight * 0.5f, tr.w, SliderTrackHeight}, SliderTrack, SliderTrackHeight * 0.5f);
            double t = (r.analysis.sliderValue - r.sliderMin) / (r.sliderMax - r.sliderMin);
            float tx = tr.x + float(std::clamp(t, 0.0, 1.0)) * tr.w;
            bool hot = (hover && m_hover.kind == ListHit::Slider) || m_sliderDrag == i;
            b.circle(tx, cy, SliderThumbRadius + (hot ? 1.5f : 0), onPaper(r.color));
            // Play / pause
            Rect pb = playButton(i);
            bool phot = hover && m_hover.kind == ListHit::Play;
            float pcx = pb.x + pb.w * 0.5f, pcy = pb.y + pb.h * 0.5f;
            b.circle(pcx, pcy, PlayButtonRadius, phot ? HoverOverlay : Paper, 1.2f, Border);
            if (r.playing) {
                b.line(pcx - 2.5f, pcy - 4, pcx - 2.5f, pcy + 4, 2, IconColor);
                b.line(pcx + 2.5f, pcy - 4, pcx + 2.5f, pcy + 4, 2, IconColor);
            } else {
                widgets::drawIcon(b, widgets::Icon::Play, pcx + 1, pcy, 11, IconColor);
            }
        }
    }

    if (hasDomain(r)) {
        const FieldBox* minB = boxFor(i, {i, Field::DomainMin});
        if (minB) {
            std::string var = r.analysis.graph == GraphKind::Polar ? "\\theta" : "t";
            drawLatex(b, "\\le " + var + "\\le ", Font::MathRoman, InlineFieldSize, minB->rect.right() + 6,
                      top + minB->baseline, Text);
        }
    }

    if (r.analysis.kind == RowKind::Error && !r.analysis.sliderSuggestions.empty()) {
        auto rects = suggestionRects(i);
        float y = rects.empty() ? top + r.h - 20 : rects[0].y;
        b.text(Font::Ui, SliderLabelSize, x0, y + (SuggestionHeight - 4) * 0.5f + SliderLabelSize * 0.35f,
               std::string("add slider:"), TextSecondary);
        size_t n = r.analysis.sliderSuggestions.size();
        for (size_t k = 0; k < rects.size(); ++k) {
            bool hot = hover && (m_hover.kind == ListHit::Suggestion || m_hover.kind == ListHit::SuggestionAll) &&
                       m_hover.index == int(k);
            std::string label = k < n ? r.analysis.sliderSuggestions[k] : "all";
            widgets::pillButton(b, rects[k].x, rects[k].y, label,
                                hot ? widgets::Interaction::Hovered : widgets::Interaction::Normal);
        }
    } else if (r.analysis.kind == RowKind::Error && sel) {
        b.text(Font::Ui, SliderLabelSize, x0, top + r.h - 8, r.analysis.error, Error);
    }
}

// ---- hit testing -------------------------------------------------------------------------------

ListHit ExpressionList::hitExpression(int i, float x, float y) const {
    const ExpressionRow& r = rows[i];
    ListHit h;
    h.row = i;
    float top = rowTopOnScreen(i);
    if (x < area.x + TabWidth) {
        float dx = x - (area.x + TabWidth * 0.5f), dy = y - (top + IconCenterY);
        h.kind = (dx * dx + dy * dy <= 16 * 16 && rowHasCurveIcon(r)) ? ListHit::Icon : ListHit::Tab;
        return h;
    }
    if (r.analysis.kind == RowKind::Slider && !editingBounds(focus, i)) {
        Rect pb = playButton(i);
        if (pb.inset(-3).contains(x, y)) { h.kind = ListHit::Play; return h; }
        Rect tr = sliderTrack(i);
        Rect grab{tr.x - SliderThumbRadius, tr.y, tr.w + 2 * SliderThumbRadius, tr.h};
        if (grab.contains(x, y)) { h.kind = ListHit::Slider; return h; }
    }
    if (r.analysis.kind == RowKind::Error) {
        auto rects = suggestionRects(i);
        for (size_t k = 0; k < rects.size(); ++k) {
            if (rects[k].contains(x, y)) {
                h.kind = k < r.analysis.sliderSuggestions.size() ? ListHit::Suggestion : ListHit::SuggestionAll;
                h.index = int(k);
                return h;
            }
        }
    }
    if (rowShowsValue(r)) {
        bool isFraction;
        std::string latex = evalLatex(r, isFraction);
        if (isFraction) {
            float w = measureLatex(*atlas, latex, EvalSize);
            float ex = contentRight() + 20 - w - 8;
            float base = top + r.mathBaseline + std::max(r.layout.descent(), 0.24f * MathSize) + EvalRowHeight * 0.75f;
            if (Rect{ex - 6, base - EvalSize * 1.05f, w + 12, EvalSize * 1.5f}.contains(x, y)) {
                h.kind = ListHit::EvalToggle;
                return h;
            }
        }
    }
    // Sub-fields first (they're small), then the main math.
    for (const FieldBox& fb : m_boxes[i]) {
        if (fb.field == Field::Main) continue;
        Rect rr{fb.rect.x - 3, top + fb.rect.y - 3, fb.rect.w + 6, fb.rect.h + 6};
        if (rr.contains(x, y)) {
            h.kind = ListHit::Field;
            h.field = fb.field;
            return h;
        }
    }
    h.kind = ListHit::Field;
    h.field = Field::Main;
    return h;
}

void ExpressionList::setSliderFromX(int i, float x) {
    ExpressionRow& r = rows[i];
    Rect tr = sliderTrack(i);
    double t = std::clamp(double(x - tr.x) / tr.w, 0.0, 1.0);
    double v = r.sliderMin + t * (r.sliderMax - r.sliderMin);
    if (r.sliderStep > 0) v = r.sliderMin + std::round((v - r.sliderMin) / r.sliderStep) * r.sliderStep;
    std::string latex = r.analysis.definedName + "=" + formatSliderValue(v, r.sliderMax - r.sliderMin);
    if (latex == blockToLatex(r.tree.root())) return;
    r.tree.clear();
    r.tree.insertLatex(latex);
    r.layoutDirty = true;
    r.analysis.sliderValue = v;  // immediate thumb feedback; analysis refreshes after
    if (onContentChanged) onContentChanged();
}

// ---- keyboard in slider-bound / domain fields --------------------------------------------------

bool ExpressionList::keyExpressionField(Key k, Mods m, const std::u32string& text) {
    int row = focus.row;
    MathTree* t = focusedTree();
    if (!t) return false;
    auto edit = [&](auto&& fn) {
        if (onContentWillChange) onContentWillChange();
        if (fn()) edited(row);
        else if (MathField* mf = fieldFor(focus)) mf->dirty = true;
    };
    auto cycle = [&](int dir) {
        static const Field slider[] = {Field::SliderMin, Field::SliderMax, Field::SliderStep};
        static const Field domain[] = {Field::DomainMin, Field::DomainMax};
        static const Field image[] = {Field::ImageCenter, Field::ImageWidth, Field::ImageHeight};
        const Field* list = domain;
        int n = 2;
        for (const Field* g : {slider, image})
            for (int j = 0; j < 3; ++j) if (g[j] == focus.field) { list = g; n = 3; }
        int idx = 0;
        for (int j = 0; j < n; ++j) if (list[j] == focus.field) idx = j;
        focusField({row, list[(idx + dir + n) % n]});
    };
    switch (k) {
    case Key::Char: edit([&] { return t->typeText(text); }); return true;
    case Key::Backspace: edit([&] { return t->backspace(); }); return true;
    case Key::Delete: edit([&] { return t->deleteForward(); }); return true;
    case Key::Left: t->moveLeft(m.shift); break;
    case Key::Right: t->moveRight(m.shift); break;
    case Key::Home: t->moveHome(m.shift); break;
    case Key::End: t->moveEnd(m.shift); break;
    case Key::Tab: cycle(m.shift ? -1 : 1); return true;
    case Key::Up:
        if (!t->moveUp()) {
            if (rows[row].type == RowType::Expression) focusField({row, Field::Main});
            else if (row > 0) focusRow(row - 1);
        }
        break;
    case Key::Down:
        if (!t->moveDown()) {
            int i = row + 1;
            while (i < int(rows.size()) && !rows[i].visibleInList) ++i;
            if (i < int(rows.size())) focusRow(i);
            else if (rows[row].type == RowType::Expression) focusField({row, Field::Main});
        }
        return true;
    case Key::Enter: case Key::Escape:
        if (rows[row].type == RowType::Expression) focusField({row, Field::Main});
        else blur();
        return true;
    }
    if (MathField* mf = fieldFor(focus)) mf->dirty = true;
    return true;
}

} // namespace calc
