// A token is a number or an operator. In Rust that is an enum -- a real sum
// type -- and `match` on it is exhaustive (miss a variant and it will not
// compile). The Token enum is given (the suite builds it); implement rpn_eval.
#[derive(Clone, Copy)]
pub enum Token {
    Num(i64),
    Add,
    Sub,
    Mul,
}

// Evaluate a reverse-Polish (postfix) expression. Return `Result<i64, String>`:
// `Ok(value)` on success, `Err(msg)` on a malformed expression -- errors as
// values, Rust's (and jichi's) rule, enforced by the type. The `?` operator
// propagates an `Err` for you.
//
// TODO: implement this. test_rpn.rs is the spec -- do NOT edit it. A one-line
// DESIGN.md is part of the task.
pub fn rpn_eval(tokens: &[Token]) -> Result<i64, String> {
    let _ = tokens;
    Ok(0) // <-- replace with a real implementation
}
