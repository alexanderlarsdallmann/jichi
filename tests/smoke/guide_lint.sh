#!/bin/sh
# smoke: the compiled-in jichi guide -- one text, two surfaces (M1196, batch G).
#
# `jichi guide [topic]` prints a topic's sections offline; the SAME text is the
# built-in `jichi-guide` skill (load_skill). The text is generated from the
# marked page sections (`<!-- jichi-guide -->`) in docs/ by
# tests/tools/guide_gen.sh. This driver is the teeth: it regenerates to a temp
# file and DIFFS it against the committed src/guide/jc_guide.c, so a page edit
# that is not regenerated fails the build. It also holds the verb, the built-in
# skill, and the describe contract to the note's shape.
#
# Red first (the committed jc_guide.c is a stub): cells 1, 3-10 fail. Green
# after the C changes + regenerate.
#
# Cell 12 (M1196 follow-up) pins the POSITIVE direction of the option-A
# catalog guard (src/chat/jc_sysmsg.c: the 'Available skills' catalog is
# rendered only when the run advertises load_skill): a full-profile
# --dump-requests run must carry the catalog AND the tool in the SAME
# request. chatbot_example (cell 8b) pins only the deny direction. The
# hunk audit of d1ca76e5 found the positive side was verified by scratch
# dump only -- uncommitted, so a regression that hid the catalog
# unconditionally would have passed the whole suite.
. "$(dirname "$0")/_smoke.sh"

t_plan 12
smoke_home
tmp=$(smoke_tmp)

# ---------------------------------------------------------------------------
# 1. generator == committed (the teeth): regenerate to temp, diff the source.
# ---------------------------------------------------------------------------
"$SMOKE_ROOT/tests/tools/guide_gen.sh" "$SMOKE_ROOT" > "$tmp/gen.c" 2>"$tmp/gen.err"
genrc=$?
# Floor: the generated file names all six topics and all ten sections. (M1199
# added the `commands` topic with the generator row + regenerate; cell 1 stayed
# green at five topics in the RED commit, because the generator was unchanged
# then.)
floor_ok=1
if [ "$genrc" -ne 0 ] \
   || ! grep -q 'GUIDE_SETUP' "$tmp/gen.c" || ! grep -q 'GUIDE_MODEL' "$tmp/gen.c" \
   || ! grep -q 'GUIDE_LEARNER' "$tmp/gen.c" || ! grep -q 'GUIDE_AGENTS' "$tmp/gen.c" \
   || ! grep -q 'GUIDE_LIMITS' "$tmp/gen.c" || ! grep -q 'GUIDE_COMMANDS' "$tmp/gen.c" \
   || ! grep -q 'The flow' "$tmp/gen.c" \
   || ! grep -q 'Check your setup' "$tmp/gen.c" \
   || ! grep -q 'environment variables, step by step' "$tmp/gen.c" \
   || ! grep -q 'The stages' "$tmp/gen.c" \
   || ! grep -q 'Getting started' "$tmp/gen.c" \
   || ! grep -q 'A good starting point' "$tmp/gen.c" \
   || ! grep -q 'Supervising' "$tmp/gen.c" \
   || ! grep -q 'must not do' "$tmp/gen.c" \
   || ! grep -q '6. Handy commands' "$tmp/gen.c"; then
    floor_ok=0
fi
if [ "$floor_ok" -eq 1 ] \
   && diff -q "$tmp/gen.c" "$SMOKE_ROOT/src/guide/jc_guide.c" >/dev/null 2>&1; then
    t_ok "the committed guide data matches the generator (one source, no drift)"
else
    if [ "$floor_ok" -ne 1 ]; then
        t_fail "generator floor (rc=$genrc): expected all 6 topics + 10 section headings"
    else
        t_fail "src/guide/jc_guide.c differs from the generator output -- page edited but not regenerated"
    fi
fi

