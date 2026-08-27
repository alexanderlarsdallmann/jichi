// The truth for this task -- do NOT edit it; fix clamp.zig until it is green.
// Zig's test runner is built into the compiler: `zig test` finds every `test`
// block, and exits nonzero if any fails -- no framework, no boilerplate.
const std = @import("std");
const c = @import("clamp.zig");

test "above hi clamps to hi" {
    try std.testing.expectEqual(@as(i64, 5), c.clamp(9, 0, 5));
}
test "below lo clamps to lo" {
    try std.testing.expectEqual(@as(i64, 0), c.clamp(-3, 0, 5));
}
test "in range passes through" {
    try std.testing.expectEqual(@as(i64, 2), c.clamp(2, 0, 5));
}
