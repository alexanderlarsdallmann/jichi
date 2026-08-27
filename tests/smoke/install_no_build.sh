#!/bin/sh
# smoke: `make install` installs; it does not build (M586).
#
# THE DEFECT, measured on the maintainer's own tree. The target read
# `install: all`, and the documented way to install is `sudo make install` -- so
# make rebuilt the ENTIRE TREE AS ROOT before installing. After three installs in
# one day the tree held **352 root-owned files** (every .o, every .d, both
# binaries), and the next ordinary `make` died with:
#
#     error: unable to open output file 'src/util/jc_diff.o': Operation not permitted
#
# WHY IT SURVIVED. `sudo make install` SUCCEEDS. The damage appears at the NEXT
# build, an hour and several commits later, and looks like a toolchain or disk
# problem rather than like the install. A defect whose symptom is separated from
# its cause by a successful command in between is one nobody attributes.
#
# WHAT IS CHECKED here, and what is not:
#
#   checked      -- that the target declares no build prerequisite (check 1),
#                   that it refuses rather than builds when the binaries are
#                   missing (2), that its refusal NAMES THE FIX (3), and that a
#                   real install compiles nothing and copies the binaries (4, 5).
#
#   NOT checked  -- check 4 ("runs no compiler") CANNOT catch `install: all` on a
#                   tree that is already built, because `all` is then satisfied
#                   and nothing compiles. That is the common case, so check 1 --
#                   reading the declaration itself -- is the load-bearing one
#                   here, and check 4 is a backstop for the other ways a build
#                   could creep into this target. Measured: with the defect
#                   restored, check 1 fires and check 4 stays green.
#
#   NOT checked  -- that installing as root works. This tier never runs sudo,
#                   deliberately (docs/TEST_TIERS.md). DESTDIR into a temp dir
#                   exercises every path the root install uses except the
#                   privilege, which is the part that cannot be tested here and
#                   is stated rather than implied.
. "$(dirname "$0")/_smoke.sh"

t_plan 9
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(smoke_tmp)

# M593 extends this driver. `install` now also refuses a binary stamped from a
# DIRTY tree -- reported by the operator, who ran the documented
# `sudo make install` after a gate run and got a `<rev>-dirty` binary on the
# PATH. The stamp is read from $(STAMP), a make variable, so these checks can
# point it at a fabricated header instead of mutating the tree they run in.
printf '#define JC_BUILD_REV "deadbee"\n'       > "$tmp/clean_stamp.h"
printf '#define JC_BUILD_REV "deadbee-dirty"\n' > "$tmp/dirty_stamp.h"

# Checks 4-6 below are about COPYING, not about cleanliness: they are given a
# clean stamp so their verdict does not depend on whether the tree happens to
# have uncommitted work while the tier runs.
CLEAN_STAMP="STAMP=$tmp/clean_stamp.h" 

