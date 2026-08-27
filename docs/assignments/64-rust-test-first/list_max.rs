// The largest element of a non-empty slice. It looks right and passes on the
// slices people usually try. It is wrong -- the same bug the C course's
// stats_max had, in a functional coat.
pub fn list_max(xs: &[i64]) -> i64 {
    let mut m = 0; // seeds at 0
    for &x in xs {
        if x > m {
            m = x;
        }
    }
    m
}
