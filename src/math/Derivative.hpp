#pragma once
// Symbolic differentiation on resolved ASTs (after inlining; no UserCall/Index nodes).
// Used for d/dx and f'(x). Constructors simplify as they build (0 and 1 elimination, constant
// folding) so derivative programs stay small enough for the GPU stack.
#include "math/Ast.hpp"

#include <string>

namespace calc {

int differentiate(AstPool& pool, int node, const std::string& var);

// Copy of `root` with every Var `name` replaced by the subtree `replacement` (same pool).
int replaceVar(AstPool& pool, int root, const std::string& name, int replacement);

} // namespace calc
