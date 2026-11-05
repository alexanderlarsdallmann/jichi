#!/bin/sh
# smoke: a command's `agent:` persona reaches the model on the `subtask: true`
# path (M596).
#
# THE DEFECT THIS EXISTS FOR. The scaffolded /learn command is `agent: mentor` +
# `subtask: true`. jc_app_command_agent_apply set app->persona_override to the
# mentor's whole prompt -- including the "FORMAT IS STRICT" block naming the five
# headings jc_learn_parse_draft needs -- and jc_agent_run_command_subtask then
# built its system message with jc_sysmsg_build_sub, which never read it. Since
# M28 (2026-06-24) every /learn and every learn-on-stop ran the mentor under the
# generic "You are a focused sub-agent" prompt, told nothing about the format;
# the M70 mentor never once received its instructions. Reproduced 2026-08-27 by
# reading the captured request: 0 bytes of mentor.md in it.
#
# WHY learn_on_stop.sh COULD NOT SEE IT. Its mock routes on the USER message
# ("lessons.draft.md"), so the draft appears whatever the system prompt says.
# The ground truth for "what the mentor was told" is the request body mockmodel
# captures, which is what this driver reads (the M429/M436 distinction).
#
# Check 5 pins the OTHER half of the fix: a persona REPLACES the generic identity
# paragraph but the sections jichi enforces mechanically (the untrusted-content
# rule, constraints, the edit scope) are still appended -- M434's
# "enforced implies stated" now holds for a persona'd subtask too.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# Same script shape as learn_on_stop.sh: the main turn answers DONE; the mentor
# turn (its user prompt names lessons.draft.md) answers with the draft, which
# /learn's `output:` saves -- the read-only mentor's protocol since M848.
cat > "$tmp/draft.sse" <<'EOF'
data: {"choices":[{"index":0,"delta":{"role":"assistant","content":"## Memory notes\n- learned X from the run\n"},"finish_reason":null}]}

data: {"choices":[{"index":0,"delta":{},"finish_reason":"stop"}]}

data: [DONE]

EOF
cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  match "lessons.draft.md"
  sse-file $tmp/draft.sse
rule
  text DONE
EOF

mm_start "$tmp/replies.mm" "$tmp/cap"
write_config "$tmp/config.json" "$MM_PORT" '"learnOnStop":true'

(cd "$ws" && "$BIN" --config "$tmp/config.json" init \
    < /dev/null > /dev/null 2>&1)
if [ -f "$ws/.jichi/agents/mentor.md" ] && \
   grep -q "FORMAT IS STRICT" "$ws/.jichi/agents/mentor.md"; then
    t_ok "init scaffolded mentor.md, and it carries the format block"
else
    t_fail "no mentor.md with a FORMAT IS STRICT block after init"
fi

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto -p "do the task" \
    < /dev/null > /dev/null 2>"$tmp/err"); rc=$?
mm_stop

if [ $rc -eq 0 ]; then
    t_ok "the --auto run completed (rc=0)"
else
    t_fail "run rc=$rc: $(head_bytes 300 "$tmp/err")"
fi

# The mentor's request is the one whose body names the draft file.
mreq=""
for f in "$tmp"/cap/req.*; do
    [ -f "$f" ] || continue
    if grep -q "lessons.draft.md" "$f"; then mreq="$f"; break; fi
done
if [ -n "$mreq" ]; then
    t_ok "captured the mentor's request ($(basename "$mreq"))"
else
    t_fail "no request naming lessons.draft.md among $(ls "$tmp"/cap/req.* 2>/dev/null | wc -l) captured"
fi

# 4: the persona -- the mentor's own instructions -- is in ITS request.
if [ -n "$mreq" ] && grep -q "FORMAT IS STRICT" "$mreq"; then
    t_ok "mentor.md's instructions reach the mentor's request"
else
    t_fail "mentor.md's instructions are ABSENT from the mentor's request -- the subtask ran under the generic sub-agent prompt"
fi

# 5: and the enforced sections still travel with it (M434, now for a persona too).
if [ -n "$mreq" ] && grep -q "UNTRUSTED" "$mreq"; then
    t_ok "the untrusted-content rule is still stated beside the persona"
else
    t_fail "the persona replaced the enforced sections too -- the untrusted-content rule is missing from the mentor's request"
fi

# 6 (M603): the project's vocabulary reaches the mentor. `init` ships the starter
# glossary of jichi's own terms with the default pack, and learn.md inlines
# `.jichi/glossary.md` when it exists -- through a shell block, so a project
# without one gets nothing rather than a "[could not read]" note the model might
# echo into a lesson. The mentor writes in the words the notes and the analyze
# report use, instead of paying the private-vocabulary tax blind.
#
# M994 (D-32) collateral: learn.md's glossary shell block is refused in an
# untrusted project, so the first (untrusted) run's mentor request carries no
# glossary. Trust the project -- a minimal local/config.json (same non-model
# keys as the driver's global config, no models) plus `jichi trust` -- then
# re-run. The model comes from the GLOBAL config ($HOME/.jichi); the re-run
# uses discovery, not --config: an explicit --config/$JC_CONFIG load skips the
# project merge and its trust gate entirely (project_trust stays NONE), so the
# shell block would still be refused.
mm_start "$tmp/replies.mm" "$tmp/cap2"
write_config "$HOME/.jichi" "$MM_PORT" '"learnOnStop":true'
mkdir -p "$ws/local"
cat > "$ws/local/config.json" <<EOF
{"lowResource":false,"snapshots":false,"repoMap":false,"references":false,"toolProfile":false,"maxRetries":0}
EOF
unset JC_CONFIG
(cd "$ws" && with_deadline 60 "$BIN" trust </dev/null >/dev/null 2>&1)
(cd "$ws" && with_deadline 60 "$BIN" \
    -q --no-session --auto -p "do the task" \
    < /dev/null > /dev/null 2>"$tmp/err2"); rc=$?
mm_stop
mreq_trusted=""
for f in "$tmp/cap2"/req.*; do
    [ -f "$f" ] || continue
    if grep -q "lessons.draft.md" "$f"; then mreq_trusted="$f"; break; fi
done
if [ $rc -eq 0 ] && [ -n "$mreq_trusted" ] && grep -q "Glossary - jichi" "$mreq_trusted"; then
    t_ok "the starter glossary reaches the mentor's request"
else
    t_fail "no glossary in the mentor's request (rc=$rc) -- the mentor reads lessons in words it was never given"
fi

t_done
