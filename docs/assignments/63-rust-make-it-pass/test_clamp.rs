// The truth for this task -- do NOT edit it; fix clamp.rs until it is green.
// `rustc --test` builds a test runner from the `#[test]` functions and exits
// nonzero if any fails -- no cargo, no framework needed.
#[path = "clamp.rs"]
mod clamp;
use clamp::clamp;

#[test]
fn above_hi_clamps_to_hi() {
    assert_eq!(clamp(9, 0, 5), 5);
}
#[test]
fn below_lo_clamps_to_lo() {
    assert_eq!(clamp(-3, 0, 5), 0);
}
#[test]
fn in_range_passes_through() {
    assert_eq!(clamp(2, 0, 5), 2);
}
