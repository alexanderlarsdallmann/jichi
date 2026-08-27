// The spec -- do NOT edit it. Make rpn.zig pass it.
const std = @import("std");
const r = @import("rpn.zig");
const Token = r.Token;

test "rpn" {
    try std.testing.expectEqual(@as(i64, 5), try r.rpnEval(&[_]Token{ .{ .num = 2 }, .{ .num = 3 }, .add }));
    try std.testing.expectEqual(@as(i64, 2), try r.rpnEval(&[_]Token{ .{ .num = 4 }, .{ .num = 2 }, .sub }));
    try std.testing.expectEqual(@as(i64, 12), try r.rpnEval(&[_]Token{ .{ .num = 3 }, .{ .num = 4 }, .mul }));
    try std.testing.expectEqual(@as(i64, 14), try r.rpnEval(&[_]Token{ .{ .num = 2 }, .{ .num = 3 }, .{ .num = 4 }, .mul, .add }));
    try std.testing.expectEqual(@as(i64, 5), try r.rpnEval(&[_]Token{ .{ .num = 10 }, .{ .num = 2 }, .{ .num = 3 }, .add, .sub }));
    try std.testing.expectEqual(@as(i64, 7), try r.rpnEval(&[_]Token{.{ .num = 7 }}));
}

test "malformed expression is an error, not a crash" {
    try std.testing.expectError(r.RpnError.StackUnderflow, r.rpnEval(&[_]Token{.add}));
}
