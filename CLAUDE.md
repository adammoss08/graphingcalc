# GraphingCalc — native Desmos-style graphing calculator

A local macOS graphing calculator that looks and behaves like the Desmos web calculator
(desmos.com/calculator), but renders natively with Metal and starts and responds faster.
**Parity with Desmos comes first.** When unsure how something should look or behave, do what
Desmos does. The spec is in `docs/DESMOS_SPEC.md`; update it when you learn something new.

## Stack (non-negotiable)
- C++20, **metal-cpp** (`third_party/metal-cpp`) for all Metal calls. Geometry and per-frame data
  live in `MTL::Buffer`s; there are no per-vertex CPU→GPU copies outside ring buffers.
- **Metal Performance Shaders** for linear algebra, such as regression least squares (`MPSMatrix*`).
  MPS has no C++ binding, so it is reached only through `src/gpu/MpsBridge.{hpp,mm}`.
- Objective-C++ (`.mm`) is allowed **only** in the platform layer (`src/app/Platform*.mm`) and
  `src/gpu/MpsBridge.mm`. Everything else is portable C++ plus metal-cpp.
- Text uses CoreText (a C API), rasterised once into a glyph atlas texture (`src/render/TextAtlas`).
- Shaders: `shaders/*.metal` are compiled offline into `default.metallib` inside the app bundle.

## Build / run / test
```
scripts/build.sh            # configure + build (Release w/ debug info) -> build/GraphingCalc.app
scripts/test.sh             # build + run unit tests (tests/*.cpp, no external framework)
scripts/run.sh              # build + launch with diagnostics (log in build/logs, crash summary;
                            # CALC_VALIDATE=1 adds Metal API/shader validation, CALC_VERBOSE=0 quiets)
scripts/screenshot.sh out.png [latex...]   # launch with expressions, capture window PNG, quit
scripts/bench.sh [--check]  # perf benchmarks vs the targets below; --check exits 1 on a miss
```
`DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer` is required for the `metal` compiler;
the scripts set it. A Stop hook (`.claude/hooks/check.sh`) runs build + tests + `bench.sh --check`
whenever sources changed and blocks finishing while any of them fail. Fix the problem; don't bypass the hook.

## Target hardware: MacBook Air 13" M3 (baseline, tune for this first)
- **60 Hz, no ProMotion** (16.6 ms frame). What matters is input lag, so a frame should take about
  1–2 ms of GPU time.
- **Fanless.** Redraw only on change: no constant render loop, no idle busy work, cache everything.
  Sustained-load throttling is a real failure mode here.
- **Unified memory.** Every `MTL::Buffer` uses `StorageModeShared`; the CPU writes curve and instance
  data straight into GPU-visible memory with no blits. Per-frame data rotates through 3 buffers
  gated by a `dispatch_semaphore` (`render/RingBuffer`), so the CPU never stalls on the GPU.
- **No fp64 on the GPU.** Explicit curves and POIs are evaluated on the CPU in `double`. GPU kernels use
  `float`; when float can't resolve an implicit-grid cell (deep zoom far from the origin) the field and
  edge crossings are computed on the CPU in `double` (`graph/ImplicitCpu`) and uploaded, and the GPU
  still runs marching squares and drawing.
- **M3 Dynamic Caching** makes the branchy, register-heavy per-pixel bytecode VM (implicit curves and
  inequalities) viable on the GPU.
- **4P + 4E cores.** Sampling for multiple expressions runs in parallel via GCD
  (`QOS_CLASS_USER_INTERACTIVE`), one task per dirty expression.
- **Build flags:** `-O3 -flto -mcpu=apple-m3`. **Never `-ffast-math`**, or anything that implies it:
  gaps, undefined regions and asymptotes are found through NaN and infinity.
- **MPS only where it wins:** regression and matrix solves, and large list ops. Its dispatch overhead
  loses to custom kernels on small jobs, so don't force it in.

### Performance targets (enforced by `calc_bench --check` in the Stop hook)
| Metric | Target |
|---|---|
| Cold launch → first frame | < 150 ms |
| Keystroke → updated graph | within 1 frame (≤ 16.6 ms end to end) |
| Pan, 50+ expressions | steady 60 fps; CPU frame work ≤ 4 ms |
| Full-window implicit curve | < 2 ms GPU time |
| Zoom a heavy curve (512-term Fourier) | ≤ 4 ms CPU per frame; full resample ≤ 60 ms in the background |

