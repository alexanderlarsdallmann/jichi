// The spec -- do NOT edit it. Make rpn.rs pass it.
#[path = "rpn.rs"]
mod rpn;
use rpn::rpn_eval;
use rpn::Token::*;

#[test]
fn evaluates() {
    assert_eq!(rpn_eval(&[Num(2), Num(3), Add]), Ok(5));
    assert_eq!(rpn_eval(&[Num(4), Num(2), Sub]), Ok(2)); // order matters
    assert_eq!(rpn_eval(&[Num(3), Num(4), Mul]), Ok(12));
    assert_eq!(rpn_eval(&[Num(2), Num(3), Num(4), Mul, Add]), Ok(14));
    assert_eq!(rpn_eval(&[Num(10), Num(2), Num(3), Add, Sub]), Ok(5));
    assert_eq!(rpn_eval(&[Num(7)]), Ok(7));
}

#[test]
fn malformed_is_err() {
    assert!(rpn_eval(&[Add]).is_err());
}
