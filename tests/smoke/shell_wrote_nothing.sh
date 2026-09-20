#!/bin/sh
# smoke: a shell command that changed nothing is reported as such (M689).
#
# WHAT WENT WRONG, driving jichi on a second project. The envelope's line
#
#     not checked: a shell command ran -- changes it made are not attributed
#                  to the run
#
# fired identically on two runs: one whose FOURTEEN shell calls were `grep -rn`,
# `find` and `ls -la`, and one whose single call was `zig fmt <file>`, which
# rewrites the file. The second is correct and important. The first is a false
# alarm about `ls` -- and a warning that fires on `ls` is one readers learn to
# skip, attached to the sentence that should never be skipped.
#
# ONLY THE NEGATIVE IS SOUND, and that is the whole design. jichi already takes
# a turn-end diff against the run-start snapshot baseline (the out-of-scope
# sweep). If that diff is EMPTY, the shell provably wrote nothing. If it is not
# empty, it proves nothing about the shell -- the run's own file tools change
# the tree too -- so the warning is left exactly as it was. Check 2 holds the
# half that is provable; check 3 holds the half that must NOT be guessed.
#
# THE FOURTH CHECK IS A DIFFERENT SEAM, here because it shares a fixture: M689
# also taught `doctor` to ask whether the server LISTS the configured model, not
# just whether the base URL answers. That probe must FAIL OPEN -- a server whose
# /v1/models cannot be read is not a bad config, and mockmodel answering a chat
# reply to a GET is exactly that server.
. "$(dirname "$0")/_smoke.sh"

command -v git > /dev/null 2>&1 || t_skip "git is required for the snapshot baseline"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
G=/usr/bin/grep
[ -x "$G" ] || G=grep

mkdir -p "$ws/src"
printf 'int main(void){return 0;}\n' > "$ws/src/a.c"
(cd "$ws" && git init -q . && git config user.email t@example.com &&
 git config user.name t && git add -A && git commit -qm base) > /dev/null 2>&1

cfg() {   # cfg <path> <port>
    cat > "$1" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$2/v1","apiKey":"x","roles":["chat"]}],
"repoMap":false,"references":false,"toolProfile":"full","lowResource":false,
"snapshots":true,"maxRetries":0}
EOF
}

# ---- run A: the shell only LOOKS -------------------------------------------
cat > "$tmp/read.mm" <<'EOF'
wire openai
rule
  match "\"messages\""
  nomatch "\"role\":\"tool\""
  tool run_terminal_command {"command":"ls -1 src"}
rule
  text LOOKED
EOF
mm_start "$tmp/read.mm" "$tmp/capA"
cfg "$tmp/a.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/a.json" --no-session \
    --edit-scope 'src/**' -p "look around" < /dev/null > "$tmp/a.out" 2> "$tmp/a.err")
mm_stop

# ---- 1: the denominator -- a shell command really ran ----------------------
# Without this, checks 2 and 3 pass on a run that never shelled out at all,
# which is the state a broken fixture produces and the one they cannot see.
if $G -q 'run_terminal_command' "$tmp/a.err"; then
    t_ok "the fixture really ran a shell command (the case is live)"
else
    t_fail "no shell tool call in run A, so the assertions below are vacuous: \
$(head_bytes 220 "$tmp/a.err")"
fi

# ---- 2: it is reported as having changed nothing ---------------------------
if $G -q 'a shell command ran and changed nothing' "$tmp/a.err" &&
   ! $G -q 'changes it made are not attributed' "$tmp/a.err"; then
    t_ok "a read-only shell command is reported as having changed nothing"
else
    t_fail "an \`ls\` still produces the unattributed-changes warning -- which \
is the line that fired on fourteen greps and on one \`zig fmt\` alike: \
$($G -A1 'checked:' "$tmp/a.err" | head -2 | tr '\n' ' ' | head_bytes 260)"
fi

# ---- run B: the shell WRITES ------------------------------------------------
cat > "$tmp/write.mm" <<'EOF'
wire openai
rule
  match "\"messages\""
  nomatch "\"role\":\"tool\""
  tool run_terminal_command {"command":"printf x > src/b.txt"}
rule
  text WROTE
EOF
mm_start "$tmp/write.mm" "$tmp/capB"
cfg "$tmp/b.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/b.json" --no-session \
    --edit-scope 'src/**' -p "write something" < /dev/null > "$tmp/b.out" 2> "$tmp/b.err")
mm_stop

# ---- 3: and THEN the warning must still fire (the two-sided half) -----------
# A build that simply deleted the warning passes check 2 and fails here. This is
# the case the warning exists for, and the reason the suppression is keyed on a
# PROOF that nothing changed rather than on "the command looked harmless".
if $G -q 'changes it made are not attributed' "$tmp/b.err" &&
   ! $G -q 'changed nothing' "$tmp/b.err"; then
    t_ok "a shell command that wrote is still reported as unattributed"
else
    t_fail "a shell command that rewrote the tree was reported as harmless -- \
the suppression must rest on an empty diff, never on the command text: \
$($G -A1 'checked:' "$tmp/b.err" | head -2 | tr '\n' ' ' | head_bytes 260)"
fi

# ---- 4: doctor's model-listing probe FAILS OPEN ----------------------------
# M689 also taught `doctor` to ask whether the server LISTS the configured
# model, because `✓ model server reachable` is a fact about the base URL and a
# committed config in the wild named two ids the gateway had retired. That probe
# must be silent when it cannot tell: mockmodel does not route by path, so a GET
# to /v1/models gets a chat reply, which is precisely a server whose listing
# cannot be read. Reporting "not listed" there would call every non-OpenAI
# server a broken config -- the failure mode that teaches people to ignore rows.
mm_start "$tmp/read.mm" "$tmp/capC"
cfg "$tmp/c.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c.json" doctor \
    < /dev/null > "$tmp/c.out" 2> "$tmp/c.err")
mm_stop
if $G -q 'model server reachable' "$tmp/c.out" &&
   ! $G -q 'not listed by its server' "$tmp/c.out"; then
    t_ok "an unreadable /v1/models is silent, not a finding (the probe fails open)"
else
    t_fail "doctor called a server a bad config because it could not read its \
model listing -- only a listing that WAS obtained and lacks the id is a \
finding: $($G -iE 'listed|reachable' "$tmp/c.out" | head -3 | tr '\n' ' ' | head_bytes 240)"
fi

t_done
