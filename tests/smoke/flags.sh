#!/bin/sh
# smoke: CLI surface basics -- version/help exit 0, an unknown flag is a
# usage error (exit 2, stderr), and `describe --output json` (the
# machine-readable interface contract) parses and carries a version.
. "$(dirname "$0")/_smoke.sh"

t_plan 12
smoke_home
tmp=$(smoke_tmp)

out=$("$BIN" --version < /dev/null 2>/dev/null); rc=$?
if [ $rc -eq 0 ] && [ -n "$out" ]; then
    t_ok "--version exits 0 with output"
else
    t_fail "--version rc=$rc out='$out'"
fi

"$BIN" --help < /dev/null > "$tmp/help" 2>&1; rc=$?
if [ $rc -eq 0 ] && grep -q "Usage:" "$tmp/help" \
   && grep -q "Commands:" "$tmp/help"; then
    t_ok "--help exits 0 with Usage/Commands sections"
else
    t_fail "--help rc=$rc or missing Usage:/Commands:"
fi

# --silent is accepted as a synonym of -q (on a help run, still exit 0).
"$BIN" --silent --help < /dev/null >/dev/null 2>&1; rc=$?
if [ $rc -eq 0 ]; then
    t_ok "--silent is accepted"
else
    t_fail "--silent rc=$rc"
fi

"$BIN" --definitely-not-a-flag < /dev/null \
    >"$tmp/out" 2>"$tmp/err"; rc=$?
if [ $rc -eq 2 ] && [ -s "$tmp/err" ]; then
    t_ok "unknown flag: exit 2 + stderr usage"
else
    t_fail "unknown flag: rc=$rc stderr=$(head_bytes 80 "$tmp/err")"
fi

# describe must not need a config (a driving agent introspects jichi on a
# fresh machine), mirroring tests/e2e/describe.py.
JC_CONFIG=/nonexistent/config.json "$BIN" describe --output json \
    < /dev/null > "$tmp/describe.json" 2>/dev/null; rc=$?
ver=$("$SMOKE_TOOLS/jsonq" '.version' "$tmp/describe.json" 2>/dev/null)
if [ $rc -eq 0 ] && [ -n "$ver" ]; then
    t_ok "describe --output json carries version $ver"
else
    t_fail "describe json rc=$rc version='$ver'"
fi

if "$SMOKE_TOOLS/jsonq" -q -t number '.v' "$tmp/describe.json" \
    && "$SMOKE_TOOLS/jsonq" -q '.tools[0].name' "$tmp/describe.json"; then
    t_ok "describe json has v + a tools array"
else
    t_fail "describe json missing v / tools[0].name"
fi

# M375: -p must refuse a flag-shaped argument instead of taking it as the
# prompt. `jichi -p --no-session "question"` once asked the model about the
# flag and silently dropped the question (the version-probe misdiagnosis).
"$BIN" --config-json "{}" -p --no-session "a real question" < /dev/null \
    >"$tmp/out5" 2>"$tmp/err5"; rc=$?
if [ $rc -eq 2 ] && grep -q "looks like a flag" "$tmp/err5"; then
    t_ok "-p refuses a flag-shaped prompt (exit 2, names the flag)"
else
    t_fail "-p flag-shaped: rc=$rc err=$(head_bytes 100 "$tmp/err5")"
fi

# M375: a positional beside an already-given -p prompt was silently ignored
# (that is where the dropped question went); refused loudly instead.
"$BIN" --config-json "{}" -p "hello" stray-word < /dev/null \
    >"$tmp/out6" 2>"$tmp/err6"; rc=$?
if [ $rc -eq 2 ] && grep -q "unexpected argument" "$tmp/err6"; then
    t_ok "-p plus a stray positional is refused (exit 2)"
else
    t_fail "-p stray positional: rc=$rc err=$(head_bytes 100 "$tmp/err6")"
fi

# M376 (S1): smoke_home must neutralize the AMBIENT project config. A planted
# ./local/config.json (a real-endpoint file on a dev box) must not reach a
# driver that runs "$BIN" without --config -- M375's born-red run made live
# model calls through exactly this channel.
cw=$(smoke_tmp)
mkdir -p "$cw/local"
printf '%s' '{"lowResource":false,"models":[{"name":"AMBIENT_CANARY","provider":"openai","model":"canary/x","apiBase":"http://127.0.0.1:1"}]}' \
    > "$cw/local/config.json"
out=$(cd "$cw" && "$BIN" status < /dev/null 2>&1)
case "$out" in
    *AMBIENT_CANARY*) t_fail "ambient local/config.json reached the binary" ;;
    *) t_ok "ambient local/config.json is neutralized for smoke drivers" ;;
esac

# M376 (S2): -p beside a dispatched subcommand ran the subcommand and dropped
# the prompt without a word; two run modes in one invocation are refused.
with_deadline 20 "$BIN" --config-json "{}" -p "hello" describe < /dev/null \
    >"$tmp/out7" 2>"$tmp/err7"; rc=$?
if [ $rc -eq 2 ] && grep -q "unexpected argument" "$tmp/err7"; then
    t_ok "-p beside a subcommand is refused (exit 2)"
else
    t_fail "-p + subcommand: rc=$rc err=$(head_bytes 100 "$tmp/err7")"
fi

# M378: the no-key warning must give the project's own advice. When the
# active model declares apiKeyEnv, the warning names THAT variable; it must
# never recommend the literal apiKey config shape doctor's M55 lint warns
# against (the wrong-advice string genre, M292).
with_deadline 20 "$BIN" --config-json '{"lowResource":false,"maxRetries":0,"models":[{"name":"m","provider":"openai","model":"x","apiBase":"http://127.0.0.1:1/v1","apiKeyEnv":"JC_SMOKE_UNSET_KEY"}]}' \
    -p "hi" < /dev/null >"$tmp/out9" 2>"$tmp/err9" || true
if grep -q "JC_SMOKE_UNSET_KEY" "$tmp/err9" \
   && ! grep -q "configure apiKey)" "$tmp/err9"; then
    t_ok "the no-key warning names the model's own apiKeyEnv variable"
else
    t_fail "no-key warning: $(grep -i 'api key' "$tmp/err9" | head -1 | head_bytes 120)"
fi

# M376 (S2): --acp selects the ACP server run mode, which cannot also take a
# -p prompt; previously the prompt was silently discarded.
with_deadline 20 "$BIN" --config-json "{}" --acp -p "hello" < /dev/null \
    >"$tmp/out8" 2>"$tmp/err8"; rc=$?
if [ $rc -eq 2 ] && grep -q "run modes" "$tmp/err8"; then
    t_ok "--acp beside -p is refused (exit 2)"
else
    t_fail "--acp + -p: rc=$rc err=$(head_bytes 100 "$tmp/err8")"
fi

t_done
