#!/bin/sh
# smoke: a progress write that fails is REPORTED, and the grade verdict is
# unchanged by it (M642).
#
# WHY. `grade --record` appends one JSONL line to .jichi/progress.jsonl, the
# learner's own record. The three appenders in jc_progress.c ignored the return
# values of fprintf AND fclose, so a full disk lost the line while the function
# answered JC_OK and the caller printed nothing. A first-seat review in the
# refute A/B claimed exactly this at three lines; the baseline refuter dismissed
# it ("fclose will report it" -- fclose's return was unchecked too); the person
# grading the refuters asked for a test. This is it. /dev/full fails every
# write with ENOSPC, so a progress file that is a symlink to it is that disk.
#
# The grade's exit code must be the SAME in both workspaces: a record that
# could not be kept is a warning on stderr, never a change of verdict.
. "$(dirname "$0")/_smoke.sh"

# THE SKIP TEST COMES BEFORE THE PLAN (M672). `t_plan 4` then `t_skip` emits
# TWO TAP plan lines -- `1..4` followed by `1..0` -- and a runner reading that
# sees malformed output and calls the driver FAILED even though it exited 0.
# OpenBSD has no /dev/full, so it took that path and was the last red driver on
# an otherwise complete row: not a defect, a driver announcing its own skip in a
# way the harness could not parse. Decide whether the platform can run the test
# first, then declare how many checks there will be.
smoke_home
tmp=$(smoke_tmp)
spec=docs/assignments/01-find-the-setting.md
mkws() {   # mkws DIR -> a workspace holding only task 01
    mkdir -p "$1/docs/assignments" "$1/.jichi"
    cp "$SMOKE_ROOT/$spec" "$1/docs/assignments/"
    cp -r "$SMOKE_ROOT/docs/assignments/01-find-the-setting" "$1/docs/assignments/"
}
okws=$(smoke_tmp); mkws "$okws"
fullws=$(smoke_tmp); mkws "$fullws"
if ! ln -s /dev/full "$fullws/.jichi/progress.jsonl" 2>/dev/null || [ ! -c /dev/full ]; then
    t_skip "no /dev/full or no symlinks here"
fi

t_plan 4

(cd "$okws" && with_deadline 60 "$BIN" grade "$spec" --record < /dev/null \
    > "$tmp/ok.out" 2> "$tmp/ok.err"); okrc=$?
(cd "$fullws" && with_deadline 60 "$BIN" grade "$spec" --record < /dev/null \
    > "$tmp/full.out" 2> "$tmp/full.err"); fullrc=$?

# --- 1: control -- the record landed -----------------------------------------------
if [ "$(grep -c . "$okws/.jichi/progress.jsonl" 2>/dev/null)" = "1" ]; then
    t_ok "control: one progress line was recorded"
else
    t_fail "control recorded $(grep -c . "$okws/.jichi/progress.jsonl" 2>/dev/null) lines (rc=$okrc): $(head_bytes 160 "$tmp/ok.err")"
fi
# --- 2: the failed write is reported --------------------------------------------------
if grep -q "could not append" "$tmp/full.err"; then
    t_ok "a write to a full disk is reported: 'could not append' on stderr"
else
    t_fail "the lost record went unreported (rc=$fullrc): stderr=$(head_bytes 160 "$tmp/full.err")"
fi
# --- 3: the verdict is not changed by the record failing ------------------------------
if [ "$okrc" = "$fullrc" ]; then
    t_ok "the grade's exit code is the same with and without the record ($okrc)"
else
    t_fail "exit codes differ: control $okrc, full disk $fullrc -- a lost record changed the verdict"
fi
# --- 4: nothing was silently written elsewhere ----------------------------------------
if [ -L "$fullws/.jichi/progress.jsonl" ] && [ "$(ls "$fullws/.jichi" | wc -l | tr -d ' ')" = "1" ]; then
    t_ok "the full-disk workspace holds only the symlink -- no fallback file appeared"
else
    t_fail "unexpected files in the full-disk workspace: $(ls "$fullws/.jichi" | tr '\n' ' ')"
fi
t_done