# ---------------------------------------------------------------------------
# 2. topic-extraction floors: exactly ten open markers across the six pages.
#    (M1199: the `commands` topic's marked section lives in TUTORIAL_BEGINNER.md
#    -- the page that documents the TUI slash commands -- so the universe grows
#    from nine to ten sections on the SAME six pages.)
# ---------------------------------------------------------------------------
nopen=$(grep -rc '<!-- jichi-guide topic=' \
    "$SMOKE_ROOT/docs/SETUP_WIZARD.md" \
    "$SMOKE_ROOT/docs/TUTORIAL_BEGINNER.md" \
    "$SMOKE_ROOT/docs/CONFIG_TUTORIAL.md" \
    "$SMOKE_ROOT/docs/CURRICULUM.md" \
    "$SMOKE_ROOT/docs/AGENTS_GUIDE.md" \
    "$SMOKE_ROOT/docs/AGENT_COLLABORATION.md" 2>/dev/null \
    | sed -n 's/^[^:]*:\([0-9]*\)$/\1/p' | awk '{ s += $1 } END { print s + 0 }')
if [ "$nopen" -eq 10 ]; then
    t_ok "ten marked sections across the six pages (the table's universe)"
else
    t_fail "expected 10 open jichi-guide markers, counted $nopen"
fi

# ---------------------------------------------------------------------------
# 3. `jichi guide` (no arg): the six topics in order, exit 0, intent each.
# ---------------------------------------------------------------------------
out=$("$BIN" guide </dev/null 2>"$tmp/e3"); rc=$?
# The model topic's content assertion (M1198, G3): `jichi guide model` must
# carry the check_model line (the sanctioned in-run live check; verdict +
# class + timings, never the model's words).
model_out=$("$BIN" guide model </dev/null 2>/dev/null)
if [ "$rc" -eq 0 ] \
   && [ "$(printf '%s\n' "$out" | grep -c '^jichi guide ')" -eq 6 ] \
   && printf '%s\n' "$out" | grep -q '^jichi guide setup' \
   && printf '%s\n' "$out" | grep -q '^jichi guide model' \
   && printf '%s\n' "$out" | grep -q '^jichi guide learner' \
   && printf '%s\n' "$out" | grep -q '^jichi guide agents' \
   && printf '%s\n' "$out" | grep -q '^jichi guide limits' \
   && printf '%s\n' "$out" | grep -q '^jichi guide commands' \
   && printf '%s\n' "$out" | grep -q ' -- ' \
   && printf '%s\n' "$model_out" | grep -q 'check_model'; then
    t_ok "jichi guide lists the six topics with intents (exit 0); the model topic carries the check_model line"
else
    t_fail "jichi guide (no arg) wrong: rc=$rc out=[$out]"
fi

# ---------------------------------------------------------------------------
# 4. `jichi guide setup`: both marked bodies, exit 0.
# ---------------------------------------------------------------------------
out=$("$BIN" guide setup </dev/null 2>"$tmp/e4"); rc=$?
if [ "$rc" -eq 0 ] \
   && printf '%s\n' "$out" | grep -q 'jichi setup' \
   && printf '%s\n' "$out" | grep -q 'Check your setup' \
   && printf '%s\n' "$out" | grep -q 'jichi doctor'; then
    t_ok "jichi guide setup carries the flow + doctor bodies"
else
    t_fail "jichi guide setup wrong: rc=$rc out=[$out]"
fi

# ---------------------------------------------------------------------------
# 5. `jichi guide limits`: all three authored rules, exit 0.
# ---------------------------------------------------------------------------
out=$("$BIN" guide limits </dev/null 2>"$tmp/e5"); rc=$?
if [ "$rc" -eq 0 ] \
   && printf '%s\n' "$out" | grep -q 'Never search for or read credentials' \
   && printf '%s\n' "$out" | grep -q 'Never read outside the project' \
   && printf '%s\n' "$out" | grep -q 'Never commit for a person unasked'; then
    t_ok "jichi guide limits carries all three authored rules"
else
    t_fail "jichi guide limits wrong: rc=$rc out=[$out]"
fi

# ---------------------------------------------------------------------------
# 6. `jichi guide bogus`: exit 2, the topic list on stdout, named refusal on
#    stderr.
# ---------------------------------------------------------------------------
out=$("$BIN" guide bogus </dev/null 2>"$tmp/e6"); rc=$?
if [ "$rc" -eq 2 ] \
   && [ "$(printf '%s\n' "$out" | grep -c '^jichi guide ')" -eq 6 ] \
   && grep -q "error: unknown guide topic 'bogus'" "$tmp/e6" \
   && grep -q 'topics: setup model learner agents limits commands' "$tmp/e6"; then
    t_ok "jichi guide bogus is a named refusal (exit 2, list on stdout)"
