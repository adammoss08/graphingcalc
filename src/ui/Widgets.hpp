#pragma once
// Shared controls so hover/pressed/focus states look identical everywhere.
#include "render/Batch2D.hpp"

namespace calc::widgets {

enum class Icon { Plus, Minus, Home, Undo, Redo, Wrench, CollapseLeft, ExpandRight, Close, Warning, Play, Keyboard };

enum class Interaction { Normal, Hovered, Pressed, Disabled };

void drawIcon(Batch2D& b, Icon icon, float cx, float cy, float size, Color c);

// Top-bar style: transparent until hovered (Desmos .dcg-icon-btn, 40x40, radius 5).
void iconButton(Batch2D& b, const Rect& r, Icon icon, Interaction s);

// Graph-paper style: white, 1px border, soft shadow (zoom buttons). `roundTop`/`roundBottom`
// allow stacking buttons into one group.
void floatingButton(Batch2D& b, const Rect& r, Icon icon, Interaction s, bool roundTop = true,
                    bool roundBottom = true);

// Pill button with text (e.g. "add slider: a").
float pillButton(Batch2D& b, float x, float y, const std::string& label, Interaction s, bool measureOnly = false);

// Dark tooltip/coordinate box with white text, anchored at (x, y) top-left.
void tooltip(Batch2D& b, float x, float y, const std::string& text);

} // namespace calc::widgets
