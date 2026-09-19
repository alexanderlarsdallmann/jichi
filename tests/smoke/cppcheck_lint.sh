#!/bin/sh
# smoke lint: the optional C++ front-end tier actually runs (M636).
#
# THE DEFECT. `make cpp-check` -- the second-front-end sieve M188 built and
# docs/CPP_BUILD.md publishes -- failed on a clean tree for 154 commits:
#
#     cpp-check FAIL: src/util/jc_buildrev.c
#         fatal error: jc_buildrev_stamp.h: No such file or directory
#
# src/util/jc_buildrev.c includes a GENERATED header (M495, the build stamp), and
# `make clean` deletes it (M593). The cpp-check rule declared no prerequisite on
# it, so the target was green only when some earlier build had already generated
# the header, and red when run the way it is documented -- `make clean` first.
# Both directions were measured before the fix.
#
# WHY A LINT AND NOT THE GATE. M188 kept cpp-check out of `make ci` on purpose:
# the sources are C89 and the C gate stays the gate. That decision holds; what
# does not hold is leaving nothing in its place, because the project has learned
# this exact lesson twice already -- the FAULT=1 drivers named in no target ran
# nowhere until M482, and smoke_lint check 16 now enforces the general form. This
# file is that enforcement for the C++ tier, at ~2 s instead of the 55 s a full
# three-front-end sweep costs.
#
# UNIVERSE, measured 2026-09-16. GENERATED HEADERS: 1 -- include/jc_buildrev_stamp.h,
# the only file in the tree written by a Makefile recipe and included by a source.
# SOURCES INCLUDING ONE: 1 -- src/util/jc_buildrev.c. C++ FRONT-ENDS available
# here: g++ 15.2.0, clang++ 21.1.8, `zig c++` 0.16.0. The first two compile a .c
# input as C++; `zig c++` does NOT (it compiles C, exactly as `zig cc` does) and
# reaches its C++ front-end only through the -x c++ that check 4 pins.
#
# Checks 4-6 are text checks on the recipe. They exist because the behavioural
# check can only run the front-ends this machine has, and the three defects they
# pin were each invisible to g++: -x c++ was needed by `zig c++`, the probe by
# `zig c++`, and $(firstword) by any multi-word driver. A machine with only g++
# would pass check 3 and still ship all three.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
G=/usr/bin/grep
[ -x "$G" ] || G=grep
tmp=$(smoke_tmp)
cd "$SMOKE_ROOT" || exit 1

STAMP_PATH=include/jc_buildrev_stamp.h

# ---- 1: the universe, enumerated twice -----------------------------------
# Route A: headers PRESENT in the tree but untracked -- what the build actually
# generated. Route B: headers a first-party source includes that resolve to no
# tracked file -- what the sources actually demand. Two independent derivations,
# one from the filesystem and one from the include graph; they must agree and
# must be non-empty, because an extraction that finds nothing makes every check
# below vacuous while printing OK.
#
# Route A does NOT key on a Makefile variable NAME, which is how this check was
# first written and why it was vacuous: the pattern wanted STAMP or GEN in an
# all-letter identifier, so a second generated header called anything else --
# MANPAGE, STAMP2_GEN -- was invisible to the very check meant to notice it.
# THE INSTRUMENT OF BOTH ROUTES IS GIT, and on a rig target there is no repo.
# scripts/_rig_ship.sh ships a tar built from `git ls-files` and deliberately
# EXCLUDES .git ("that would carry build artifacts, .git, and every cache"), so
# on FreeBSD, illumos and both Pi rows `git ls-files` matched nothing, every
# header looked untracked, and this check went RED on a tree where nothing was
# wrong -- on every non-host row at once. A check whose instrument is absent must
# SKIP and say so, which is what check 3 already does for the C++ front-end and
# what M625 states as a principle; failing instead reports a defect in the
# subject when the defect is in the harness. Checks 2 and 4-6 are text checks on
# the Makefile and the sources, so they still run on target -- and check 1 keeps
# its full strength on the host, where `make ci` is the gate.
if ! command -v git >/dev/null 2>&1 || ! git rev-parse --git-dir >/dev/null 2>&1
then
    t_skip_one "not a git checkout -- the tracked/untracked split is the \
instrument this check enumerates the universe with (a shipped rig tree has no .git)"
else
route_a=$(find include src -name '*.h' -type f 2>/dev/null | sort -u \
          | while read -r h; do
                git ls-files --error-unmatch "$h" >/dev/null 2>&1 || echo "$h"
            done | sort -u)
