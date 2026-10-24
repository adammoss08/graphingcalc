#pragma once
// MathTree <-> LaTeX, in the dialect Desmos uses for copy/paste and saved graph state.
#include <string>

namespace calc {
struct Block;
std::string blockToLatex(const Block& b);
void latexToBlock(const std::string& latex, Block& out);  // appends parsed nodes to `out`
}
