#!/bin/sh
# smoke lint: a first-party C file that CALLS offsetof must #include <stddef.h>
# itself (M1161, the platform batch; SV:1722.2).
#
# The tooth, measured on the host: on the Pi 500 (Debian 12, glibc 2.36)
# tests/tools/sockbind.c does not BUILD -- `error: expected expression before
# 'struct'` at its offsetof (sockbind.c:98). It gets offsetof today only from
# another header's TRANSITIVE include, and glibc 2.36's <sys/un.h> does not
# pull <stddef.h>; the dev box's glibc does, so the tree compiled green here
# and died there. The rule is "include what you use": a file that calls
# offsetof must include <stddef.h>, so the call compiles on every row, not
# only on a header set that happens to leak it (the same class as
# sprintf_lint: a dependency held by reviewer memory and by the dev box's
# luck, made a lint instead).
#
# UNIVERSE (measured 2026-11-12, run 0043): the first-party C files that call
# offsetof -- every *.c/*.h under src/ and include/, and the tests' own C
# (tests/*.c|h, tests/tools/*.c|h, tests/fuzz/*.c|h, sprintf_lint's shape;
# tests/bench and tests/measure are data, not code) -- are EXACTLY two:
# src/chat/jc_agent.c (calls it for the mid-turn persist helper; its include
# is the M1083 D-08 line) and tests/tools/sockbind.c (its include is M1161
# fix 1). FLOOR 2 is asserted as a floor, sprintf_lint's way: a count under 2
# says a live call vanished without its line being read, the same red as a
# broken scan. A third file that starts calling offsetof is NOT a red -- the
# check is per-file, so it is covered the moment the file exists; that is why
# this is a lint and not an allowlist.
#
# The tooth: delete <stddef.h>'s include from EITHER file and check 2 goes red
# (its red-first leg IS the base tree, which lacks sockbind.c's include).
#
# Checks (t_plan 2): 1 the offsetof file-count floor (2); 2 every file that
# calls offsetof includes <stddef.h>.
# /usr/bin/grep, not a session's grep (CLAUDE.md: in this session bare grep is
# a shell-function shim -- the 1,443-vs-0 incident), in both checks.
. "$(dirname "$0")/_smoke.sh"

# Guarded /usr/bin/grep for portability (systems without merged /usr have grep at /bin/grep).
G=/usr/bin/grep
[ -x "$G" ] || G=grep

t_plan 2

# The universe, sprintf_lint's shape: product source plus the tests' own C
# (tests/bench holds fixtures and a benchmark's workspaces -- data, not code;
# tests/measure's .c-free Python is outside the *.c/*.h scan anyway).
targets=$(find "$SMOKE_ROOT/src" "$SMOKE_ROOT/include" \
    \( -name '*.c' -o -name '*.h' \) 2>/dev/null)
tests_c=$(for f in "$SMOKE_ROOT"/tests/*.c "$SMOKE_ROOT"/tests/*.h \
                   "$SMOKE_ROOT"/tests/tools/*.c "$SMOKE_ROOT"/tests/tools/*.h \
                   "$SMOKE_ROOT"/tests/fuzz/*.c "$SMOKE_ROOT"/tests/fuzz/*.h; do
              [ -f "$f" ] && printf '%s\n' "$f"
          done)
targets="$targets
$tests_c"

# The files that CALL offsetof. A call is `offsetof(`, not the word: a
# comment or a struct field named offsetof never trips it (neither exists in
# the universe today; the floor check guards the scan, the per-file check the
# rule). Per-file grep -q (not -l over the list): no `path:` prefix to parse,
# and an empty list cannot make grep read stdin.
offfiles=""
for f in $targets; do
    [ -f "$f" ] || continue
    if $G -qF 'offsetof(' "$f"; then
        offfiles="$offfiles $f"
    fi
done

noff=$(printf '%s\n' $offfiles | grep -c .)
if [ "$noff" -ge 2 ]; then
    t_ok "offsetof universe: $noff file(s) call offsetof (floor 2, measured 2026-11-12 run 0043: jc_agent.c, sockbind.c)"
else
    t_fail "offsetof universe: only $noff file(s) call offsetof (floor 2) -- a live call vanished or the scan broke (the floor exists so this reads as a red, not a green)"
fi

missing=""
for f in $offfiles; do
    if ! $G -qF '#include <stddef.h>' "$f"; then
        missing="$missing $f"
    fi
done
if [ -z "$missing" ]; then
    t_ok "every offsetof caller includes <stddef.h> (the dev box's header leak must not be the platform row's missing include)"
else
    t_fail "offsetof without #include <stddef.h> (include what you use -- it only compiled on a glibc whose headers leak it):$missing"
fi

t_done
