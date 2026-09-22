#!/bin/sh
# smoke: no model id is EVER substituted, and nothing invents a vendor (M709,
# inverting M505).
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
# M505 kept the default and made it visible, calling this a reporting defect.
# M709 REMOVED IT, because it was a resolution defect: the operator installed
# jichi on a second machine and was told, correctly, that it was configured to
# spend money on `claude-opus-4-8` -- a decision nobody had taken. A config that
# relies on the old default was relying on a priced frontier id chosen by a
# string literal, which is the thing not worth preserving.
#
# The checks below are inverted from M505's on purpose. What they guard is the
# same concern, from the other side: a reader must never see a model id the
# program picked.
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

# ---- 1. a config naming no model is a doctor FAILURE, not a green line -----
out=$(with_deadline 60 "$BIN" --config "$tmp/defaulted.json" doctor \
      < /dev/null 2>&1)
if printf '%s' "$out" | grep -q 'no model is configured'; then
    t_ok "a config that names no model is reported as having none"
else
    t_fail "a config naming no model did not say so -- the reader cannot tell \
it from a working one: $(printf '%s' "$out" | grep -i model | head_bytes 200)"
fi

# ---- 2. AND NO VENDOR MODEL ID APPEARS ANYWHERE IN THE OUTPUT --------------
# The check the operator's finding turns on, and it is deliberately about the
# whole output rather than one line: a substitution anywhere -- the loaded line,
# a warning, a hint -- is a model id the program chose.
#
# A LETTER MUST FOLLOW THE DASH, and the match may not be preceded by a path
# character. Measured while verifying the installed binary: the first version of
# this pattern was a bare `claude-`, and it fired on doctor's own `state root`
# line because that run's HOME was under `/tmp/claude-1000/`. A vendor id is
# always `claude-<letter>` (opus, sonnet, haiku); a path segment that happens to
# contain the word is not, and neither is a version number. Same defect shape as
# the `TOS` that matched `CentOS` eleven times the same day.
if printf '%s' "$out" | grep -qE '(^|[^/A-Za-z0-9_-])(claude-[a-z]|gpt-[0-9][a-z]?)|api\.(anthropic|openai)\.com'; then
    t_fail "doctor named a vendor model id for a config that names none: \
$(printf '%s' "$out" | grep -iE 'claude-|gpt-4|gpt-5' | head_bytes 240)"
else
    t_ok "no vendor model id appears anywhere in the output for a config naming none"
fi

# ---- 3. a NAMED model stays silent ----------------------------------------
# A warning that fires on a correct config trains the reader to ignore it, which
# is worse than no warning at all.
out2=$(with_deadline 60 "$BIN" --config "$tmp/named.json" doctor \
       < /dev/null 2>&1)
if ! printf '%s' "$out2" | grep -q 'no model is configured'; then
    t_ok "a config that names its model produces no such notice"
else
    t_fail "the notice fired on a config that named its model explicitly"
fi

# ---- 4. --unattended treats it as fatal -----------------------------------
# A supervisor starting a loop with no model configured cannot do anything
# useful, and before M709 it would have started against a priced id nobody
# chose -- the posture problem M158b's escalation set exists for.
with_deadline 60 "$BIN" --config "$tmp/defaulted.json" doctor --unattended \
    < /dev/null > /dev/null 2>&1; rc=$?
if [ "$rc" -ne 0 ]; then
    t_ok "--unattended refuses to start with no model configured (exit $rc)"
else
    t_fail "an unattended supervisor would start with no model configured"
fi

t_done
