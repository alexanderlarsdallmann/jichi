#ifndef RPN_HPP
#define RPN_HPP
#include <vector>
#include <stdexcept>

// A token is a number or an operator. C++ has no built-in sum type, so this is
// the common encoding: a `kind` tag plus a value used only when kind == Num.
struct Token {
    enum Kind { Num, Add, Sub, Mul } kind;
    long value; // meaningful only for Num
};

// Evaluate a reverse-Polish (postfix) expression. On a malformed expression
// (an operator with too few operands), THROW std::runtime_error -- the C++ error
// model, the counterpart of returning an error value. The suite checks both the
// happy path and that a bad expression throws.
//
// TODO: implement this. test_rpn.cpp is the spec -- do NOT edit it. A one-line
// DESIGN.md is part of the task.
inline long rpn_eval(const std::vector<Token>& tokens)
{
    (void)tokens;
    return 0; // <-- replace with a real implementation
}

#endif
