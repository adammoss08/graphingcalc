---
name: desmos-parity
description: Check or implement Desmos look-and-feel parity (layout, colours, grid, math editor typing behaviour, curve styling). Use when building or reviewing any user-visible feature, or when the user says something "doesn't look/act like Desmos".
---

# Desmos parity

The source of truth is `docs/DESMOS_SPEC.md`. Its values were extracted from Desmos's live CSS and JS.
`src/ui/Theme.hpp` must mirror it.

## When adding or changing a visible feature
1. Find the relevant spec section. If it doesn't exist, get the real value before guessing:
   - Fetch the current CSS/JS bundle URLs from `https://www.desmos.com/calculator` (the `href`/`src` of
     `shared_calculator_desktop-*.css|js`) into the scratchpad, then search with `python3 -I` (the
     JS is about 5 MB on one line; ugrep regexes choke, so use Python `re`).
   - Graph-paper constants live in the JS `graphSettings` defaults (`labelSize`, `majorAxisOpacity`, …).
   - Panel and row styling lives in the CSS under `.dcg-exppanel`, `.dcg-expressionitem`, `.dcg-tab`, `.dcg-mq-*`.
2. Add or update the spec entry, marking judged values *(observed)*.
3. Implement with tokens from `Theme.hpp`, and add new tokens there rather than using literals.
4. Verify with `scripts/screenshot.sh` (see the `build-run` skill) and Read the PNG.

## Behaviour checklist (typing feel is the most noticeable)
- [ ] `/` wraps the previous term as the numerator; `^`/`_` open sup/sub; right-arrow exits.
- [ ] Auto-commands: sqrt, pi, theta, tau, sin…arctan, ln, log, exp, abs; `<=`, `>=`, `*` → `·`.
- [ ] Brackets auto-pair, and typing the closer steps over it.
- [ ] Backspace into a structure enters it; backspace on an empty structure removes it.
- [ ] Enter → new row below; Up/Down move rows; Esc unfocuses.
- [ ] Undefined variable → "add slider" button.
- [ ] Constant expressions show `= value` right-aligned under the math.
- [ ] Clicking the colour circle toggles visibility.
- [ ] New expressions cycle red, blue, green, purple, black.
