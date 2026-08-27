#!/bin/sh
# smoke: the `describe` subcommand -- jichi's machine-readable interface
# contract. Offline, no config (a driving agent must introspect jichi on a
# fresh machine): the JSON carries the stable contract keys, the jsonl
# event schema + stop reasons, the daemon protocol, and per-tool readonly
# flags. (Port of tests/e2e/describe.py, M210.)
. "$(dirname "$0")/_smoke.sh"

t_plan 10
smoke_home
tmp=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
D="$tmp/describe.json"

JC_CONFIG=/nonexistent/there/config.json "$BIN" describe --output json \
    < /dev/null > "$D" 2>/dev/null; rc=$?
if [ $rc -eq 0 ] && "$JQ" -q '.' "$D"; then
    t_ok "describe --output json exits 0 with valid JSON, no config needed"
else
    t_fail "describe rc=$rc or invalid JSON"
fi

missing=""
for k in v product version output_formats exit_codes modes jsonl daemon \
         subcommands tools conditional_tools; do
    "$JQ" -q ".$k" "$D" || missing="$missing $k"
done
if [ -z "$missing" ]; then
    t_ok "contract keys all present"
else
    t_fail "missing keys:$missing"
fi

# enumerate an array's field into a file: collect_field ARRAYPATH FIELD OUT
# Iteration ends when the ELEMENT is missing; an element merely lacking the
# field is skipped (describe's daemon requests mix "type" and "name" keys).
collect_field() {
    _cf_i=0
    : > "$3"
    while [ $_cf_i -lt 64 ]; do
        "$JQ" -q "$1[$_cf_i]" "$D" 2>/dev/null || break
        _cf_v=$("$JQ" "$1[$_cf_i]$2" "$D" 2>/dev/null) \
            && printf '%s\n' "$_cf_v" >> "$3"
        _cf_i=$((_cf_i + 1))
    done
}

collect_field ".jsonl.events" ".type" "$tmp/evtypes"
missing=""
for want in message_start tool_call tool_result done; do
    grep -q "^$want\$" "$tmp/evtypes" || missing="$missing $want"
done
if [ -z "$missing" ]; then
    t_ok "jsonl event schema covers message_start/tool_call/tool_result/done"
else
    t_fail "jsonl events missing:$missing"
fi

collect_field ".jsonl.stop_reasons" "" "$tmp/reasons"
missing=""
for want in done timeout budget error; do
    grep -q "$want" "$tmp/reasons" || missing="$missing $want"
done
if [ -z "$missing" ]; then
    t_ok "stop_reasons cover done/timeout/budget/error"
else
    t_fail "stop_reasons missing:$missing"
fi

# daemon requests may key the verb as .type or .name
collect_field ".daemon.requests" ".type" "$tmp/dreq"
collect_field ".daemon.requests" ".name" "$tmp/dreq2"
cat "$tmp/dreq2" >> "$tmp/dreq"
missing=""
for want in prompt ping shutdown; do
    grep -q "^$want\$" "$tmp/dreq" || missing="$missing $want"
done
if [ -z "$missing" ]; then
    t_ok "daemon protocol covers prompt/ping/shutdown"
else
    t_fail "daemon requests missing:$missing"
fi

# read_file must be readonly:true and write_file readonly:false -- a
# driving agent reasons about side effects from these flags.
ro_read=""; ro_write=""
i=0
while [ $i -lt 64 ]; do
    name=$("$JQ" ".tools[$i].name" "$D" 2>/dev/null) || break
    case "$name" in
        read_file)  ro_read=$("$JQ" ".tools[$i].readonly" "$D") ;;
        write_file) ro_write=$("$JQ" ".tools[$i].readonly" "$D") ;;
    esac
    i=$((i + 1))
done
if [ "$ro_read" = "true" ] && [ "$ro_write" = "false" ]; then
    t_ok "tool readonly flags: read_file=true, write_file=false"
else
    t_fail "readonly flags wrong: read=$ro_read write=$ro_write"
fi

JC_CONFIG=/nonexistent/there/config.json "$BIN" describe \
    < /dev/null > "$tmp/text" 2>/dev/null; rc=$?
if [ $rc -eq 0 ] && grep -q "interface contract" "$tmp/text"; then
    t_ok "the text form works too"
else
    t_fail "describe (text) rc=$rc"
fi

# M301: THE TWO RENDERINGS MUST AGREE. They did not: the text form listed exit code
# 143 and the JSON omitted it -- and 143 is the code M146 added so a supervisor
# could tell a graceful SIGTERM from a crash, so the machine contract was missing
# the entry that exists for machines. Every exit code named in the text must appear
# in the JSON array.
missing=""
for code in 0 1 2 130 143; do
    "$JQ" -q ".exit_codes[] | select(.name==\"$code\")" "$D" 2>/dev/null \
        || grep -q "\"name\":\"$code\"" "$D" || missing="$missing $code"
done
if [ -z "$missing" ]; then
    t_ok "every documented exit code is in the JSON contract (incl. 143)"
else
    t_fail "exit code(s) missing from the machine contract:$missing"
fi

# M301: an event's `fields` array is split on spaces, so prose passed as a field
# list becomes fake field names -- the heartbeat entry shipped "(only", "with",
# "--heartbeat" as fields. A field name is an identifier; nothing else belongs.
# Checked POSITIVELY -- every field must look like an identifier. A blocklist of
# prose words was the first attempt and it flagged "model", which is a real field of
# message_start: guessing which words are prose is the same mistake M295 avoided.
# The extraction avoids a doubled opening bracket: smoke_lint reads it as bash's
# test keyword, and it is right to be blunt about that.
# Two greps on purpose. The first is deliberately loose because a doubled opening
# bracket trips smoke_lint's bashism check (comments are not exempt, by design --
# this comment said it the forbidden way first); the second restores that this was an
# ARRAY, which matters because the daemon section has a "fields" OBJECT whose values
# are legitimately prose ("string (required)"). Dropping that filter made this check
# fail against correct output.
bad=$(grep -o '"fields":.[^]]*' "$D" \
      | grep '^"fields":\[' \
      | grep -o '"[^"]*"' | grep -v '^"fields"$' | tr -d '"' \
      | grep -vE '^[a-z_][a-z0-9_]*$' | tr '\n' ' ')
if [ -z "$bad" ]; then
    t_ok "event field arrays carry identifiers only, not prose"
else
    t_fail "non-identifier token(s) in an event fields array (put prose in the
 note field instead): $bad"
fi

# M301: the contract must point at its own stability policy. A consumer that cannot
# tell a promise from an implementation detail has to infer it from the ROADMAP --
# a design history, not an interface -- which is what this milestone found missing.
if grep -q '"stability"' "$D" && grep -q "EMBEDDING.md" "$D"; then
    t_ok "the contract names its stability policy and where to read it"
else
    t_fail "no stability statement in describe --output json"
fi

t_done
