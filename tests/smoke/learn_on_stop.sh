#!/bin/sh
# smoke: learn-on-stop (M71) -- after a COMPLETED --auto run, the mentor
# auto-runs and drafts .jichi/lessons.draft.md. The mock routes by
# content: the main turn gets a plain answer; the mentor turn (its prompt
# names lessons.draft.md) gets a write_file call creating the draft.
# (Port of tests/e2e/learn_on_stop.py, M211.)
#
# M598: learn-on-stop now PARSES the draft it just produced and says what
# `learn apply` would commit from it. A draft that would apply nothing -- prose
# under invented headings, the shape a mentor that never received its format
# block produced for weeks (M596) -- used to be indistinguishable from a good
# one until someone ran apply, weeks later. Check 4 pins the count line on a
# parseable draft; check 5 pins the WARN on a prose-only one. The run is no
# longer -q, because the count line is the subject.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "lessons.draft.md"
  match "\"role\":\"tool\""
  text drafted
rule
  match "lessons.draft.md"
  tool write_file {"path":".jichi/lessons.draft.md","content":"## Memory notes\n- learned X from the run\n"}
rule
  text DONE
EOF

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT" '"learnOnStop":true'

# scaffold the mentor agent + learn command into the workspace
(cd "$ws" && "$BIN" --config "$tmp/config.json" init \
    < /dev/null > /dev/null 2>&1)
if [ -f "$ws/.jichi/commands/learn.md" ]; then
    t_ok "init scaffolded the learn command"
else
    t_fail "no learn command scaffolded"
fi

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --no-session --auto -p "do the task" \
    < /dev/null > /dev/null 2>"$tmp/err"); rc=$?
mm_stop

if [ $rc -eq 0 ]; then
    t_ok "the --auto run completed (rc=0)"
else
    t_fail "run rc=$rc: $(tail -c 200 "$tmp/err")"
fi

if grep -q "learned X from the run" "$ws/.jichi/lessons.draft.md" 2>/dev/null
then
    t_ok "the mentor wrote lessons.draft.md after the run"
else
    t_fail "no draft (or wrong content) after the --auto run"
fi

# 4 (M598): the run reports what the draft would commit -- one memory note.
if grep -q "draft parsed" "$tmp/err" && grep -q "1 memory note(s)" "$tmp/err"; then
    t_ok "learn-on-stop reports the draft's parse counts (1 memory note)"
else
    t_fail "no parse-count line after the mentor ran: $(tail -c 300 "$tmp/err")"
fi

# 5 (M598): a draft `learn apply` would commit NOTHING from is named as such, at
# WARN, while the run is still on the operator's screen. A fresh workspace, and
# a mock whose mentor turn writes prose under its own headings -- the zigodot
# shape the 2026-08-27 analysis measured (0 directives, no parser heading).
ws2=$(smoke_tmp)
rm -f "$tmp/.port"
cat > "$tmp/replies2.mm" <<'EOF'
wire openai
rule
  match "lessons.draft.md"
  match "\"role\":\"tool\""
  text drafted
rule
  match "lessons.draft.md"
  tool write_file {"path":".jichi/lessons.draft.md","content":"## Tokenizer loops\nThe tokenizer was edited many times.\n"}
rule
  text DONE
EOF
mm_start "$tmp/replies2.mm" "$tmp/cap2"
write_config "$tmp/config2.json" "$MM_PORT" '"learnOnStop":true'
(cd "$ws2" && "$BIN" --config "$tmp/config2.json" init \
    < /dev/null > /dev/null 2>&1)
(cd "$ws2" && with_deadline 60 "$BIN" --config "$tmp/config2.json" \
    --no-session --auto -p "do the task" \
    < /dev/null > /dev/null 2>"$tmp/err2"); rc2=$?
mm_stop
if [ $rc2 -eq 0 ] && grep -q "would commit nothing" "$tmp/err2" && \
   grep -q "## Memory notes" "$tmp/err2"; then
    t_ok "a prose-only draft is flagged as unappliable, naming the headings apply parses"
else
    t_fail "no unappliable-draft warning (rc=$rc2): $(tail -c 300 "$tmp/err2")"
fi

t_done
