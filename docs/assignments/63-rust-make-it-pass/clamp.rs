// Clamp x into the inclusive range [lo, hi].
pub fn clamp(x: i64, lo: i64, hi: i64) -> i64 {
    if x < lo {
        return lo;
    }
    if x > hi {
        return x; // <-- one of these lines is wrong
    }
    x
}
