#!/bin/sh
# smoke: an explicit --readonly survives every later mode change (M840).
#
# THE DEFECT. jc_app_set_mode() set `readonly = 0` for chat and auto, and two
# paths call it after --readonly was applied:
#   * a bounded headless run (--deadline, --budget-tokens, --max-tool-calls ...)
#     is promoted to AUTO "to act without prompts", unless --plan or --auto
#     pinned the mode;
#   * a resumed session re-applies the mode it was saved in.
# So `jichi --readonly -p ... --deadline 60s` WROTE FILES, and under -q it did not
# even print its "auto-approving" notice. Found while writing the chatbot guide
# (M841): a scripted write_file landed in the knowledge folder of a configuration
# about to be documented as read-only.
#
# THE FIX has two parts, and each check below holds one of them:
#   * no promotion of a --readonly run -- it has nothing to approve (check 1: the
#     run stays in chat mode, and says nothing about auto-approving);
#   * readonly_pinned -- no mode change clears readonly while --readonly was given
#     (check 2). The case that needs it is resuming a session SAVED IN AUTO MODE:
#     a resume re-applies the saved mode. A session saved in chat mode would not
#     show it, because a headless chat run refuses a write anyway -- it needs an
#     approval nobody is there to give -- which is why check 2 resumes the session
#     of check 3's run, a bounded run that WAS promoted.
# Check 3 is the CONTROL: the same bounded run without --readonly IS promoted and
# its write lands, so checks 1 and 2 are not refusing for some other reason.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<MM
wire openai
rule
  count 1
  tool write_file {"path":"one.txt","content":"one"}
rule
  count 2
  text RUN1_DONE
rule
  count 3
  tool write_file {"path":"three.txt","content":"three"}
rule
  count 4
  text RUN3_DONE
rule
  count 5
  tool write_file {"path":"two.txt","content":"two"}
rule
  count 6
  text RUN2_DONE
MM
mm_start "$tmp/replies.mm" "$tmp/cap" 6
write_config "$tmp/config.json" "$MM_PORT"
bounded() { (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --deadline 60s --max-tool-calls 5 --output jsonl "$@" < /dev/null); }

bounded --readonly --no-session -p "write one" > "$tmp/r1.jsonl" 2> "$tmp/r1.err"
bounded -p "write three" > "$tmp/r3.jsonl" 2> "$tmp/r3.err"
sid=$(sed -n 's/.*"session_id":"\([^"]*\)".*/\1/p' "$tmp/r3.jsonl" | head -n 1)
bounded --readonly --session "$sid" -p "write two" > "$tmp/r2.jsonl" 2> "$tmp/r2.err"
mm_stop

# 1: a bounded --readonly run is not promoted, and its write is refused
if [ ! -e "$ws/one.txt" ] && grep -q '"type":"message_start"[^}]*"mode":"chat"' "$tmp/r1.jsonl" &&
   ! grep -q 'auto-approving' "$tmp/r1.err"; then
    t_ok "--readonly with --deadline: not promoted to auto (mode chat), and write_file is refused"
else
    t_fail "--readonly run: one.txt $( [ -e "$ws/one.txt" ] && echo WRITTEN || echo absent), mode $(grep -o '"mode":"[a-z]*"' "$tmp/r1.jsonl" | head -n 1), notice $(grep -c 'auto-approving' "$tmp/r1.err")"
fi
# 2: resuming the session does not clear --readonly
if [ -n "$sid" ] && [ ! -e "$ws/two.txt" ] && grep -q RUN2_DONE "$tmp/r2.jsonl"; then
    t_ok "--readonly survives resuming a session saved in auto mode: write_file is refused"
else
    t_fail "resumed --readonly run: session '$sid', two.txt $( [ -e "$ws/two.txt" ] && echo WRITTEN || echo absent)"
fi
# 3: CONTROL -- without --readonly the same bounded run is promoted and writes
if [ -e "$ws/three.txt" ] && grep -q '"type":"message_start"[^}]*"mode":"auto"' "$tmp/r3.jsonl"; then
    t_ok "control: without --readonly the bounded run is promoted to auto and its write lands"
else
    t_fail "control: three.txt $( [ -e "$ws/three.txt" ] && echo written || echo ABSENT), mode $(grep -o '"mode":"[a-z]*"' "$tmp/r3.jsonl" | head -n 1)"
fi
t_done