else
    t_fail "jichi guide bogus wrong: rc=$rc err=[$(cat "$tmp/e6" 2>/dev/null)]"
fi

# ---------------------------------------------------------------------------
# 7. `jichi skills` in an EMPTY project lists the built-in jichi-guide.
# ---------------------------------------------------------------------------
proj="$tmp/proj"
mkdir -p "$proj"
out=$(cd "$proj" && "$BIN" skills </dev/null 2>"$tmp/e7"); rc=$?
if [ "$rc" -eq 0 ] \
   && printf '%s\n' "$out" | grep -q 'jichi-guide' \
   && printf '%s\n' "$out" | grep -q 'before configuring jichi'; then
    t_ok "jichi skills lists the built-in jichi-guide in an empty project"
else
    t_fail "jichi skills (empty project) wrong: rc=$rc out=[$out]"
fi

# ---------------------------------------------------------------------------
# 8. load_skill "jichi-guide" serves the text in a headless run. The mock model
#    is scripted to call the tool, then stop; the run's jsonl tool_result line
#    carries the body's first line (a 512-byte preview, so the preamble stem).
# ---------------------------------------------------------------------------
ws="$tmp/ws8"; mkdir -p "$ws" "$tmp/cap8"
cat > "$tmp/mm8" <<'EOF'
wire openai
rule
  count 1
  tool load_skill {"name":"jichi-guide"}
rule
  text GUIDE_STOP
EOF
mm_start "$tmp/mm8" "$tmp/cap8"
write_config "$tmp/config8.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config8.json" \
    -q --no-session --auto --output jsonl \
    -p "call load_skill with name jichi-guide and stop" \
    < /dev/null > "$tmp/out8.jsonl" 2>"$tmp/e8"); rc=$?
mm_stop
tr_res=$(grep '"type":"tool_result"' "$tmp/out8.jsonl" 2>/dev/null | tr '\n' ' ')
if [ "$rc" -eq 0 ] && [ -n "$tr_res" ] \
   && printf '%s' "$tr_res" | grep -q 'jichi-guide' \
   && printf '%s' "$tr_res" | grep -q 'jichi guide' \
   && printf '%s' "$tr_res" | grep -q '"is_error":false'; then
    t_ok "load_skill jichi-guide serves the guide text in a run (jsonl tool_result)"
else
    t_fail "load_skill cell: rc=$rc tool_result=[$tr_res]"
fi

# ---------------------------------------------------------------------------
# 9. `describe --output json` names `guide` in the interface contract.
# ---------------------------------------------------------------------------
out=$("$BIN" describe --output json </dev/null 2>/dev/null)
if printf '%s\n' "$out" | grep -q '"name":"guide"'; then
    t_ok "describe names guide in the interface contract"
else
    t_fail "describe missing a guide subcommand row"
fi

# ---------------------------------------------------------------------------
# 10. the built-in is additive: a project WITH a custom skill lists BOTH.
# ---------------------------------------------------------------------------
proj10="$tmp/proj10"; mkdir -p "$proj10/.jichi/skills/foo"
cat > "$proj10/.jichi/skills/foo/SKILL.md" <<'EOF'
---
name: foo
description: a project skill
---
foo body
EOF
out=$(cd "$proj10" && "$BIN" skills </dev/null 2>"$tmp/e10")
if [ "$(printf '%s\n' "$out" | grep -c .)" -ge 2 ] \
   && printf '%s\n' "$out" | grep -q 'foo' \
   && printf '%s\n' "$out" | grep -q 'jichi-guide'; then
    t_ok "a project skill ADDS: jichi skills lists foo and jichi-guide"
else
    t_fail "additive cell wrong: out=[$out]"
fi

# ---------------------------------------------------------------------------
# 11. (M1199, G4) the `commands` topic carries the TUI's slash commands: every
#     /name `jichi guide commands` lists resolves in the SAME ground truth
#     slash_commands_lint uses (the TUI_CMDS completion table in jc_tui.c --
#     the file that lint greps). The guide is the agent's ground truth, so a
#     listed command without a handler is the M292 defect: `/learn
#     corrections` told people to run a command that never existed.
# ---------------------------------------------------------------------------
# The universe: the TUI_CMDS completion table in jc_tui.c -- the same file
# slash_commands_lint greps (the note's ground truth).
sed -n '/static const char \*TUI_CMDS\[\] = {/,/^};$/p' "$SMOKE_ROOT/src/tui/jc_tui.c" \
    | grep -oE '"/[a-z][a-z0-9-]*( [a-z0-9-]+)?"' | tr -d '"' \
    | sort -u > "$tmp/tui_cmds"