Where the speed comes from, in priority order:
1. Incremental re-evaluation through the dependency graph: editing one expression recomputes only
   it and its dependents.
2. Curve caches: when panning, reuse the samples already taken and sample only newly exposed regions;
   refine progressively rather than starting over. Curves that took > 4 ms to sample resample on a
   background queue while the old samples are drawn rescaled (`CurveStyle.zoom`); results land via
   the main queue (`GraphView::asyncSampling`, interactive only; headless tools stay synchronous).
   Evaluation is batched (sums/products/integrals included) and large batches use every core
   (`evaluateBatchParallel`); parametric refinement is breadth-first so each round is one batch.
3. The entire UI (rects, circles, glyphs) is one instanced draw, and curves one or two more.
4. Measure, don't guess: `--perf-hud` shows CPU and GPU-timestamp timings per frame; use Instruments'
   Metal System Trace for deeper profiling.

## Architecture (data flows top → bottom)
```
src/app      Platform.mm  NSApp/NSWindow/CAMetalLayer view, events, menus, clipboard, file/image panels,
                          CLI flags (--expr --open --image --screenshot --size --select --hover --click
                          --view --style-menu --keypad --3d --dark --light --verbose --launch-bench --perf-hud). Device + App::init
                          run on a background thread during AppKit startup (launch target).
             App          owns everything; routes events (popovers > keypad > top bar > list > graph);
                          analysis (tables become synthetic rows), graph items, undo, frame building
             GraphState   Desmos-compatible graph JSON: expression/text/folder/table/image items
src/ui       Theme.hpp    ALL colours, sizes, fonts, timings (design tokens). No literals elsewhere.
                          Colours that change in dark mode are runtime variables (Theme.cpp swaps
                          them; View > Appearance: System/Light/Dark). Use Paper/Ink, not White/Black,
                          for backgrounds/lines; draw curve colours through onPaper().
             Widgets      shared icons + button styles (hover/pressed/disabled look identical everywhere)
             MathView     MathQuill-style typesetting (incl. ∑ ∏ ∫ d/dx), cursor/selection, hit testing
             RowModel     row types, Style, MathField/TextField, Focus {row, field, col, cell}
             ExpressionList  container: focus, layout, keyboard routing, shared row chrome
               RowExpression  math, "= value" (decimal/fraction toggle), regressions, sliders (play,
                              inline bound editor), parametric/polar domain fields, suggestions
               RowTable       tables (computed columns, placeholder row/column, cell navigation)
               RowMisc        notes, folders, images
             Popovers     style menu (colour, line style/width/opacity, fill, points) and the + menu
             Keypad       on-screen keypad (main / functions / ABC pages)
             GraphView    grid/axes/labels, curve caches + parallel sampling (GCD), POIs, trace (incl.
                          implicit curves), pan/zoom, zoom buttons, settings popover; takes GraphItems
               GraphView3D   3D mode: Scene3D (GPU surfaces, space curves and 2D curves on z = 0
                             sampled once in world units, points, box/floor grid/axes as 3D lines),
                             projected axis letters + ticks; drag orbits, zoom changes the box size L
src/math     MathTree     editable structured math + all MathQuill editing commands
             Latex        MathTree <-> LaTeX (copy/paste, save files)
             Parser       MathTree -> Ast (implicit multiplication, function application, d/dx, f',
                          ∑/∏/∫, ~)
             Compiler     resolve (inline defs/functions, derivatives, degrees, reductions) -> broadcast
                          lists -> fold -> stack bytecode (Bytecode.hpp, shared with shaders/vm.h)
             Derivative   symbolic differentiation (chain rule for every builtin, Leibniz for ∫)
             Analyzer     whole-graph: definitions, classification, regressions, side fields, row hash;
                          in 3D (`AnalyzerSettings.threeD`) z is a coordinate: Surface, Parametric3D, Point3D
             Regression   least squares (CPU double; MPS Gram + double refinement for large n·k) and
                          Levenberg–Marquardt for nonlinear models
             Vm/Builtins  CPU double interpreter (scalar + batched; loops for ∑ ∏ ∫)
src/graph    Viewport, Camera3D (orbit camera, box [-L, L]^3), Grid (Desmos 1-2-5 steps), Sampler (adaptive explicit/parametric/polar,
             off-screen culling, chord simplification, pan-extension), Poi, ImplicitCpu (deep zoom)
src/render   Renderer     one command buffer per frame: implicit compute (field -> edge refine ->
                          marching squares -> indirect args) then a single render pass; images.
                          Pass has a memoryless Depth32Float attachment (RenderFormats.hpp)
             Renderer3D   3D mode: surfaceGrid compute (192² VM evaluations per surface, skipped while
                          programs + L are unchanged, so orbiting only redraws), indexed lit surfaces,
                          depth-tested capsule lines/points, all scissored to the graph area
             Batch2D      every UI primitive in one instance buffer (SDF rects/lines/glyphs/images)
             TextAtlas    CoreText rasteriser + R8 atlas;  RingBuffer: triple-buffered shared buffers
             ShaderTypes  structs shared with MSL
src/gpu      MpsBridge.mm the only MPS code (MPSMatrixMultiplication for regression Gram matrices)
shaders/     ui.metal, curve.metal (tile-memory "no double blend" curves, dashed/dotted, fills),
             implicit.metal (+ vmEvalPoints test hook), vm.h (GPU float VM incl. loop opcodes),
             surface.metal (3D surfaces + lines)
tests/       test_math, test_vm (GPU vs CPU parity), test_regression_mps, test_app, test_app3d, test_rows
             (event-level row/editor behaviour), fixtures/showcase.json, bench.cpp (perf gate)
```
Precision: the CPU evaluates in `double`, the GPU in `float` relative to the viewport centre.
Explicit curves, POIs and regressions use the CPU path; implicit equations and inequality shading
use the GPU (CPU double fallback at deep zoom). Constant rational exponents (x^(1/3)) compile to
OP_POW_ODD/EVEN so negative bases work identically on both VMs; variable exponents of negative bases
are CPU-only Desmos semantics.

