#!/bin/sh
# smoke lint: no orphaned UNIT tests -- every test function defined is wired into
# the runner, and every declaration corresponds to one (M390).
#
# The smoke tier has enforced "no orphans: all N drivers are named in run.sh"
# since M271; the unit tier -- 146 test functions -- enforced nothing of the kind,
# and the toolchain cannot help: a non-static `void test_foo(void)` that nobody
# calls is perfectly legal C. So a whole test file can go dark (added, compiled,
# never called) with the suite total still only growing, which is exactly the
# invisibility TEST_INTEGRITY recommendation 3 was written about and the way
# zigodot's reachability problem stayed hidden until someone went looking.
#
# THREE DIRECTIONS, because each is invisible in a different way:
#   1. defined, never called  -- the real gap; no compiler or linker sees it.
#   2. called, never defined  -- normally a link error, so the build catches it;
#      checked anyway because a call inside a disabled block would not link-fail.
#   3. declared, never defined -- a dead declaration in jc_test.h links fine, so
#      it too is silent (the M285 rule: a declared-but-dead name is worse than an
#      absent one).
#
# STATED SCOPE (the M305 rule): only column-0 `void test_*(void)` signatures count
# as suite entry points. `static void test_*` helpers (11 in test_acp.c alone) are
# deliberately excluded -- they are internal to a file, not suite entries. A
# NON-static helper named test_* that is called from another test rather than from
# the runner would be flagged; the failure message names both remedies, because a
# finding with no way forward is a loop amplifier (M342/M360).
. "$(dirname "$0")/_smoke.sh"

t_plan 5
tmp=$(smoke_tmp)

T="$SMOKE_ROOT/tests"

# Definitions: anchored at column 0, so `static void test_...` never matches.
grep -hoE '^void test_[a-z0-9_]+\(void\)' "$T"/test_*.c \
    | sed 's/^void //;s/(void)//' | sort -u > "$tmp/def"

# Calls in the runner. A leading space is prepended so the boundary match needs
# no \b (a GNU extension this tier cannot assume -- it runs on old systems).
sed 's/^/ /' "$T/test_main.c" \
    | grep -oE '[^a-zA-Z0-9_]test_[a-z0-9_]+\(\);' \
    | sed 's/^.//;s/();//' | sort -u > "$tmp/call"

# Declarations in the shared header.
grep -oE '^void test_[a-z0-9_]+\(void\);' "$T/jc_test.h" \
    | sed 's/^void //;s/(void);//' | sort -u > "$tmp/decl"

ndef=$(grep -c . "$tmp/def")
if [ "$ndef" -ge 100 ]; then
    t_ok "extracted $ndef unit test functions (floor 100)"
else
    t_fail "only $ndef test functions extracted -- the signature style moved; fix the extraction, not the floor"
fi

dark=$(comm -23 "$tmp/def" "$tmp/call" | tr '\n' ' ')
if [ -z "$dark" ]; then
    t_ok "every defined test function is called by the runner"
else
    t_fail "test function(s) defined but never run: $dark
    -- wire each into tests/test_main.c (and tests/jc_test.h), or make it
       'static' if it is a helper rather than a suite entry point"
fi

ghost=$(comm -13 "$tmp/def" "$tmp/call" | tr '\n' ' ')
if [ -z "$ghost" ]; then
    t_ok "every called test function is defined"
else
    t_fail "runner calls undefined test function(s): $ghost"
fi

deadd=$(comm -23 "$tmp/decl" "$tmp/def" | tr '\n' ' ')
if [ -z "$deadd" ]; then
    t_ok "every declaration in jc_test.h has a definition"
else
    t_fail "dead declaration(s) in tests/jc_test.h: $deadd"
fi

# The matcher can miss (two-sided; the config_keys_lint rule).
if grep -q "test_zzz_invented" "$tmp/def" "$tmp/call" "$tmp/decl" 2>/dev/null; then
    t_fail "an invented test name was found -- the extraction is broken"
else
    t_ok "the matcher can miss: an invented test name is in none of the sets"
fi

t_done
