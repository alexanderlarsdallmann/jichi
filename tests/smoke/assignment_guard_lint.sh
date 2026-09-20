#!/bin/sh
# smoke lint: every course toolchain guard declares cannot-run (exit 77),
# never a FAIL exit 1 (M625; the row M624 deferred).
#
# UNIVERSE, STATED: docs/assignments/*/test.sh. A "toolchain guard" is the
# probe at the top of a course script that bails out when the compiler or
# runtime it needs is not usable (M624: usability, not existence -- a rustup
# shim with no toolchain answers `command -v` and fails `--version`). Before
# M625 all of them exited 1, so a missing toolchain GRADED as FAIL and
# `--record` wrote passed:false into the learner's progress file for a
# property of the machine. The contract: a guard says "CANNOT RUN" and exits
# 77 (automake's SKIP), which jc_gradecore reads as a refusal, never a grade.
#
# Enumerated TWICE, by different routes (CLAUDE.md "audit the universe"):
#   A. scripts carrying the "CANNOT RUN" marker (the guard's message);
#   B. scripts invoking one of the ten courses' toolchains at all.
# B minus A is a course that can fail for a missing toolchain with no
# declared guard; A must also never pair its marker with `exit 1`.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
tmp=$(smoke_tmp)
AD="$SMOKE_ROOT/docs/assignments"

# --- A: the marker set, floored at today's exact count ----------------------------
na=0
: > "$tmp/set_a"
for f in "$AD"/*/test.sh; do
    if grep -q "CANNOT RUN" "$f"; then
        na=$((na+1))
        printf '%s\n' "${f#"$AD"/}" >> "$tmp/set_a"
    fi
done
if [ "$na" -eq 61 ]; then
    t_ok "61 scripts carry a CANNOT RUN guard (today's exact count)"
else
    t_fail "marker set is $na, not 61 -- a guard was added or removed; recount and refloor"
fi

# --- 1b: no guard still exits 1 (the defect this lint exists for) -----------------
# The guard construct is one of two shapes: a single line carrying its own
# `exit`, or an echo line whose NEXT line is the block's exit. Only the
# guard's own exit is judged -- an `exit 1` two lines away belongs to a
# neighbouring graded check (a compile failure IS a grade) and must not
# be flagged; the first draft of this check did, 21 times.
: > "$tmp/bad"
for f in "$AD"/*/test.sh; do
    if awk '/CANNOT RUN/ {
               if (/exit/) { if (!/exit 77/) bad = 1; next }
               if (getline > 0 && $0 !~ /exit 77/) bad = 1
           }
           END { exit bad ? 0 : 1 }' "$f"; then
        printf '%s\n' "${f#"$AD"/}" >> "$tmp/bad"
    fi
    # the pre-M625 shape: a not-usable message that still says FAIL / exit 1
    if grep -E 'not usable|no C\+\+ compiler|needs (zig|a C\+\+ compiler)' "$f" \
        | grep -v "CANNOT RUN" | grep -q "FAIL"; then
        printf '%s\n' "${f#"$AD"/}" >> "$tmp/bad"
    fi
done
sort -u "$tmp/bad" -o "$tmp/bad"
if [ ! -s "$tmp/bad" ]; then
    t_ok "no toolchain guard grades: none says FAIL or exits 1"
else
    t_fail "guard(s) still grade a missing toolchain: $(tr '\n' ' ' < "$tmp/bad")"
fi

# --- B: the invocation set, by a different route ----------------------------------
# Scripts that RUN one of the ten courses' toolchains. `python3` joined the list
# at M674 with the Python track, and it had to: route B is the INDEPENDENT route,
# so a toolchain it does not know about is four graders this check cannot see --
# they would sit in route A's marker count and in nobody's invocation count, and
# the disagreement between the two routes is the entire mechanism. Comments and quoted
# strings are stripped first, so a script that merely greps a report for the
# text 'zig cc' (task 19) is not an invocation -- the first draft of this lint
# matched it, which is why the stripping is here.
nb=0
: > "$tmp/set_b"
for f in "$AD"/*/test.sh; do
    if sed -e 's/^[ 	]*#.*//' -e 's/"[^"]*"//g' -e "s/'[^']*'//g" "$f" \
       | grep -qE '(^|[ 	("=:-])(cc|gcc|clang|clang\+\+|g\+\+|c\+\+|zig|rustc|raco|guile|elixir|runghc|clojure|python3)([ 	;)]|$)|\$\{?CXX|\$\{CC'; then
        nb=$((nb+1))
        printf '%s\n' "${f#"$AD"/}" >> "$tmp/set_b"
    fi
done
if [ "$nb" -ge 61 ]; then
    t_ok "invocation set enumerated $nb toolchain-using scripts (>= 61)"
else
    t_fail "invocation set is only $nb -- the second route broke; read $tmp/set_b"
fi

# --- 2: B minus A must be empty -- no unguarded toolchain user --------------------
missing=$(comm -23 "$tmp/set_b" "$tmp/set_a" 2>/dev/null)
if [ -z "$missing" ]; then
    t_ok "every toolchain-using script declares a CANNOT RUN guard"
else
    t_fail "toolchain script(s) with no cannot-run guard: $(printf '%s' "$missing" | tr '\n' ' ')"
fi
t_done
