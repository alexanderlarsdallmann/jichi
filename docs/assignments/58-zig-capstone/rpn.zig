// Evaluate a reverse-Polish (postfix) expression. A token is a tagged union --
// Zig's typed sum type -- so "a number or an operator" is a real type, and the
// switch on it is exhaustive (leave a case out and it will not compile). And
// because a malformed expression is a real possibility, rpnEval returns an
// ERROR UNION (RpnError!i64): errors as values, checked by the compiler, jichi's
// house rule made a language feature.
//
// The Token type and RpnError are given (the suite builds them); implement
// rpnEval. TODO: replace the stub. test_rpn.zig is the spec -- do NOT edit it.
// A one-line DESIGN.md is part of the task.
pub const Token = union(enum) {
    num: i64,
    add,
    sub,
    mul,
};

pub const RpnError = error{StackUnderflow};

pub fn rpnEval(tokens: []const Token) RpnError!i64 {
    _ = tokens;
    return 0; // <-- replace with a real implementation
}
