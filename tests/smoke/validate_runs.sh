#!/bin/sh
# smoke: `config validate` fails what jichi refuses to run (M719, plan D5 --
# decided "fail" by the operator on 2026-09-23).
#
# THE DEFECT, measured by the M713 review on the installed v0.10.0: against
# `{"models":[{"name":"a"}]}`,
#
#     jichi --config cfg.json config validate   ->  OK: cfg.json            exit 0
#     jichi --config cfg.json doctor            ->  x no model is configured  exit 1
#
# The same file, opposite verdicts -- and the OK was a statement about a config
# the program will not run. DEFERRED.md carried it as "should validate surface
# posture warnings", on the premise that such a config resolves to a priced
# default; M709 had already made that false.
#
# THE DECISION, and its limit. `validate` fails, with doctor's OWN sentence, for
# what jichi refuses to run with: an active model that names no model id, and --
# since M718 -- one that names a model but no wire dialect. Everything else stays
# a parse check: doctor's WARNINGS (no key, no pricing) do not fail it, because
# two renderers of one judgement drift apart (check 4 holds that line). The
# sentence is compared against doctor's actual output, not a copy of it, so the
# two surfaces cannot drift apart silently (check 2).
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
unset ANTHROPIC_API_KEY OPENAI_API_KEY JC_PROVIDER

printf '%s\n' '{"lowResource":false,"models":[{"name":"a"}]}' > "$tmp/nomodel.json"
printf '%s\n' '{"lowResource":false,"models":[{"name":"a","model":"some-model","apiBase":"http://127.0.0.1:9/v1"}]}' \
    > "$tmp/noprov.json"
printf '%s\n' '{"lowResource":false,"models":[{"name":"a","provider":"openai","model":"some-model","apiBase":"http://127.0.0.1:9/v1"}]}' \
    > "$tmp/runs.json"
printf '%s\n' '{"lowResource":false,"models":[{"name":"a",' > "$tmp/broken.json"

val() {   # val NAME -> $tmp/NAME.val (stdout+stderr), $tmp/NAME.vrc
    with_deadline 30 "$BIN" --config "$tmp/$1.json" config validate \
        < /dev/null > "$tmp/$1.val" 2>&1
    echo $? > "$tmp/$1.vrc"
}
for c in nomodel noprov runs broken; do val "$c"; done
with_deadline 30 "$BIN" --config "$tmp/nomodel.json" doctor \
    < /dev/null > "$tmp/nomodel.doc" 2>&1
with_deadline 30 "$BIN" --config "$tmp/runs.json" doctor \
    < /dev/null > "$tmp/runs.doc" 2>&1

show() { head_bytes 200 < "$tmp/$1.val" | tr '\n' ' '; }

# ---- 1-2. no model id: fail, and say exactly what doctor says --------------------
if [ "$(cat "$tmp/nomodel.vrc")" -ne 0 ] && ! grep -q '^OK' "$tmp/nomodel.val"; then
    t_ok "validate fails a config that names no model"
else
    t_fail "validate approved a config jichi will not run \
(rc=$(cat "$tmp/nomodel.vrc")): $(show nomodel)"
fi
# doctor's detail line is the one after "no model is configured"; compare text.
sentence=$(awk 'f { sub(/^[ \t]+/, ""); print; exit } /no model is configured/ { f = 1 }' \
               "$tmp/nomodel.doc")
if [ "${#sentence}" -lt 40 ]; then
    t_fail "could not read doctor's no-model sentence (got '$sentence') -- \
check 2 would be vacuous"
elif grep -qF "$sentence" "$tmp/nomodel.val"; then
    t_ok "validate prints doctor's own sentence, word for word"
else
    t_fail "validate and doctor disagree in words: doctor says '$sentence'; \
validate says: $(show nomodel)"
fi

# ---- 3. a named model with no wire dialect: the M718 refusal ---------------------
if [ "$(cat "$tmp/noprov.vrc")" -ne 0 ] && grep -q 'names no provider' "$tmp/noprov.val"; then
    t_ok "validate fails an entry that names no provider, with the refusal's sentence"
else
    t_fail "validate approved an entry jichi refuses to run \
(rc=$(cat "$tmp/noprov.vrc")): $(show noprov)"
fi

# ---- 4-5. and it stays a parse check -----------------------------------------------
if [ "$(cat "$tmp/runs.vrc")" -eq 0 ] && grep -q '^OK' "$tmp/runs.val"; then
    t_ok "a config jichi runs still validates OK"
else
    t_fail "validate refused a runnable config (rc=$(cat "$tmp/runs.vrc")): $(show runs)"
fi
# The premise of check 4 being meaningful: doctor DOES warn about that config.
if grep -q '^! ' "$tmp/runs.doc"; then
    t_ok "doctor warns about that same config -- and validate did not fail on it"
else
    t_fail "doctor raised no warning on the runnable config, so check 4 proves \
nothing about warnings: $(head_bytes 200 < "$tmp/runs.doc" | tr '\n' ' ')"
fi

# ---- 6. a malformed config still fails (the parse check itself) ------------------
if [ "$(cat "$tmp/broken.vrc")" -ne 0 ]; then
    t_ok "a malformed config still fails validate"
else
    t_fail "validate passed malformed JSON: $(show broken)"
fi

t_done