# Route B resolves each quoted include by BASENAME against the tracked files,
# not against include/ -- src/ carries convert_internal.h, mcp_internal.h,
# net_util.h, prov_internal.h and tool_util.h beside their sources, and an
# include/-shaped assumption reported all five as generated. Four checks in one
# session went wrong this way before (TEST_INTEGRITY, "Audit the universe");
# this one went wrong on its first run, which is the argument for running it.
route_b=$(for f in $(smoke_srcfiles src c | xargs "$G" -l '#include "' /dev/null 2>/dev/null); do
              "$G" -oE '#include "[A-Za-z0-9_]+\.h"' "$f" | sed 's/.*"\(.*\)"/\1/'
          done | sort -u | while read -r h; do
              [ -n "$h" ] || continue
              git ls-files "*/$h" "$h" 2>/dev/null | "$G" -q . || echo "include/$h"
          done | sort -u)
na=$(printf '%s\n' "$route_a" | "$G" -c . 2>/dev/null || echo 0)
if [ "$na" -eq 1 ] && [ "$route_a" = "$STAMP_PATH" ] && [ "$route_b" = "$STAMP_PATH" ]; then
    t_ok "1 generated header, agreed by both routes (filesystem, include graph): $route_a"
else
    t_fail "the generated-header universe changed or the extraction broke.
  by Makefile variable: ${route_a:-none}
  by untracked include:  ${route_b:-none}
Both routes must name the same set. If a SECOND generated header appeared, every
target that compiles a source including it needs the same prerequisite check 2
makes -- that is the whole defect this file exists for."
fi
fi

# ---- 2: every generated header is a prerequisite of what compiles it -----
# Two targets compile src/util/jc_buildrev.c: the object rule and cpp-check.
# The object rule always had its dependency; cpp-check is the one that did not.
# OPTIONS BEFORE OPERANDS. This read `grep -lE 'pat' src -r`, and only GNU grep
# permutes an option that follows a file operand; POSIX grep takes `-r` as a
# FILE. On OpenBSD the extraction came back EMPTY and the check reported
# `including sources=''` -- a claim that no source includes the generated header,
# on a tree where one does. (2026-09-19.)
inc=$("$G" -rlE '#include "jc_buildrev_stamp\.h"' src 2>/dev/null | sort | tr '\n' ' ')
obj_dep=$("$G" -c '^src/util/jc_buildrev\.o:.*\$(STAMP)' Makefile)
cpp_dep=$("$G" -c '^cpp-check:.*\$(STAMP)' Makefile)
if [ "$inc" = "src/util/jc_buildrev.c " ] && [ "$obj_dep" -ge 1 ] && [ "$cpp_dep" -ge 1 ]; then
    t_ok "both targets that compile $inc declare \$(STAMP) as a prerequisite"
else
    t_fail "including sources='$inc' object-rule dep=$obj_dep cpp-check dep=$cpp_dep
A target that compiles a source including a generated header must depend on that
header, or it is green only by accident of what ran before it."
fi

# ---- 3: behavioural -- cpp-check survives a missing stamp ----------------
# The property, not its spelling. The stamp is removed and `make cpp-check` must
# put it back and pass; the recipe runs for real, over one translation unit --
# the one that includes the generated header, so the check is not vacuous.
if ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
    t_skip_one "no C++ front-end installed"
