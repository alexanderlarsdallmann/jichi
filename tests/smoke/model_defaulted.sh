#!/bin/sh
# smoke: a model id nobody configured is reported as substituted, not as chosen
# (M505).
#
# THE DEFECT THIS EXISTS FOR, found while REVIEWING this project's own new
# documentation (the DOC_REVIEW §5 pass, which is why the review exists). A
# config of `{"models":[{"name":"a"}]}` produced:
#
#     $ jichi --config bad.json config validate
#     OK: bad.json
#       1 model(s); active: claude-opus-4-8
#
# `default_model(provider)` fills a missing "model" field -- `gpt-4o` for openai,
# `claude-opus-4-8` otherwise -- and doctor rendered the result as a GREEN
# "configuration loaded" line, indistinguishable from a config that named that
# model.
#
# Two reasons this is worse than a cosmetic gap. The substitution reaches for a
# PRICED FRONTIER id, which is the hazard ANECDOTES #63 records in this project's
# own history (~$10 spent on a model nobody authorised); and a hardcoded id in a
# fallback is a stale claim by construction.
#
# The default is NOT removed here -- that would change behaviour for configs that
# rely on it. This is a reporting defect, the same shape as M503's verify_source:
# the value may be right, and its provenance was invisible.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)

cat > "$tmp/defaulted.json" <<'EOS'
{"models":[{"name":"a","apiBase":"http://127.0.0.1:1/v1","apiKey":"x"}],
 "lowResource":false,"snapshots":false,"repoMap":false}
EOS
cat > "$tmp/named.json" <<'EOS'
{"models":[{"name":"a","model":"jlu/qwen3-coder-next",
 "apiBase":"http://127.0.0.1:1/v1","apiKey":"x","roles":["chat"]}],
 "lowResource":false,"snapshots":false,"repoMap":false}
EOS

# ---- 1. the substitution is reported ---------------------------------------
out=$(with_deadline 60 "$BIN" --config "$tmp/defaulted.json" doctor \
      < /dev/null 2>&1)
if printf '%s' "$out" | grep -q 'DEFAULTED, not configured'; then
    t_ok "a model id filled from the built-in default is reported as such"
else
    t_fail "the substituted id was presented as a configured one -- the reader \
cannot tell a chosen model from a default: \
$(printf '%s' "$out" | grep -i model | head_bytes 200)"
fi

# ---- 2. and it names the id AND the provider whose default supplied it ------
# Without both, the reader cannot tell WHICH default fired, and the two differ:
# openai gets gpt-4o, everything else a priced Anthropic id.
if printf '%s' "$out" | grep -q "substituted from the built-in default for provider"; then
    t_ok "the notice names the id and the provider default that supplied it"
else
    t_fail "the notice does not say where the id came from: \
$(printf '%s' "$out" | grep -i default | head_bytes 200)"
fi

# ---- 3. a NAMED model stays silent ----------------------------------------
# A warning that fires on a correct config trains the reader to ignore it, which
# is worse than no warning at all.
out2=$(with_deadline 60 "$BIN" --config "$tmp/named.json" doctor \
       < /dev/null 2>&1)
if ! printf '%s' "$out2" | grep -q 'DEFAULTED'; then
    t_ok "a config that names its model produces no such notice"
else
    t_fail "the notice fired on a config that named its model explicitly"
fi

# ---- 4. --unattended treats it as fatal -----------------------------------
# A supervisor starting a loop against a model nobody chose is a posture problem
# of exactly the kind M158b's escalation set exists for -- and the substituted id
# may be a priced one.
with_deadline 60 "$BIN" --config "$tmp/defaulted.json" doctor --unattended \
    < /dev/null > /dev/null 2>&1; rc=$?
if [ "$rc" -ne 0 ]; then
    t_ok "--unattended refuses to start against a defaulted model id (exit $rc)"
else
    t_fail "an unattended supervisor would start against a model nobody chose"
fi

t_done
