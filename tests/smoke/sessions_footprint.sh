#!/bin/sh
# smoke: repeated /sessions must not grow the session arena (M197). The
# unit test covers jc_session_list itself; it cannot see a CALL SITE
# handing it app->arena -- where the bug lived (/sessions, /resume, and
# every Tab press retained the whole store for the process's life). This
# PTY-drives the real TUI against a synthesized store and reads the
# binary's own /context "Arenas: session N KB" gauge before and after 10x
# /sessions, asserting the growth stays small.
# (Port of tests/e2e/sessions_footprint.py, M217.)
. "$(dirname "$0")/_smoke.sh"

t_plan 2
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)
# 16 sessions x ~32 KB = ~512 KB store. Pre-fix, 10x /sessions retained
# ~5 MB; the fix retains nothing, so a 128 KB bar is a ~40x margin either way.
FILES=16
LIMIT_KB=128

sdir="$HOME/.jichi.d/sessions"
mkdir -p "$sdir"
pad=$(printf '%*s' 32000 '' | tr ' ' a)
i=0
while [ $i -lt $FILES ]; do
    sid=$(printf '00000000-0000-4000-8000-%012d' $i)
    if [ $i -eq 0 ]; then wsd="$ws"; else wsd="/nonexistent/ws-$i"; fi
    printf '{"sessionId":"%s","title":"synth %d","workspaceDirectory":"%s","mode":"chat","history":[{"role":"user","content":"q"},{"role":"assistant","content":"%s"}]}' \
        "$sid" "$i" "$wsd" "$pad" > "$sdir/$sid.json"
    i=$((i + 1))
done

# \x15 = Ctrl-U (a stray partial line must not become part of the command)
{
    echo 'expect "] " 15'
    echo 'send "\x15/context\r"'
    echo 'expect "Arenas: session" 10'
    echo 'delay 300'
    i=0
    while [ $i -lt 10 ]; do
        echo 'send "\x15/sessions\r"'
        echo 'expect "synth" 10'
        echo 'delay 100'
        i=$((i + 1))
    done
    echo 'send "\x15/context\r"'
    echo 'expect "Arenas: session" 10'
    echo 'delay 300'
    echo 'send "\x15/exit\r"'
    echo 'waitexit 10'
} > "$tmp/fp.pd"

(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 60 --cols 100 \
    --log "$tmp/fp.log" "$tmp/fp.pd" -- "$BIN" --no-route --no-lite); rc=$?
if [ $rc -eq 0 ]; then
    t_ok "the TUI served /context and 10x /sessions over the store"
else
    t_fail "ptydrive script rc=$rc"
fi

# Parse the two gauge readings from the transcript (strip ANSI first).
# The literal ESC-bracket is a class [\[] rather than a bare backslash-
# bracket, so the pattern has no doubled-bracket -- smoke_lint's bashism
# grep is deliberately blunt and would flag one anywhere in the file.
#
# ONE PASS, AND NO NULLABLE `grep -o` PATTERN (M481). This was two greps ending
# in `grep -o '[0-9]*'`, which is a -o pattern that can match the EMPTY string.
# GNU grep skips empty matches and prints 123; OpenBSD's grep 0.9 prints NOTHING
# and exits 0 -- so this driver failed on that row for months with before='' and
# after='', and the empty output was indistinguishable from jichi never printing
# the gauge. It was diagnosed by comparing against NetBSD, which passes because
# it is a BSD that ships GNU grep. posix_utils_lint checks 15-16 now ban the
# construct tier-wide.
smoke_plain "$tmp/fp.log" \
    | sed -n 's/.*Arenas: session \([0-9][0-9]*\) KB.*/\1/p' > "$tmp/kb"
before=$(sed -n '1p' "$tmp/kb")
after=$(tail -1 "$tmp/kb")
if [ -n "$before" ] && [ -n "$after" ]; then
    grew=$((after - before))
    if [ "$grew" -le "$LIMIT_KB" ]; then
        t_ok "10x /sessions grew the session arena by ${grew} KB (<= $LIMIT_KB)"
    else
        t_fail "arena grew ${grew} KB (limit $LIMIT_KB) -- a call site is retaining store text (M197)"
    fi
else
    t_fail "could not read the /context arena gauge (before='$before' after='$after')"
fi

t_done
