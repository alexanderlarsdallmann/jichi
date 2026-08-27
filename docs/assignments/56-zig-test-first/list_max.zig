// The largest element of a non-empty slice of i64. It looks right and passes on
// the slices people usually try. It is wrong -- the same bug the C course's
// stats_max had, in a functional coat.
pub fn listMax(xs: []const i64) i64 {
    var m: i64 = 0; // seeds at 0
    for (xs) |x| {
        if (x > m) m = x;
    }
    return m;
}