else
    cp "$STAMP_PATH" "$tmp/stamp.saved" 2>/dev/null
    rm -f "$STAMP_PATH"
    # smoke_make, not bare `make`: FreeBSD's make is bmake and cannot parse
    # this Makefile, so this check reported rc=2 for a target that passes there.
    _mk=$(smoke_make)
    out=$($_mk cpp-check CPPCHECK_SRC=src/util/jc_buildrev.c 2>&1)
    rc=$?
    [ -f "$STAMP_PATH" ] || cp "$tmp/stamp.saved" "$STAMP_PATH" 2>/dev/null
    if [ "$rc" -eq 0 ] && [ -f "$STAMP_PATH" ]; then
        t_ok "cpp-check regenerates the stamp and passes from a clean tree"
    else
        t_fail "cpp-check rc=$rc with the stamp removed:
$(printf '%s\n' "$out" | sed 's/^/    /')
This is the M636 defect returning: the target is green only after another build."
    fi
fi

# ---- 4: the recipe names the language -----------------------------------
# Every driver here dispatches on the .c extension. Without -x c++, g++ tolerated
# it, clang++ warned -Wdeprecated 180 times, and `zig c++` refused every file.
# The pattern is anchored on the recipe body, not on a leading \t: grep -E reads
# a backslash-t as a plain 't', so "^\t" matched nothing and this check reported
# the defect it was written to deny. CLAUDE.md names that exact trap; it caught
# this file on its first run.
# It is asserted on BOTH lines of the recipe, separately, because the first
# spelling of this check matched either one: the probe line and the compile loop
# both read "$(CXX) -x c++ -std=c++17", so deleting -x c++ from the LOOP -- the
# line that compiles all 302 files -- left the check green. $$mode appears only
# on the loop, and probe.cc only on the probe.
loop_x=$("$G" -cE '\$\(CXX\) -x c\+\+ -std=c\+\+17 \$\$mode' Makefile)
probe_x=$("$G" -cE '\$\(CXX\) -x c\+\+ -std=c\+\+17 -fsyntax-only \$\$d/probe\.cc' Makefile)
if [ "$loop_x" -ge 1 ] && [ "$probe_x" -ge 1 ]; then
    t_ok "both the probe and the compile loop pass -x c++ (front-end neutral)"
else
    t_fail "-x c++ is missing: compile loop=$loop_x, probe=$probe_x (want >= 1 each).
Without it the target is g++-only again, and clang++'s deprecation of implicit
.c-as-C++ makes that a broken build rather than a noisy one. A probe that names
the language while the loop does not is worse than neither: it reports the mode
the loop will not use."
fi

# ---- 5: -fsyntax-only is probed, not assumed ----------------------------
# `zig c++` ACCEPTS -fsyntax-only and then fails every file with
# "error: FileNotFound". Asking whether the flag is accepted answers yes; asking
# whether something compiles with it answers no. The recipe must ask the second.
if "$G" -qE 'fsyntax-only \$\$d/probe\.cc' Makefile &&
   "$G" -q 'mode="-c -o /dev/null"' Makefile; then
    t_ok "the syntax-only mode is probed by compiling, with an object fallback"
else
    t_fail "the -fsyntax-only probe or its -c -o /dev/null fallback is gone.
A driver that accepts the flag and then fails every file will report the tree
broken; the probe is what tells those two apart."
fi

# ---- 6: the presence probe handles a multi-word driver ------------------
# `command -v "zig c++"` looks up a command whose name contains a space and finds
# nothing -- a false "cpp-check: zig c++ not found" for any multi-word driver.
if "$G" -qE 'command -v \$\(firstword \$\(CXX\)\)' Makefile; then
    t_ok "the presence probe looks up \$(firstword \$(CXX))"
else
    t_fail "the presence probe no longer uses \$(firstword \$(CXX)) -- a driver
spelled as two words (\`zig c++\`, \`ccache g++\`) is reported missing when it is
installed."
fi

t_done
