#!/bin/sh
# smoke lint: the extracted driven task issues the SAME commands the three rigs
# issued before it existed (M676).
#
# Named *_lint.sh because it compiles nothing and runs no jichi -- it sources
# the helper with recording stand-ins and reads what it TRIED to do. The first
# name had no suffix and smoke_lint check 5 refused it, correctly: the rule is
# that a non-lint driver must run "$BIN", and a driver that runs no binary at
# all is a lint. Renamed rather than exempted.
#
# WHY THIS AND NOT A LIVE RUN. The claim being tested is not "the driven task
# works" -- three rows are already Driven, so it does. The claim is that
# EXTRACTING it into scripts/_rig_live.sh preserved it exactly, and the risk in
# a pure extraction is textual divergence, not behaviour. A live run needs a VM,
# a loaded model and four minutes, proves the commands work rather than that
# they are unchanged, and cannot run in this tier at all. A golden-command
# comparison runs everywhere, in milliseconds, forever.
#
# HOW: the helper's whole contract is that the caller supplies g/gl/ok/bad/note.
# So this driver supplies RECORDING ones, calls jc_rig_live, and reads what it
# tried to do. No VM, no ssh, no model, no network.
#
# THE CHECK THAT MATTERS MOST is the last one: the nonce written into the
# fixture and the nonce the assertion greps for must be the SAME string. If a
# future edit ever decouples them -- writing one phrase and grepping for
# another, or grepping for a constant -- the agentic turn would pass on any
# output at all, and every Driven verdict after that would be worthless. That
# failure is silent by construction, which is exactly what a check is for.
#
# CHECK 6 (M716): THE VOLUNTEER PAGE IS A SECOND COPY, SO IT IS PINNED HERE.
# docs/VERIFY_A_PLATFORM.md prints this task for a person to paste on their own
# machine -- the two prompts, the fixture and the keyless config -- because a
# volunteer has no rig to source. A copy is where drift starts, and a drifted copy
# would make a volunteer's "Driven" incomparable with every other row while
# looking identical. The page is compared with THIS helper's own output (its
# config and fixture functions) and with the prompt goldens checks 1 and 2 prove
# the helper sends -- never with a third copy kept in this file.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
tmp=$(smoke_tmp)

# Recording stand-ins for the rig's contract. `g` also consumes stdin, because
# that is how the helper writes the config and the fixture.
G_LOG="$tmp/g.log"; GL_LOG="$tmp/gl.log"; OK_LOG="$tmp/ok.log"
: > "$G_LOG"; : > "$GL_LOG"; : > "$OK_LOG"
RESULTS="$tmp/results.txt"; : > "$RESULTS"

# `g` ALWAYS drains stdin, and every call site below closes it with
# `< /dev/null`. The first draft drained conditionally (`[ ! -t 0 ]`) and hung:
# the helper's un-redirected calls -- `g "mkdir -p ..."` -- inherit the driver's
# stdin, which in the tier is a pipe rather than a tty, so `sed` sat waiting for
# an EOF that the runner would never send. A conditional that reads "am I being
# fed?" is really asking "is my parent a terminal?", and those are different
# questions on every machine this tier runs on.
g() {
    echo "CMD $*" >> "$G_LOG"
    sed 's/^/IN  /' >> "$G_LOG" 2>/dev/null || true
}
gl() { echo "CMD $*" >> "$GL_LOG"; return 1; }   # fail: we want the commands, not a verdict
ok()   { echo "ok   $*" >> "$OK_LOG"; }
bad()  { echo "bad  $*" >> "$OK_LOG"; }
note() { echo "note $*" >> "$OK_LOG"; }

. "$SMOKE_ROOT/scripts/_rig_live.sh"
jc_rig_live testrow 9999 test-model "$tmp" < /dev/null >/dev/null 2>&1

# --- 1: the wire turn, byte for byte as the rigs issued it -------------------
_b64_live=$(printf '%s' 'reply with OK' | base64 | tr -d '\n')
if grep -q -- "--prompt-b64 $_b64_live --output json" "$GL_LOG" \
   && grep -q -- '--config \$HOME/live.json' "$GL_LOG"; then
    t_ok "turn 1 is the unchanged wire command (--prompt-b64 'reply with OK' --output json)"
else
    t_fail "turn 1's command changed. It was, in all three rigs:
  cd \$HOME/jichi && ./jichi --config \$HOME/live.json --prompt-b64 <b64> --output json
recorded:
$(cat "$GL_LOG")"
fi

# --- 2: the agentic turn, byte for byte -------------------------------------
_b64_tool=$(printf '%s' 'Use the read_file tool to read note.txt in this directory, then report the pass phrase.' | base64 | tr -d '\n')
if grep -q -- "--auto -q --prompt-b64 $_b64_tool" "$GL_LOG"; then
    t_ok "turn 2 is the unchanged agentic command (--auto -q, the read_file prompt)"
