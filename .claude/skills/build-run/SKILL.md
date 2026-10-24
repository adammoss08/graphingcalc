---
name: build-run
description: Build, launch, screenshot, or benchmark GraphingCalc. Use when asked to run the app, check a UI change visually, reproduce a rendering bug, or measure performance.
---

# Build / run / see GraphingCalc

All scripts set `DEVELOPER_DIR` themselves (Xcode's `metal` compiler is required).

| Goal | Command |
|---|---|
| Build | `scripts/build.sh` |
| Unit tests | `scripts/test.sh [name-filter]` |
| Launch interactively | `scripts/run.sh [--expr 'y=x^2' ...] [--perf-hud]` |
| See the UI | `scripts/screenshot.sh build/shots/shot.png [--size 1400x900] [--select N] [--hover x,y] [--click x,y]... [--open tests/fixtures/showcase.json] [--image f.png] [--style-menu N] [--keypad] 'y=x^2'` then Read the PNG |
| Perf gate | `scripts/bench.sh --check` |

## Visual verification loop
1. Screenshot with expressions that exercise the change. Write PNGs under `build/shots/` (git-ignored).
   `tests/fixtures/showcase.json` has every row type (note, folder, sliders, domain, table, image).
   `--click x,y` (points) simulates clicks after layout, e.g. to open menus or focus fields.
2. Read the PNG and compare it against `docs/DESMOS_SPEC.md`. Check spacing, colours, line weights,
   label placement and fonts.
3. For behaviour that can't be screenshotted (typing, drag), add a unit test at the
   `App`/`ExpressionList` level. These are driven by synthetic events and need no window.

`--screenshot` renders offscreen at 2× scale (Retina, like the M3 Air) and exits; it never needs
screen-recording permission. `--select N` focuses row N (1-based) so selected-state styling and POIs
show. `--hover x,y` simulates the pointer at screen point (x,y).

## When the build breaks
- Metal shader errors print the `.metal` file and line. Check opcode parity with `src/math/Bytecode.hpp`.
- metal-cpp link errors (`undefined symbol _MTL_PRIVATE_...`) mean `src/render/MetalImpl.cpp` (the one
  TU defining `NS_PRIVATE_IMPLEMENTATION` etc.) is missing from the target.
