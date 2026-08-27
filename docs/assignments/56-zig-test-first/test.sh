#!/bin/sh
# The make-it-fail-first task, in Zig. Passes only when ALL hold:
#   1. you wrote test_list_max.zig, and it exists, runs, and passes;
#   2. it actually tests something (>= 3 expect checks -- not a hollow suite);
#   3. an independent acceptance probe confirms the bug is really fixed.
cd "$(dirname "$0")" || exit 1
zig version >/dev/null 2>&1 || { echo "FAIL: zig is not usable -- install Zig (ziglang.org) (or a version-manager shim with no version selected)"; exit 1; }

if [ ! -f test_list_max.zig ]; then
    echo "FAIL: write the failing test first -- test_list_max.zig is missing"; exit 1; fi
zig test test_list_max.zig >/dev/null 2>&1 || { echo "FAIL: your tests do not pass"; exit 1; }
n=$(grep -cE 'std\.testing\.expect' test_list_max.zig)
[ "$n" -ge 3 ] || { echo "FAIL: only $n checks -- pin the behaviour with at least 3"; exit 1; }

# Independent acceptance: the all-negative slice is the case the bug hides in.
cat > _accept.zig <<'ACC'
const std = @import("std");
const lm = @import("list_max.zig");
test "accept" {
    try std.testing.expectEqual(@as(i64, -1), lm.listMax(&[_]i64{ -3, -1, -7 }));
    try std.testing.expectEqual(@as(i64, 9), lm.listMax(&[_]i64{ 5, 2, 9, 1 }));
}
ACC
zig test _accept.zig >/dev/null 2>&1
rc=$?
rm -f _accept.zig
[ $rc -eq 0 ] || { echo "FAIL: listMax is still wrong on an all-negative slice"; exit 1; }
echo "PASS: the failing test was written, and the bug is fixed"