else
    t_fail "turn 2's command changed. It was, in all three rigs:
  cd \$HOME/ws && \$HOME/jichi/jichi --config \$HOME/live.json --auto -q --prompt-b64 <b64>
recorded:
$(cat "$GL_LOG")"
fi

# --- 3: the prompts cross as base64, never as quoted strings -----------------
# Both were quoted strings first, and both lost their quotes inside the remote
# `sh -c`: jichi got `-p reply` and swallowed `--output json`. The failure is
# not a crash, it is a DIFFERENT COMMAND that still exits 0.
if ! grep -q -- "-p 'reply" "$GL_LOG" && ! grep -q -- '-p reply' "$GL_LOG" \
   && grep -qc -- '--prompt-b64' "$GL_LOG"; then
    t_ok "both prompts cross as --prompt-b64, never as a quoted string"
else
    t_fail "a prompt is being passed unquoted-through-ssh again:
$(cat "$GL_LOG")"
fi

# --- 4: the config names the tunnel, the model and nothing priced ------------
if grep -q 'IN.*"apiBase":"http://127.0.0.1:9999/v1"' "$G_LOG" \
   && grep -q 'IN.*"model":"test-model"' "$G_LOG"; then
    t_ok "the guest config points at the reverse tunnel and the named model"
else
    t_fail "the generated live config lost the tunnel URL or the model:
$(grep '^IN' "$G_LOG")"
fi

# --- 5: the fixture's nonce IS the asserted nonce ----------------------------
# The silent one. Extract the phrase the helper WROTE, then confirm the same
# string is what the agentic branch greps for -- by re-running with a gl that
# echoes the phrase, which must then be reported as a pass.
_written=$(sed -n 's/^IN  The pass phrase is \(TIER-V-[0-9A-F]*\)\./\1/p' "$G_LOG" | head -1)
: > "$OK_LOG"
gl() { echo "CMD $*" >> "$GL_LOG"; printf 'the phrase is %s\n' "$_written"; }
jc_rig_live testrow 9999 test-model "$tmp" < /dev/null >/dev/null 2>&1
_written2=$(sed -n 's/^IN  The pass phrase is \(TIER-V-[0-9A-F]*\)\./\1/p' "$G_LOG" | tail -1)
if [ -n "$_written" ] && [ "$_written" != "$_written2" ] \
   && grep -q "^ok   agentic turn" "$OK_LOG"; then
    t_fail "the agentic turn passed while the fixture held a DIFFERENT nonce
($_written written first, $_written2 written on the second call). The assertion
is not reading the phrase this run wrote, so it would pass on any output."
elif [ -n "$_written" ] && [ -n "$_written2" ] && [ "$_written" != "$_written2" ]; then
    t_ok "each run writes a fresh nonce ($_written then $_written2), and a stale one does not pass"
else
    t_fail "could not extract two distinct per-run nonces from the fixture writes.
first='$_written' second='$_written2' -- a constant phrase is a phrase a cached
answer could carry."
fi

# --- 6: the volunteer page publishes THIS task, byte for byte (M716) --------
PAGE="$SMOKE_ROOT/docs/VERIFY_A_PLATFORM.md"
_missing=""
grep -qF -- "-p 'reply with OK'" "$PAGE" 2>/dev/null || _missing="$_missing wire-prompt"
grep -qF -- "-p 'Use the read_file tool to read note.txt in this directory, then report the pass phrase.'" \
    "$PAGE" 2>/dev/null || _missing="$_missing tool-prompt"
if [ "$(jc_rig_live_fixture XYZZY)" != "The pass phrase is XYZZY." ] ||
   ! grep -qF -- "printf 'The pass phrase is %s.\n'" "$PAGE" 2>/dev/null; then
    _missing="$_missing fixture"
fi
jc_rig_live_config MODEL-ID http://127.0.0.1:1234/v1 > "$tmp/cfg" 2>/dev/null
_nl=0
while IFS= read -r _l; do
    _nl=$((_nl + 1))
    grep -qxF -- "$_l" "$PAGE" 2>/dev/null || _missing="$_missing config-line-$_nl"
done < "$tmp/cfg"
# The floor: the helper's config is three lines today. Fewer means the extraction
# read nothing, and an empty set agrees with any page.
if [ "$_nl" -ge 3 ] && [ -z "$_missing" ]; then
    t_ok "the volunteer page publishes the rig's task: both prompts, the fixture, all $_nl config lines"
else
    t_fail "docs/VERIFY_A_PLATFORM.md no longer publishes the task scripts/_rig_live.sh
defines (missing:${_missing:- none}; helper config lines read: $_nl). A volunteer
pasting it would run a different task from every other Driven row. Update the page
from the helper's output -- the helper is the definition, the page is the copy."
fi

t_done
