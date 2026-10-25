#pragma once
// Design tokens. Every colour, size, font and timing used by the UI lives here and mirrors
// docs/DESMOS_SPEC.md. Do not put literals in drawing code; add a token instead.
#include <cstdint>

namespace calc {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    static constexpr Color hex(uint32_t rgb, float a = 1) {
        return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, a};
    }
    constexpr Color withAlpha(float na) const { return {r, g, b, na}; }
    constexpr Color mix(const Color& o, float t) const {
        return {r + (o.r - r) * t, g + (o.g - g) * t, b + (o.b - b) * t, a + (o.a - a) * t};
    }
};

enum class Font : uint8_t { Ui, UiBold, MathItalic, MathRoman, HeaderLight, Count };

namespace theme {

// ---- palette ----
inline constexpr Color White = Color::hex(0xFFFFFF);
inline constexpr Color Black = Color::hex(0x000000);
inline constexpr Color Accent = Color::hex(0x2F72DC);          // --dcg-accent-color
inline constexpr Color Text = Color::hex(0x000000);
inline constexpr Color TextSecondary = Color::hex(0x000000, 0.65f);
inline constexpr Color TextMuted = Color::hex(0x999999);
inline constexpr Color Border = Color::hex(0x000000, 0.2f);
inline constexpr Color RowBorder = Color::hex(0xCECECE, 0.8f);
inline constexpr Color TabBg = Color::hex(0xEEEEEE);
inline constexpr Color TabBorder = Color::hex(0x000000, 0.06f);
inline constexpr Color Error = Color::hex(0xE15855);
inline constexpr Color HoverOverlay = Color::hex(0x000000, 0.06f);
inline constexpr Color PressedOverlay = Color::hex(0x000000, 0.12f);
inline constexpr Color PoiGray = Color::hex(0x808080);
inline constexpr Color LabelHanging = Color::hex(0x999999);

// Curve colour cycle (Desmos skips orange in the cycle).
inline constexpr Color CurveRed = Color::hex(0xC74440);
inline constexpr Color CurveBlue = Color::hex(0x2D70B3);
inline constexpr Color CurveGreen = Color::hex(0x348543);
inline constexpr Color CurvePurple = Color::hex(0x6042A6);
inline constexpr Color CurveOrange = Color::hex(0xFA7E19);
inline constexpr Color CurveBlack = Color::hex(0x000000);
inline constexpr Color ColorCycle[] = {CurveRed, CurveBlue, CurveGreen, CurvePurple, CurveBlack};
inline constexpr Color ColorPalette[] = {CurveRed, CurveBlue, CurveGreen, CurvePurple, CurveOrange, CurveBlack};
inline constexpr int ColorCycleCount = 5;
inline constexpr int ColorPaletteCount = 6;

// ---- app header (dark bar) ----
inline constexpr float HeaderHeight = 46;
inline constexpr Color HeaderBg = Color::hex(0x2A2A2A);
inline constexpr Color HeaderText = Color::hex(0xDDDDDD);
inline constexpr Color HeaderBorder = Color::hex(0x000000);
inline constexpr float HeaderTitleSize = 18;
inline constexpr float HeaderTrafficLightInset = 78;   // room for the window buttons

// ---- expression panel ----
inline constexpr float PanelDefaultWidth = 400;
inline constexpr float PanelMinWidth = 300;
inline constexpr float TopBarHeight = 48;
inline constexpr Color TopBarTop = Color::hex(0xFCFCFC);
inline constexpr Color TopBarBottom = Color::hex(0xEAEAEA);
inline constexpr float IconButtonSize = 40;
inline constexpr float IconButtonRadius = 5;
inline constexpr float IconStroke = 1.6f;
inline constexpr Color IconColor = Color::hex(0x000000, 0.8f);

inline constexpr float RowMinHeight = 58;
inline constexpr float RowPadBottom = 6;
inline constexpr float TabWidth = 38;
inline constexpr float TabIndexSize = 10.4f;          // 65% of 16
inline constexpr float IconDiameter = 29;
inline constexpr float IconCenterY = 28;
inline constexpr float MathLeftPad = 10;
inline constexpr float MathRightPad = 34;             // room for the delete ×
inline constexpr float MathSize = 18.4f;              // 115% of 16
inline constexpr float NewRowHeight = 54;
inline constexpr float DeleteSize = 14;
inline constexpr float EvalSize = 15.2f;              // .95rem
inline constexpr float EvalRowHeight = 24;
inline constexpr float SliderRowHeight = 40;
inline constexpr float SliderTrackHeight = 3;
inline constexpr float SliderThumbRadius = 7;
inline constexpr float SliderLabelSize = 13.6f;       // 85%
inline constexpr float ErrorIconSize = 16;
inline constexpr float SuggestionHeight = 26;
inline constexpr float RegressionLineHeight = 24;
inline constexpr float DomainRowHeight = 30;
inline constexpr float LabelRowHeight = 30;
inline constexpr float DragHaloRadius = 13;            // draggable points (Desmos halo)
inline constexpr float DragHaloOpacity = 0.25f;
inline constexpr float InlineFieldSize = 15;           // slider bounds, domains, image fields
inline constexpr float InlineFieldMinWidth = 36;
inline constexpr float PlayButtonRadius = 10;
inline constexpr float SliderSweepSeconds = 8;         // one sweep across the range while playing
inline constexpr float FolderIndent = 16;
inline constexpr float NoteTextSize = 15;
inline constexpr float NoteLineHeight = 20;
inline constexpr float MiscRowMinHeight = 46;
inline constexpr float TableHeaderHeight = 34;
inline constexpr float TableCellHeight = 32;
inline constexpr float TableColumnWidth = 100;
inline constexpr float TableTextSize = 16;
inline constexpr Color TableHeaderBg = Color::hex(0xF4F4F4);
inline constexpr Color TableGrid = Color::hex(0xDDDDDD);
inline constexpr Color ComputedText = Color::hex(0x000000, 0.45f);
inline constexpr float ImageThumbSize = 56;
inline constexpr Color Placeholder = Color::hex(0x000000, 0.35f);
inline constexpr float RegressionLabelSize = 11;

// ---- math typesetting (MathQuill metrics, as em fractions) ----
inline constexpr float FracScale = 0.9f;
inline constexpr float SupScale = 0.75f;              // 90% of 90%... per spec
inline constexpr float NestedSupScale = 0.85f;
inline constexpr float FracBarThickness = 1.0f;
inline constexpr float FracPadX = 0.2f;               // em
inline constexpr float SupRaise = 0.5f;               // em of parent
inline constexpr float SubDrop = 0.3f;
inline constexpr float OpPad = 0.2f;                  // em around binary operators
inline constexpr Color CursorColor = Color::hex(0x000000);
inline constexpr Color SelectionColor = Color::hex(0xB4D5FE);
inline constexpr Color GhostBracket = Color::hex(0x000000, 0.25f);
inline constexpr float CursorBlinkSeconds = 0.5f;

// ---- graph paper (Desmos graphSettings defaults) ----
inline constexpr Color GraphBg = Color::hex(0xFFFFFF);
inline constexpr float MajorGridOpacity = 0.4f;
inline constexpr float MinorGridOpacity = 0.12f;
inline constexpr float AxisOpacity = 0.9f;
inline constexpr float AxisLineWidth = 1.5f;
inline constexpr float GridLineWidth = 1.0f;
inline constexpr float LabelSize = 14;
inline constexpr float PixelsPerLabel = 80;
inline constexpr float LabelOffset = 5;
inline constexpr float LabelEdgeMargin = 3;
inline constexpr float LabelHalo = 1.5f;
inline constexpr float TickHalfLength = 3;

inline constexpr float CurveWidth = 2.5f;
inline constexpr float CurveWidthHighlight = 3.5f;
inline constexpr float CurveOpacity = 0.7f;
inline constexpr float CurveOpacityHighlight = 1.0f;
inline constexpr float FillOpacity = 0.4f;
inline constexpr float DashLength = 11;               // gap = 7 (Desmos [11, 7] at 2.5 width)
inline constexpr float PointDiameter = 8;
inline constexpr float PoiDiameter = 8;
inline constexpr float HoverPickRadius = 10;
inline constexpr float ImplicitCellPts = 2.5f;         // GPU field grid spacing (points); see calc_bench

// ---- 3D mode (Desmos 3D look) ----
inline constexpr unsigned Surface3DGridN = 192;        // surface grid points per side (GPU-evaluated)
inline constexpr int Max3DSurfaces = 64;
inline constexpr float Surface3DOpacity = 1.0f;
inline constexpr float Floor3DGridOpacity = 0.12f;
inline constexpr float Floor3DGridWidth = 1.0f;
inline constexpr float Axis3DOpacity = 0.7f;
inline constexpr float Axis3DWidth = 1.5f;
inline constexpr float Box3DOpacity = 0.15f;
inline constexpr float Box3DWidth = 1.0f;
inline constexpr float Curve3DSamples = 1024;          // parametric space-curve samples over the t domain
inline constexpr float Point3DDiameter = 9;
inline constexpr float Axis3DLabelSize = 15;
inline constexpr float Tick3DLabelSize = 12;
inline constexpr float Orbit3DSensitivity = 0.01f;     // radians per point dragged
inline constexpr float ModeButtonGap = 8;              // 2D/3D toggle below the zoom buttons
inline constexpr float ModeButtonTextSize = 14;

// ---- graph controls (zoom buttons, top-right) ----
inline constexpr float ZoomButtonSize = 36;
inline constexpr float ZoomButtonRadius = 4;
inline constexpr float ZoomButtonInset = 10;
inline constexpr float ZoomButtonGap = 8;
inline constexpr Color ZoomButtonBorder = Color::hex(0x000000, 0.2f);
inline constexpr Color ShadowColor = Color::hex(0x000000, 0.12f);
inline constexpr float ShadowBlur = 3;

// ---- tooltip / coordinate labels ----
inline constexpr float TooltipRadius = 4;
inline constexpr float TooltipPadX = 8;
inline constexpr float TooltipPadY = 5;
inline constexpr float TooltipTextSize = 14;
inline constexpr Color TooltipBg = Color::hex(0x000000, 0.75f);
inline constexpr Color TooltipText = Color::hex(0xFFFFFF);
inline constexpr float CoordLabelSize = 14;

// ---- interaction ----
inline constexpr float ZoomButtonFactor = 2.0f;
inline constexpr float ScrollZoomSensitivity = 0.0035f;  // per point of scroll delta
inline constexpr float DragThreshold = 3;

} // namespace theme
} // namespace calc