### Known gaps vs Desmos (next work)
Image drag handles on the graph; draggable points with expression coordinates (only plain slider
variables drag today); actions/tickers; recursion; complex numbers; geometry and 3D calculators;
accessibility (audio trace). Tables: column resize and drag-select.
3D mode: implicit surfaces (x²+y²+z²=9, needs marching cubes), trace/POIs/point dragging in 3D,
surfaces of x or y (x = f(y, z)), translucent-surface sorting, camera not saved in files, 3D
inequalities. The 2D/3D button and View ▸ 3D Mode (⌘3) switch modes; `graph.threeDMode` is saved.

Diagnostics: `src/Log.hpp` (`log::error` always, `log::info` when verbose). GPU command-buffer
failures, row errors and slow frames are logged; never swallow an error silently.
Version: `project(GraphingCalc VERSION x.y.z)` in CMakeLists.txt feeds the bundle's version keys.

## Rules
- **Design consistency:** every colour, size, radius, font size and animation duration comes from
  `src/ui/Theme.hpp`. Interactive elements use the shared helpers in `src/ui/Widgets` so hover,
  pressed and focus states look the same everywhere.
- **Performance:** see the targets above. Render only when something is dirty; never spin a render
  loop while idle. Never allocate an `MTL::Buffer` per frame; use `render/RingBuffer`. A change
  that regresses `calc_bench` past a target is a failing build.
- **Opcode parity:** `src/math/Bytecode.hpp` and `shaders/implicit.metal` must stay in sync. Changing
  one means changing the other, plus a test in `tests/test_vm.cpp`.
- metal-cpp ownership: objects from `new*`/`alloc`/`copy` are owned and must be `release()`d; use
  `NS::SharedPtr` (`NS::TransferPtr`) in members. Wrap per-frame work in an `NS::AutoreleasePool`.
- Behaviour changes to parsing or evaluation need a unit test. UI changes need a screenshot check
  (`scripts/screenshot.sh`), compared against the spec.
- Keep files focused (under ~800 lines). Match the existing style: 4-space indent, `camelCase`
  functions, `PascalCase` types, `m_` members, brace on same line.

## Claude tooling in this repo
- Skills: `build-run` (build, launch, screenshot, inspect), `desmos-parity` (checklist + how to verify).
- Agents: `desmos-parity-reviewer` (compares screenshots/behaviour to the spec),
  `metal-perf-reviewer` (reviews GPU/CPU hot paths against the budget).
