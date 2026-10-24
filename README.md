# GraphingCalc

A native macOS graphing calculator that looks and works like the Desmos web calculator, written in
C++20 with [metal-cpp](https://developer.apple.com/metal/cpp/) and Metal Performance Shaders. It is
tuned for an M3 MacBook Air: it opens in about 110 ms, redraws within a frame of each keystroke, and
uses no CPU while idle.

## Features
- Desmos-style structured maths editing (fractions, exponents, roots, brackets, ∑ ∏ ∫, d/dx, f′),
  LaTeX copy/paste, undo/redo
- Explicit, implicit, polar and parametric curves; inequalities with shading; points, lists, piecewise
- Sliders (play, editable bounds/step), parametric/polar domains, function definitions, regressions
  (linear and nonlinear `~`), derivatives, sums, products, integrals
- Notes, folders, tables (computed columns), images
- Style menu (colour, dashed/dotted, thickness, opacity, fill, point style), on-screen keypad
- Points of interest, tracing (including implicit curves), pan/zoom, degrees/radians, grid settings
- Save/open graphs in Desmos's JSON state format

## Build
Requires Xcode (with the Metal toolchain: `xcodebuild -downloadComponent MetalToolchain`) and CMake + Ninja.
```
scripts/build.sh     # build/GraphingCalc.app
scripts/run.sh       # build and launch
scripts/test.sh      # unit + GPU parity + event-level tests
scripts/bench.sh     # performance targets
```

## Layout
See `CLAUDE.md` for the architecture, rules and performance targets, and `docs/DESMOS_SPEC.md` for the
look-and-feel spec (values taken from the Desmos stylesheet and scripts).

GraphingCalc is an independent project. It is not affiliated with or endorsed by Desmos Studio.
