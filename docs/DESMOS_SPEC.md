# Desmos parity spec

These values come from the live desmos.com/calculator stylesheet and JS bundle (fetched 2026-10-24).
Values marked *(observed)* were judged by eye and are open to correction. `Theme.hpp` must match
this file. When the two disagree, this file wins until it is updated.

## Layout (points, light theme)
| Element | Value |
|---|---|
| App header | height 46 (+1px bottom border `#000`), bg `#2a2a2a`, text `#ddd`. Title "Untitled Graph" left, logo centre. Native: sits in the title-bar area with traffic lights inset. |
| Expression top bar | height 48, bg gradient `#fcfcfc → #eaeaea`, bottom + right border `rgba(0,0,0,.2)`, padding 5. Buttons are 40×40 icon buttons, radius 5: **+** (add menu), undo, redo, settings/wrench, collapse `«` (right). |
| Expression panel | default width 400 *(observed; resizable by dragging divider)*, right border `rgba(0,0,0,.2)`, bg `#fff`. |
| Expression row | top border 1px `rgba(206,206,206,.8)`, min height ~58 *(observed)*, fade-container padding-bottom 6. Math font 115% of 16 ≈ 18.4pt. Math starts after the tab at x = 38 + ~10. |
| Row tab (left gutter) | width 38, bg `#eee`, right border `rgba(0,0,0,.06)`, index number font 65% (≈10.4pt) at top-left padding (1,2), colour `rgba(0,0,0,.65)`. |
| Selected row | tab bg = accent `#2f72dc`, index text white, 2px top/bottom border accent, 1px right border accent. |
| Expression icon | circle 29×29 centred in tab at y = 28 (27 when selected). Shows curve colour as a ring with a white inner area and a coloured stroke-preview line. Hidden expressions show an empty, unfilled ring. Clicking toggles visibility; long-press or right-click opens the colour menu. |
| Delete "×" | at the right of the row, visible on hover/selected, opacity .2 → 1 on hover *(observed)*. |
| New-expression row | an empty row height 54 at the end of the list; clicking it focuses a new expression. |
| Evaluation | value expressions show "= value" right-aligned under the math, font .95rem, padding-right 5. |
| Slider row | min/max/step labels at 85% size. Track with a blue thumb *(observed)*. |
| Graph zoom buttons | top-right of graph: wrench (graph settings), then a grouped **+** / **−** stack, plus a home button that appears only when the view is not default. Buttons are white, 1px `rgba(0,0,0,.2)` border, radius 4, soft shadow, about 36×36 *(observed)*. |
| Tooltip | radius 4, padding 5×8, 14px, dark theme bg `#000` fg `#fff`. |

## Graph paper (from the JS `graphSettings` defaults)
- Background `#fff`. Text/grid colour = text colour `#000` with opacity applied.
- `majorAxisOpacity .4`, `minorAxisOpacity .12` (grid lines are 1px wide), `axisOpacity .9`,
  `axisLineWidth 1.5`, `axisLineOffset .25`.
- `labelSize 14`, `pixelsPerLabel 80`.
- **Grid step:** `target = 80 / screenWidthPx * (xmax - xmin)`. For each `c` in `[1,2,5]`,
  `cand = c * 10^ceil(log10(target / c))`, and major is the smallest cand. Minor = major / `[5,4,5]`[c].
  If x and y pixel scales are within 1% (square), both axes use the same step.
- Axis labels: numbers at each major step. x labels are centred under the tick, `5px` below the
  x-axis. y labels are right-aligned `5px` left of the y-axis, vertically centred. "0" is drawn once,
  at bottom-left of the origin. Labels are clamped inside the screen with a 3px margin; a clamped
  label is drawn in the "hanging" grey `#999` *(observed)*. Labels have a white halo (3px stroke).
  Tick marks span ±3px across each axis at each label.
- Default viewport: centred at origin, x from −10 to 10 across the graph width when the graph is
  wider than tall; otherwise y spans ±10. Scale is square.

## Curves
- Colour cycle for new expressions: **RED `#c74440`, BLUE `#2d70b3`, GREEN `#348543`, PURPLE `#6042a6`, BLACK `#000000`**
  (ORANGE `#fa7e19` is available in the palette but skipped by the cycle).
