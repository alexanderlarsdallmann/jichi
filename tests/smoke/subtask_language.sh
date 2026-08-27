#!/bin/sh
# smoke: the answer-language directive reaches a `subtask: true` command, and a
# command may pin its own with `language:` frontmatter (M597).
#
# THE DEFECT THIS EXISTS FOR. LANGUAGE.md said two things eleven lines apart:
# "Top-level only -- subagents keep their focused-task prompt" and "learning
# surfaces inherit it for free: ... the mentor loop". The code agreed with the
# first: jc_sysmsg_append_language had one call site, in the top-level builder,
# justified by "a subagent's answer is consumed by the main agent, which follows
# the directive itself". A command subtask has no main agent downstream -- its
# answer streams to the user, or is write_file'd to disk -- so the mentor of a
# German self-learner drafted English lessons, and nothing told them why.
#
# The operator's decision (2026-08-27): lessons are stored in the user's
# language by default; English-canonical is an OPTION -- one line of frontmatter
# (`language: English`) on the command, not a config key, so the choice lives
# beside the mentor it governs.
#
# Same flow as subtask_persona.sh; the mock routes on the user message, so the
# system prompt is free to vary and is exactly what these checks read.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
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

# --- run 1: the session language follows the command subtask ------------------
mm_start "$tmp/replies.mm" "$tmp/cap1"
write_config "$tmp/config.json" "$MM_PORT" '"learnOnStop":true,"language":"Deutsch"'
(cd "$ws" && "$BIN" --config "$tmp/config.json" init \
    < /dev/null > /dev/null 2>&1)
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto -p "do the task" \
    < /dev/null > /dev/null 2>"$tmp/err1"); rc=$?
mm_stop

mreq=""
for f in "$tmp"/cap1/req.*; do
    [ -f "$f" ] || continue
    if grep -q "lessons.draft.md" "$f"; then mreq="$f"; break; fi
done
if [ $rc -eq 0 ] && [ -n "$mreq" ]; then
    t_ok "run 1 completed and the mentor's request was captured"
else
    t_fail "run 1 rc=$rc, mentor request: '${mreq:-none}' -- $(head_bytes 200 "$tmp/err1")"
fi

if [ -n "$mreq" ] && grep -q "Respond in Deutsch" "$mreq"; then
    t_ok "the session language reaches the mentor's request"
else
    t_fail "the mentor was not told to answer in Deutsch -- the directive stayed top-level only"
fi

# --- run 2: `language:` frontmatter on the command pins its own ---------------
# The English-canonical option: the mentor writes English lessons while the
# session still answers the user in Deutsch. Written into the scaffolded
# learn.md exactly as a user would.
lm="$ws/.jichi/commands/learn.md"
if [ -f "$lm" ]; then
    # Frontmatter order is free, so the key goes right after the opening `---`
    # (POSIX awk; a `\n` in a sed replacement is a GNU extension -- M471).
    awk 'NR == 1 { print; print "language: English"; next } { print }' "$lm" \
        > "$lm.new" && mv "$lm.new" "$lm"
fi
mm_start "$tmp/replies.mm" "$tmp/cap2"
write_config "$tmp/config.json" "$MM_PORT" '"learnOnStop":true,"language":"Deutsch"'
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto -p "do the task" \
    < /dev/null > /dev/null 2>"$tmp/err2"); rc=$?
mm_stop

mreq=""
for f in "$tmp"/cap2/req.*; do
    [ -f "$f" ] || continue
    if grep -q "lessons.draft.md" "$f"; then mreq="$f"; break; fi
done
if [ -n "$mreq" ] && grep -q "language: English" "$lm" && \
   grep -q "Respond in English" "$mreq"; then
    t_ok "a command's \`language:\` pins the subtask's answer language (English-canonical lessons)"
else
    t_fail "the frontmatter language did not reach the mentor (rc=$rc, req='${mreq:-none}')"
fi

if [ -n "$mreq" ] && grep -q "Respond in English" "$mreq" && \
   ! grep -q "Respond in Deutsch" "$mreq"; then
    t_ok "the pinned language REPLACES the session language rather than joining it"
else
    t_fail "both directives reached the mentor -- two 'Respond in' lines contradict each other"
fi

t_done