out=$("$BIN" guide commands </dev/null 2>"$tmp/e11"); rc=$?
# The body's table rows are "| `/name` | ..." -- extract the names (the second
# field is backticked; strip the backticks so a two-word `/learn` line still
# yields exactly its first word).
printf '%s\n' "$out" | awk -F'|' '/^\| `\/[a-z]/ { print $2 }' \
    | sed 's/^ *//; s/ *$//; s/^`//; s/`$//' | grep -E '^/[a-z0-9-]+' > "$tmp/guide_cmds"
ng=$(grep -c . "$tmp/guide_cmds" 2>/dev/null || true)
[ -z "$ng" ] && ng=0
bad=""
if [ "$rc" -ne 0 ]; then
    t_fail "jichi guide commands: rc=$rc (no commands topic in the binary yet) -- the marked page section is not regenerated into jc_guide.c"
elif [ "$ng" -lt 8 ]; then
    t_fail "jichi guide commands lists $ng slash commands (want >= 8)"
else
    while IFS= read -r g; do
        [ -n "$g" ] || continue
        # Resolves if a TUI_CMDS entry equals g, or g is the first word of a
        # two-word entry (so `/learn` resolves via `/learn analyze`).
        { grep -qx -- "$g" "$tmp/tui_cmds" \
           || grep -qE -- "^$g " "$tmp/tui_cmds"; } || bad="$bad $g"
    done < "$tmp/guide_cmds"
    if [ -z "$bad" ]; then
        t_ok "jichi guide commands lists $ng slash commands, every one in TUI_CMDS"
    else
        t_fail "jichi guide commands lists $ng slash commands; these have no TUI_CMDS entry:$bad"
    fi
fi

# ---------------------------------------------------------------------------
# 12. (M1196 follow-up, batch G) the POSITIVE direction of the option-A
#     catalog guard is pinned: a full-profile headless turn that advertises
#     load_skill must carry the 'Available skills' catalog in its request's
#     system message. chatbot_example 8b pins the deny direction (denied
#     load_skill -> no catalog); this pins the offer direction, in the SAME
#     request, so the catalog's presence is tied to the tool being offered --
#     both halves, or the cell is half-vacuous. --dump-requests into a
#     scratch dir (M341); the mock replies with one plain text rule, so the
#     run completes without any tool call.
# ---------------------------------------------------------------------------
ws12="$tmp/ws12"; mkdir -p "$ws12" "$tmp/cap12" "$tmp/reqs12"
cat > "$tmp/mm12" <<'EOF'
wire openai
rule
  count 1
  text GUIDE_CATALOG_OK
EOF
mm_start "$tmp/mm12" "$tmp/cap12" 1
write_config "$tmp/config12.json" "$MM_PORT"
(cd "$ws12" && with_deadline 60 "$BIN" --config "$tmp/config12.json" \
    -q --no-session --auto --output jsonl \
    --dump-requests "$tmp/reqs12" \
    -p "say hello and stop" \
    < /dev/null > "$tmp/out12.jsonl" 2>"$tmp/e12"); rc=$?
mm_stop
d12="$tmp/reqs12/req-0001.json"
if [ "$rc" -ne 0 ] || [ ! -s "$d12" ]; then
    t_fail "catalog-positive cell: the full-profile run did not complete (rc=$rc) or dumped no request ($(ls "$tmp/reqs12" 2>/dev/null | tr '\n' ' '))"
elif [ "$(grep -c 'Available skills' "$d12")" -ne 1 ] \
   || [ "$(grep -c 'jichi-guide' "$d12")" -ne 1 ] \
   || [ "$(grep -c '"name":"load_skill"' "$d12")" -ne 1 ]; then
    t_fail "full-profile request: Available skills=$(grep -c 'Available skills' "$d12") jichi-guide=$(grep -c 'jichi-guide' "$d12") load_skill-tool=$(grep -c '"name":"load_skill"' "$d12") (want exactly 1 each -- the catalog and its offer tool must ride in the same request)"
else
    t_ok "a full-profile run that advertises load_skill carries the Available skills catalog (the positive direction of the option-A guard)"
fi

t_done
