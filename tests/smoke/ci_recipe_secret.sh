#!/bin/sh
# smoke: DEPLOYMENT.md's CI recipe, run as published, keeps the model's key from every
# process jichi starts -- the verify command, which runs code the model wrote, included
# (M894).
#
# THE DEFECT. The worked CI example began with `export OPENAI_API_KEY="$CI_LLM_KEY"`.
# jichi removes a key from its children only under the names it knows -- each
# configured apiKeyEnv and a built-in list of the providers' usual names -- so it
# removed the copy, and the original stayed: the example's `make test`, which builds and
# runs the code the model wrote, saw the key under CI_LLM_KEY. Measured with the mock
# model on 2026-10-03 while researching question 3b of that day's plan. And the copy
# authenticated nothing unless the config pointed at OpenAI's own endpoint: since M718
# jichi sends a provider's usual variable only there, so against any other gateway the
# old recipe sent no key at all -- which is why check 3 fails on the old page too.
#
# THE FIX. The recipe names the CI variable in the config (`"apiKeyEnv": "CI_LLM_KEY"`),
# so jichi reads the key from it and removes it; the copy is gone; and the call closes
# stdin, as every headless call must (ANECDOTES #64).
#
#   1  the published recipe runs as published, and its verify passes
#   2  its verify command sees the key under no name
#   3  the run authenticated with the key from the variable the page names
#   4  the recipe copies no key into another variable
#   5  the recipe's jichi call closes stdin (< /dev/null)
#   6  CONTROL: a key copied into a known name stays visible under its own -- the
#      reason the page says never to copy it
#
# The recipe is read from docs/DEPLOYMENT.md section 4c, not retyped here. One line is
# left out: `jq -r '.text' result.json` prints the answer, needs jq, and holds no
# secret. Everything else runs as printed. The counts come from the verify command's own
# environment, written by a Makefile the fixture provides.
. "$(dirname "$0")/_smoke.sh"

command -v make >/dev/null 2>&1 || t_skip "needs make -- the recipe's verify is make test"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
DOC="$SMOKE_ROOT/docs/DEPLOYMENT.md"

awk '/^### 4c\./{s=1; next} s && /^### /{exit} s' "$DOC" > "$tmp/sec.md"
awk '/^```sh$/{b=1; next} b && /^```$/{exit} b' "$tmp/sec.md" > "$tmp/recipe.sh"
grep -v "^jq " "$tmp/recipe.sh" > "$tmp/run.sh"
nlines=$(grep -c . "$tmp/recipe.sh")
keyenv=$(grep -o '"apiKeyEnv": *"[A-Z_][A-Z0-9_]*"' "$tmp/sec.md" | head -n 1 |
         sed 's/.*"\([A-Z_][A-Z0-9_]*\)"$/\1/')

# fixture WS CFGEXTRA -- a workspace whose `make test` counts the key's two names in
# its own environment, and a config at the path the recipe names
fixture() {
    mkdir -p "$1/src" "$1/tests" "$1/local"
    printf 'test:\n\t@env | grep -c -e "^CI_LLM_KEY=" > %s/ci.n || true\n\t@env | grep -c -e "^OPENAI_API_KEY=" > %s/oa.n || true\n' \
        "$tmp" "$tmp" > "$1/Makefile"
    cat > "$1/local/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1"$2,"roles":["chat"]}],
 "repoMap":false,"references":false,"maxRetries":0,"lowResource":false}
EOF
}

cat > "$tmp/r.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  text RECIPE_DONE
rule
  tool write_file {"path":"src/fix.c","content":"int fixed;\n"}
EOF
mkdir -p "$tmp/bin"
ln -s "$BIN" "$tmp/bin/jichi"

# --- the recipe, as published -----------------------------------------------------------
ws=$(smoke_tmp)
mm_start "$tmp/r.mm" "$tmp/cap" 6
fixture "$ws" "${keyenv:+,\"apiKeyEnv\":\"$keyenv\"}"
rm -f "$tmp/ci.n" "$tmp/oa.n"
(cd "$ws" && with_deadline 120 env PATH="$tmp/bin:$PATH" CI_LLM_KEY=canary-ci-key \
    sh "$tmp/run.sh" < /dev/null) > "$tmp/out" 2>&1
rc=$?
mm_stop

if [ "$nlines" -ge 8 ] && [ "$rc" = "0" ] && [ -f "$tmp/ci.n" ] &&
   grep -q '"verify":"green"' "$ws/result.json" 2>/dev/null; then
    t_ok "the published recipe runs as published, and its verify passes"
else
    t_fail "recipe ($nlines lines) exited $rc: $(head_bytes 300 "$tmp/out")"
fi
if [ "$(cat "$tmp/ci.n" 2>/dev/null)" = "0" ] && [ "$(cat "$tmp/oa.n" 2>/dev/null)" = "0" ]; then
    t_ok "its verify command sees the key under no name"
else
    t_fail "the verify command saw CI_LLM_KEY $(cat "$tmp/ci.n" 2>/dev/null || echo '?') and OPENAI_API_KEY $(cat "$tmp/oa.n" 2>/dev/null || echo '?') times"
fi
if grep -h -i '^authorization: bearer canary-ci-key' "$tmp/cap"/req.* >/dev/null 2>&1; then
    t_ok "the run authenticated with the key from the variable the page names (${keyenv:-none named})"
else
    t_fail "the model's requests did not carry the key"
fi
if [ "$nlines" -ge 8 ] && ! grep -qE '^[[:space:]]*export [A-Z_][A-Z0-9_]*=.*\$' "$tmp/recipe.sh"; then
    t_ok "the recipe copies no key into another variable"
else
    t_fail "the recipe copies a variable: $(grep -E '^[[:space:]]*export ' "$tmp/recipe.sh")"
fi
if [ "$nlines" -ge 8 ] && grep -q '< /dev/null > result.json' "$tmp/recipe.sh"; then
    t_ok "the recipe's jichi call closes stdin (< /dev/null)"
else
    t_fail "the jichi call does not close stdin: $(grep 'result.json' "$tmp/recipe.sh" | head -n 1)"
fi

# --- CONTROL: a copied key, under a config that names no variable -----------------------
ws2=$(smoke_tmp)
mm_start "$tmp/r.mm" "$tmp/cap2" 6
fixture "$ws2" ""
rm -f "$tmp/ci.n" "$tmp/oa.n"
(cd "$ws2" && with_deadline 120 env CI_LLM_KEY=canary-ci-key OPENAI_API_KEY=canary-ci-key \
    "$BIN" -p "change only src/" --auto --config ./local/config.json \
    --edit-scope 'src/**' --strict-scope --verify 'make test' \
    --no-session --output json -q < /dev/null > "$tmp/out2" 2>&1)
mm_stop
if [ "$(cat "$tmp/ci.n" 2>/dev/null)" = "1" ] && [ "$(cat "$tmp/oa.n" 2>/dev/null)" = "0" ]; then
    t_ok "CONTROL: a key copied into a known name stays visible under its own"
else
    t_fail "CONTROL: CI_LLM_KEY $(cat "$tmp/ci.n" 2>/dev/null || echo '?'), OPENAI_API_KEY $(cat "$tmp/oa.n" 2>/dev/null || echo '?')"
fi

t_done
