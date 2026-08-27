#!/bin/sh
# smoke: custom output styles (M28, offline) -- a project style activated
# via config `outputStyle` is injected into the system prompt (`sysmsg`),
# listed with the active marker, and overridable via --output-style.
# (Port of tests/e2e/output_style.py, M210.)
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)

mkdir -p "$ws/.jichi/output-styles"
cat > "$ws/.jichi/output-styles/haiku.md" <<'EOF'
---
description: Haiku replies
---
ANSWER_ONLY_IN_HAIKU
EOF
printf 'Be plain.\n' > "$ws/.jichi/output-styles/plain.md"

cat > "$tmp/config.json" <<'EOF'
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"x","apiKey":"k"}],
 "outputStyle":"haiku","repoMap":false}
EOF

(cd "$ws" && "$BIN" --config "$tmp/config.json" sysmsg \
    < /dev/null > "$tmp/sys" 2>&1)
if grep -q "# Output style" "$tmp/sys" \
   && grep -q "ANSWER_ONLY_IN_HAIKU" "$tmp/sys"; then
    t_ok "active style injected into the system prompt"
else
    t_fail "style not in sysmsg: $(tail -c 200 "$tmp/sys")"
fi

(cd "$ws" && "$BIN" --config "$tmp/config.json" output-styles \
    < /dev/null > "$tmp/list" 2>&1)
if grep -q "\* haiku" "$tmp/list" && grep -q "plain" "$tmp/list"; then
    t_ok "output-styles lists both, marking the active one"
else
    t_fail "listing wrong: $(head_bytes 200 "$tmp/list")"
fi

(cd "$ws" && "$BIN" --config "$tmp/config.json" --output-style plain \
    sysmsg < /dev/null > "$tmp/sys2" 2>&1)
if grep -q "Be plain." "$tmp/sys2" \
   && ! grep -q "ANSWER_ONLY_IN_HAIKU" "$tmp/sys2"; then
    t_ok "--output-style overrides the config"
else
    t_fail "override failed"
fi

t_done
