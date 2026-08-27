#!/bin/sh
# smoke: the binary says WHICH COMMIT it was built from, and the two surfaces agree
# (M495).
#
# THE DEFECT THIS EXISTS FOR, measured 2026-08-19. The operator ran `jichi` from
# PATH; that install was 12 days and ~50 milestones behind the tree, and BOTH
# printed `jichi 0.9.0`. The gap surfaced only as behaviour: `setup --api-base`
# answered "unknown option" for a flag its own --help documents (M488), and
# doctor's context-window check (M489) printed NOTHING AT ALL -- not a failure,
# not a "could not ask", just absence, which reads exactly like agreement. Half an
# hour of configuration debugging went by before the binary was suspected.
#
# JC_VERSION cannot close that: it moves once per release, so ~50 milestones share
# one string. The commit can, and this asserts the property that actually helps --
# the binary's stamp matches the tree it is being tested in. A stale binary run
# against a newer checkout fails here, which is the whole point.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
tmp=$(smoke_tmp)

"$BIN" --version > "$tmp/v" 2>&1 || true

# ---- 1. the version line, which must not regress ---------------------------
if grep -q '^jichi [0-9]' "$tmp/v"; then
    t_ok "--version prints the version line"
else
    t_fail "no version line: $(head_bytes 80 "$tmp/v")"
fi

# ---- 2. in a git checkout, the stamp is present ----------------------------
# Skipped rather than failed where there is no repository: a tarball build has
# nothing to stamp with, and jc_build_rev() then returns NULL BY DESIGN, so an
# absent line is correct there. The check is about a build made by this Makefile
# in this repo.
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
_head=$(cd "$ROOT" && git rev-parse --short HEAD 2>/dev/null || true)
if [ -z "$_head" ]; then
    t_skip_one "no git checkout, so there is nothing to stamp with (tarball build)"
    t_skip_one "no git checkout: cannot compare the stamp with HEAD"
    t_skip_one "no git checkout: cannot compare describe's build field"
else
    if grep -q '^build: ' "$tmp/v"; then
        t_ok "a git build stamps the commit into --version"
    else
        t_fail "no 'build:' line from a binary built in a git checkout -- a stale \
install cannot then be told from a fresh one, which is the defect this exists for"
    fi

    # ---- 3. and it is THIS tree's commit, not some other build's ------------
    _stamp=$(sed -n 's/^build: \([0-9a-f]*\).*/\1/p' "$tmp/v")
    if [ "$_stamp" = "$_head" ]; then
        t_ok "the stamp is this tree's HEAD ($_head)"
    else
        t_fail "binary was built from '$_stamp' but this tree is at '$_head' -- \
you are testing a DIFFERENT build than the source you are reading (rebuild, or on \
the operator's machine: sudo make install)"
    fi

    # ---- 4. one source of truth, two surfaces ------------------------------
    # describe's json is a stable interface a supervisor parses; it must not
    # disagree with the text a human reads.
    _dj=$("$BIN" describe --output json 2>/dev/null \
          | sed -n 's/.*"build"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')
    _vfull=$(sed -n 's/^build: //p' "$tmp/v")
    if [ -n "$_dj" ] && [ "$_dj" = "$_vfull" ]; then
        t_ok "describe --output json reports the same build as --version"
    else
        t_fail "describe says '$_dj', --version says '$_vfull' -- two surfaces, two \
answers"
    fi
fi

t_done
