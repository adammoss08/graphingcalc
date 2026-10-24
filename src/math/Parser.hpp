#pragma once
// MathTree -> Ast, following Desmos rules: implicit multiplication, operator names that take an
// unparenthesised argument ("sin 2x"), f(x) only when f is a known function, sin^{-1} -> arcsin.
#include "math/Ast.hpp"

#include <set>
#include <stdexcept>
#include <string>

namespace calc {

struct Block;

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// `functionNames`: names defined as functions anywhere in the graph (so "f(x)" is a call, not f·x).
Statement parseStatement(const Block& root, AstPool& pool, const std::set<std::string>& functionNames);

// If the row looks like "name(args) = ...", returns name (used to pre-collect function names).
std::string peekFunctionDefinitionName(const Block& root);

// Canonical identifier name for a letter plus optional subscript block, e.g. "a_{1}".
std::string identifierName(char32_t letter, const Block* subscript);

} // namespace calc