- Line width 2.5, `curveOpacity .7` *(the stroke alpha for lines)*. Point diameter 8 (`pointLineWidth`).
- Inequalities: fill opacity `.4` (`getFillOpacity`). `<`/`>` draw a dashed boundary and `≤`/`≥` a
  solid one. Dash pattern = `[w*11/2.5, w*11/2.5*7/11]` = **11 on, 7 off** at width 2.5.
- Curves are stroked as whole paths, so a single curve never double-blends where it overlaps itself
  (our curve shader does this with tile-memory programmable blending).
- Hovering a curve or clicking it highlights the line (thicker, about 3.5) and shows a grey point
  with a "(x, y)" label. When an expression is selected, POIs (zeros, extrema, intercepts,
  intersections) appear as grey dots; clicking one shows its coordinates.

## Math editor (MathQuill behaviour)
- Variables are Times New Roman italic; digits and operators upright; operator names (sin, cos…)
  upright with .2em padding. Fractions: 90% size, bar 1px, centred. Sup/sub: 90% (70% for nested
  fractions), raised. Sqrt: radical prefix .95em plus overline stem.
- Typing: `/` makes a fraction from the preceding term, `^` superscript, `_` subscript,
  `sqrt`→√, `pi`→π, `theta`→θ, `tau`→τ, `<=`→≤, `>=`→≥, `*`→·, `(`/`[`/`|` auto-pair,
  sin/cos/tan/… become operator names. Arrow keys move through the structure. Backspace on an
  empty structure deletes it. Enter creates a new expression below. Up/down move between rows.
- Undefined single-letter variables raise an "add slider: a" button; clicking it creates `a=1`
  with slider −10..10.

## Interaction
- Drag to pan. Scroll/pinch zooms about the cursor. +/- buttons zoom ×2 about the centre. Home resets.
- Cmd-Z/Cmd-Shift-Z undo/redo across expression edits. Cmd-C copies LaTeX of the selection.
- Graph state saves and loads as Desmos-compatible JSON
  (`{version, graph:{viewport}, expressions:{list:[{type,id,color,latex,hidden}]}}`).

## Row types (Desmos list item types)
- **Note** (`text`): plain text, word-wrapped. Typing `"` into an empty expression converts it.
- **Folder** (`folder`, children carry `folderId`): collapse triangle and title; the folder icon in the
  tab toggles visibility of every child. Children are indented and drawn with a guide line.
- **Table** (`table`): header row (grey `#f4f4f4`), 100pt columns, 32pt cells. The first column is the
  independent variable; others are plotted as points against it (colour icon in the header). A header
  that is an expression of the first column is a computed column (grey values). There is always one
  empty row, and a placeholder column while focused. Desmos default: points on, lines off.
- **Image** (`image`): thumbnail plus center / width / height fields; drawn beneath curves.
- **Sliders**: play button (sweeps back and forth), `min ≤ a ≤ max  Step:` inline editor when a bound
  is clicked (inline field = 1px underline, 2px accent when focused).
- **Parametric / polar**: a `0 ≤ t ≤ 1` (polar: `0 ≤ θ ≤ 12π`) domain line under the math.
- **Values**: rational results get a decimal ⇄ fraction toggle (click the value box).

## Style menu (long-press or right-click a colour icon)
Six swatches (red, blue, green, purple, orange, black); line style solid/dashed/dotted; thickness
(0.5–10, default 2.5); opacity (default .7); fill opacity (inequalities, default .4); point style
point/open/cross and size (default 8); tables: Points / Lines toggles. JSON keys: `lineStyle`,
`lineWidth`, `lineOpacity`, `fillOpacity`, `pointStyle`, `pointSize`, `lines`, `points`.

## Keypad
Drawer across the bottom (232pt, bg `#dbdde1`, white keys, grey function keys, radius 5). Pages: main
(x y a² aᵇ, digits, operators, ( ) < > |a| , ≤ ≥ √ π θ, arrows, ⌫, ↵), functions (trig, inverse trig,
ln/log, e^x, roots, d/dx, f′, ∑ ∏ ∫, mod/floor/round/ceil/min/max/total/mean, [ ], ~), ABC (letters).
