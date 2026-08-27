// The suite -- do NOT edit it. It uses std.testing.allocator, which reports a
// leak as a test failure. Note how the test itself frees the RESULT with defer;
// your job is to make shout free its own internal temporary the same way.
const std = @import("std");
const s = @import("shout.zig");

test "shout uppercases, and leaks nothing" {
    const a = std.testing.allocator;
    const r = try s.shout(a, "hello");
    defer a.free(r);
    try std.testing.expectEqualStrings("HELLO", r);
}
