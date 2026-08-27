const std = @import("std");

// Return an uppercased heap copy of s. The CALLER owns the result and frees it.
// It works -- and it leaks: `scratch` is a temporary that is never freed. In C
// that leak is invisible until a footprint gauge or valgrind finds it. In Zig,
// std.testing.allocator FAILS the test the moment a test body leaks -- the same
// safety AddressSanitizer gives C, built into the test runner. The fix is one
// line, and it is the Zig habit: `defer`.
pub fn shout(allocator: std.mem.Allocator, s: []const u8) ![]u8 {
    const scratch = try allocator.alloc(u8, s.len);
    // BUG: scratch is never freed -- add a `defer` right here.
    @memcpy(scratch, s);
    const out = try allocator.alloc(u8, s.len);
    for (scratch, 0..) |ch, i| out[i] = std.ascii.toUpper(ch);
    return out;
}