# --- 1: the declaration itself ------------------------------------------------
# The cheapest and most durable check: `install:` must not list a build target as
# a prerequisite. This is what actually regressed, and it regresses by someone
# typing four characters.
decl=$(grep -nE '^install:' "$ROOT/Makefile" | head -1)
rhs=$(printf '%s\n' "$decl" | sed 's/^[0-9]*:install:*//')
case "$rhs" in
    *all*|*"$(printf '\t')"*|*jichi*)
        t_fail "the install target declares a build prerequisite: '$decl'.
   Since installing is documented as \`sudo make install\`, that rebuilds the
   whole tree AS ROOT and leaves every object file root-owned; the next plain
   \`make\` then fails with 'Operation not permitted'. Install must install." ;;
    *)
        t_ok "install declares no build prerequisite ($decl)" ;;
esac

# --- 2: with nothing built, it refuses instead of building --------------------
# Run in a COPY, because the real tree's binaries exist and because a driver that
# ran `make clean` in the source tree would destroy the build the rest of the
# tier is using.
mkdir -p "$tmp/tree"
cp "$ROOT/Makefile" "$tmp/tree/Makefile"
out=$(cd "$tmp/tree" && make install DESTDIR="$tmp/dest" 2>&1); rc=$?
if [ "$rc" -ne 0 ]; then
    t_ok "with no binaries present, install refuses (exit $rc)"
else
    t_fail "install succeeded with nothing built -- so it built something, which
   under sudo is the defect. Output:
$(printf '%s' "$out" | head_bytes 300)"
fi

# --- 3: and the refusal names the fix, not just the cause ---------------------
# M342/M360: a refusal that states only a cause is the message class that
# amplifies retry loops -- and here the wrong retry (`sudo make` again) is
# exactly what causes the damage.
miss=""
for want in "make -j4" "sudo make install" "make clean"; do
    case "$out" in *"$want"*) ;; *) miss="$miss '$want'" ;; esac
done
if [ -z "$miss" ]; then
    t_ok "the refusal names the build command, the install command and the recovery"
else
    t_fail "the refusal does not name:$miss. A reader who hits this must be told
   to build as their normal user, and -- if a root-owned tree already exists --
   that plain \`make clean\` fixes it without sudo. Saw:
$(printf '%s' "$out" | head_bytes 300)"
fi

# --- 4+5: a real install copies the binaries and compiles nothing -------------
# The binaries the tier already built are reused; DESTDIR keeps it out of the
# system. If this ever starts compiling, check 1 has been defeated some other way.
if [ -x "$ROOT/jichi" ] && [ -x "$ROOT/jichi-convert" ]; then
    iout=$(cd "$ROOT" && make install DESTDIR="$tmp/d2" $CLEAN_STAMP 2>&1)
    ncc=$(printf '%s\n' "$iout" | grep -cE '^[[:space:]]*(cc|gcc|clang|\$\(CC\))[[:space:]]')
    if [ "$ncc" -eq 0 ]; then
        t_ok "a real install runs no compiler"
    else
        t_fail "install ran the compiler $ncc time(s) -- under sudo every one of
   those outputs is written as root. Lines:
$(printf '%s\n' "$iout" | grep -E '^[[:space:]]*(cc|gcc|clang)' | head_bytes 200)"
    fi
    if [ -x "$tmp/d2/usr/local/bin/jichi" ] &&
       [ -x "$tmp/d2/usr/local/bin/jichi-convert" ]; then
        t_ok "both binaries land under DESTDIR"
    else
        t_fail "install ran but the binaries are not under DESTDIR -- the target
   refuses correctly and then installs nothing, which is worse than the defect
   it replaced. Tree: $(find "$tmp/d2" -type f 2>/dev/null | head_bytes 200)"
    fi

    # --- 6: what landed IS what was built ------------------------------------
    # Checks 4 and 5 together say "no compiler ran" and "a file appeared", which
    # a target that copied a stale binary from somewhere else would also satisfy.
    # This is the operator-facing promise the whole milestone is about: after
    # `make && sudo make install`, the jichi on PATH is the jichi just built.
    built=$("$BIN" --version 2>/dev/null | head -n 1)
    landed=$("$tmp/d2/usr/local/bin/jichi" --version 2>/dev/null | head -n 1)
    if [ -n "$built" ] && [ "$built" = "$landed" ]; then
        t_ok "the installed binary is the build in the tree ($built)"
    else
        t_fail "the installed copy does not report the tree build's version.
   tree='${built:-nothing}' installed='${landed:-nothing}'. Either install
   copied something else, or the copy does not run at all -- and the operator's
   whole reason for installing is that the PATH binary becomes the one they
   just built."
    fi
else
    t_skip_one "the tier's binaries are absent, so the install path is untestable"
    t_skip_one "the tier's binaries are absent, so the install path is untestable"
    t_skip_one "the tier's binaries are absent, so the install path is untestable"
fi


# --- 7: a binary built from a dirty tree is refused ---------------------------
# The operator-facing defect. Every step of the sequence that produces it
# succeeds -- build, gate, commit, install -- and the damage shows up later as a
# `--version` nobody can check out.
dout=$(cd "$ROOT" && make install DESTDIR="$tmp/d3" STAMP="$tmp/dirty_stamp.h" 2>&1)
drc=$?
if [ "$drc" -ne 0 ] && [ ! -e "$tmp/d3/usr/local/bin/jichi" ]; then
    t_ok "a binary stamped from a dirty tree is refused, and nothing is copied"
else
    t_fail "install accepted a '-dirty' stamp (rc=$drc, installed=$([ -e "$tmp/d3/usr/local/bin/jichi" ] && echo yes || echo no)).
   $(printf '%s' "$dout" | head_bytes 200)"
fi

# --- 8: the refusal names the way out -----------------------------------------
# A refusal a reader cannot act on sends them to `sudo make clean && sudo make`,
# which is the M586 damage this project already paid for once.
miss=
for w in "make -j4" "sudo make install" "ALLOW_DIRTY=1"; do
    printf '%s' "$dout" | grep -qF "$w" || miss="$miss '$w'"
done
if [ -z "$miss" ]; then
    t_ok "the dirty refusal names the rebuild, the install and the override"
else
    t_fail "the dirty refusal does not name:$miss -- a reader who hits this will
   improvise, and the obvious improvisation is building under sudo (M586)."
fi

# --- 9: CONTROL -- a clean stamp installs -------------------------------------
# Without this, check 7 is satisfied by a target that refuses everything.
cout=$(cd "$ROOT" && make install DESTDIR="$tmp/d4" STAMP="$tmp/clean_stamp.h" 2>&1)
crc=$?
if [ "$crc" -eq 0 ] && [ -x "$tmp/d4/usr/local/bin/jichi" ]; then
    t_ok "a binary stamped from a clean tree installs (the refusal is not blanket)"
else
    t_fail "install refused a CLEAN stamp (rc=$crc) -- the check refuses everything.
   $(printf '%s' "$cout" | head_bytes 200)"
fi

t_done
