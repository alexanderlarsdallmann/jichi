// The spec -- do NOT edit it. Make rpn.hpp pass it.
#include "rpn.hpp"
#include <cassert>

int main()
{
    assert(rpn_eval({{Token::Num, 2}, {Token::Num, 3}, {Token::Add, 0}}) == 5);
    assert(rpn_eval({{Token::Num, 4}, {Token::Num, 2}, {Token::Sub, 0}}) == 2); // order matters
    assert(rpn_eval({{Token::Num, 3}, {Token::Num, 4}, {Token::Mul, 0}}) == 12);
    assert(rpn_eval({{Token::Num, 2}, {Token::Num, 3}, {Token::Num, 4}, {Token::Mul, 0}, {Token::Add, 0}}) == 14);
    assert(rpn_eval({{Token::Num, 10}, {Token::Num, 2}, {Token::Num, 3}, {Token::Add, 0}, {Token::Sub, 0}}) == 5);
    assert(rpn_eval({{Token::Num, 7}}) == 7);

    // A malformed expression must throw, not crash or return garbage.
    bool threw = false;
    try {
        rpn_eval({{Token::Add, 0}});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
    return 0;
}
