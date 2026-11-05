#!/bin/sh
# M827 (N15, threadwork's demos drive): a read-only inspector that calls NO model --
# `jichi attempts`/`recover`/`undo`/`checkpoints` -- must not print the "No model is
# configured, so jichi cannot call one yet" first-run banner. On the demos drive
# `jichi attempts show` in a container with no config began with that banner and the
# `jichi setup` lines, then printed the attempt: noise to a reader of a kept state,
# who has no reason to set up a model. A run that DOES call a model still gets it.
. "$(dirname "$0")/_smoke.sh"

t_plan 2
smoke_home
# from_defaults must be TRUE: no config anywhere. smoke_home pins JC_CONFIG to a null
# config, which counts as a loaded config, so drop it and run in a fresh empty dir.
unset JC_CONFIG
ws=$(smoke_tmp)

( cd "$ws" && "$BIN" attempts < /dev/null > "$ws/att.out" 2>&1 )
if grep -q 'No model is configured' "$ws/att.out"; then
    t_fail "attempts printed the no-model banner: $(head_bytes 120 "$ws/att.out")"
else
    t_ok "an offline inspector (attempts) does not print the no-model banner"
fi

# Contrast: a run that WOULD call a model still gets the hint (it needs a key).
( cd "$ws" && "$BIN" -p "hello" --no-stdin > "$ws/run.out" 2>&1 )
if grep -q 'No model is configured' "$ws/run.out"; then
    t_ok "a model-calling run still gets the no-model hint"
else
    t_fail "the no-model hint vanished for a real run too: $(head_bytes 120 "$ws/run.out")"
fi
t_done
