#!/bin/sh
# smoke: /status and the idle proverb under --accessible (M574).
#
# TWO SURFACES, opposite conclusions, and the contrast is the point.
#
# /status NEEDED ALMOST NOTHING. Its alignment is whitespace, which a reader
# collapses; its labels are words; `on`/`off` are words. Exactly one line put
# information into punctuation:
#
#     tokens:      3,960 in / 10 out
#
# The whole relation is in a SLASH. The sentence that replaces it already exists
# in the catalog with a German translation (JC_MSG_SESSION_TOKENS) -- which is
# the M566 argument for a catalog over a literal, arriving as a benefit rather
# than a lesson for once.
#
# THE IDLE PROVERB NEEDED SUPPRESSING, and that is a different kind of finding.
# `print_wisdom` was already clean text -- no glyph, no brackets, dim and
# indented. Nothing about how it is WRITTEN is wrong. What is wrong is that
# `jc_config.c` sets `out->wisdom = 1`, so it is ON BY DEFAULT and prints before
# every idle prompt: a sighted reader glances past it, a listener must sit
# through it to reach the prompt. Same text, same channel, and the cost differs
# by audience -- which is the M561 test applied to VOLUME rather than to
# punctuation.
#
# So accessible mode suppresses it, and `/wisdom on` typed by a person overrides
# that (check 5). Suppressing something a user explicitly asked for would be
# paternalism, not accessibility.
#
# GATED AT THE PRINT SITE, not at initialisation, for two measured reasons:
# `/accessible on` can be typed mid-session, and `show_wisdom` is computed
# BEFORE `ctx.accessible` is assigned -- so an init-time test would read a stale
# zero and the suppression would never fire.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
G=/usr/bin/grep
[ -x "$G" ] || G=grep
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# THE FIXTURE: proverbs are read from $cwd/.jichi/wisdom.json, so the workspace
# is where they go -- not $HOME, which is what the first attempt at this driver
# assumed.
mkdir -p "$ws/.jichi"
# THE SHAPE MATTERS: wisdom_add_file accepts a top-level array or an
# `entries` array, of OBJECTS with a `text` field. My first fixture wrote
# `{"proverbs": ["..."]}` -- valid JSON, zero proverbs loaded, and checks 5 and
# 6 both went red saying the feature was broken when the fixture was.
cat > "$ws/.jichi/wisdom.json" <<'EOF'
{ "entries": [ { "text": "MEASURE_TWICE_CUT_ONCE" } ] }
EOF

cat > "$tmp/m.mm" <<'EOF'
wire openai
rule
  text hello there
EOF

# One script for every arm: say something (so a turn completes and the idle
# prompt is reached again, which is when a proverb prints), then /status.
cat > "$tmp/p.pd" <<'EOF'
expect "> " 15
send "hi\r"
expect "hello there" 25
delay 800
send "/status\r"
delay 1200
send "/exit\r"
waitexit 15
EOF

run_arm() {   # run_arm <name> <extra-flags...>
    _n="$1"; shift
    mm_start "$tmp/m.mm" "$tmp/cap.$_n"
    write_config "$tmp/config.json" "$MM_PORT"
    (cd "$ws" && with_deadline 70 "$SMOKE_TOOLS/ptydrive" --deadline 65 \
        --cols 100 --log "$tmp/$_n.log" "$tmp/p.pd" -- \
        "$BIN" --config "$tmp/config.json" --no-session "$@" \
        > /dev/null 2>&1) || true
    mm_stop
    tr '\r' '\n' < "$tmp/$_n.log" > "$tmp/$_n.txt"
}

run_arm acc --accessible
run_arm pln

# The explicit-override arm needs its own script: /wisdom on, then a turn.
cat > "$tmp/w.pd" <<'EOF'
expect "> " 15
send "/wisdom on\r"
delay 700
send "hi\r"
expect "hello there" 25
delay 1200
send "/exit\r"
waitexit 15
EOF
mm_start "$tmp/m.mm" "$tmp/cap.exp"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 70 "$SMOKE_TOOLS/ptydrive" --deadline 65 --cols 100 \
    --log "$tmp/exp.log" "$tmp/w.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --accessible \
    > /dev/null 2>&1) || true
mm_stop
tr '\r' '\n' < "$tmp/exp.log" > "$tmp/exp.txt"

# ---- 1: the denominator -- all three arms ran and reached /status or a turn --
# Checks 2-6 are a mix of presence and ABSENCE assertions, and the absences hold
# trivially in an empty capture. Floored on all three arms, because the first
# version of this driver put wisdom.json in $HOME and every proverb check passed
# by finding nothing.
bad=""
$G -q 'model:' "$tmp/acc.txt" || bad="$bad acc(no-status)"
$G -q 'model:' "$tmp/pln.txt" || bad="$bad pln(no-status)"
# FLOORED ON THE FIXTURE LOADING, not merely on the toggle being acknowledged:
# `/wisdom on` prints "idle proverbs on; N loaded" whether N is 1 or 0, and a
# fixture in the wrong shape yields 0 while every message still looks right.
# That is exactly how the first version of this driver reported a product bug
# that was mine.
$G -q 'idle proverbs on; [1-9]' "$tmp/exp.txt" || bad="$bad exp(0-proverbs-loaded)"
$G -q 'hello there' "$tmp/exp.txt" || bad="$bad exp(no-turn)"
if [ -z "$bad" ]; then
    t_ok "all three arms reached /status or completed a turn"
else
    t_fail "an arm produced nothing to test:$bad -- the absence assertions below \
would hold trivially. acc tail: \
$(tail -2 "$tmp/acc.txt" 2>/dev/null | tr '\n' ' ' | head_bytes 200)"
fi

# ---- 2: accessible /status states the token counts as a sentence ------------
if $G -q 'input tokens' "$tmp/acc.txt" && ! $G -q 'in / ' "$tmp/acc.txt"; then
    t_ok "accessible /status: the token line is prose, with no slash form"
else
    t_fail "the /status token line is not accessible prose. Saw: \
$($G -o 'tokens:.*\|[0-9,]* input tokens.*' "$tmp/acc.txt" | head -2 \
  | tr '\n' ' '). It should reuse JC_MSG_SESSION_TOKENS, which already has a \
German translation."
fi

# ---- 3: THE REGRESSION GUARD -- the sighted /status is unchanged ------------
# The aligned form is easier to scan in a column, which is why it stays.
if $G -q 'tokens:' "$tmp/pln.txt" && $G -q 'in / ' "$tmp/pln.txt" &&
   ! $G -q 'input tokens' "$tmp/pln.txt"
then
    t_ok "default /status keeps its aligned in / out form"
else
    t_fail "the DEFAULT /status changed. --accessible must add an arm, not \
replace the aligned form: $($G -o 'tokens:.*' "$tmp/pln.txt" | head -1)"
fi

# ---- 4: no unasked proverb for a listener ----------------------------------
if ! $G -q 'MEASURE_TWICE_CUT_ONCE' "$tmp/acc.txt"; then
    t_ok "accessible: no idle proverb before the prompt"
else
    t_fail "an idle proverb was read to a listener who never asked for one -- \
config wisdom defaults to 1, so this arrives unrequested and must be sat \
through before the prompt. Occurrences: \
$($G -c 'MEASURE_TWICE_CUT_ONCE' "$tmp/acc.txt")"
fi

# ---- 5: but an EXPLICIT request is honoured -------------------------------
# The difference between accessibility and paternalism. Without this check,
# suppressing the proverb outright would pass check 4 and be wrong.
if $G -q 'MEASURE_TWICE_CUT_ONCE' "$tmp/exp.txt"; then
    t_ok "accessible + /wisdom on: the proverb the user asked for is shown"
else
    t_fail "/wisdom on was ignored under --accessible. Suppressing what a user \
explicitly enabled is paternalism, not accessibility -- wisdom_explicit exists \
to draw exactly that line. Toggle seen: \
$($G -c 'idle proverbs on' "$tmp/exp.txt"), proverb: \
$($G -c 'MEASURE_TWICE_CUT_ONCE' "$tmp/exp.txt")"
fi

# ---- 6: and the sighted default still shows it ---------------------------
# The other half of check 4: a fix that removed proverbs for everyone would pass
# checks 4 and 5 and silently delete a feature.
if $G -q 'MEASURE_TWICE_CUT_ONCE' "$tmp/pln.txt"; then
    t_ok "default: the idle proverb still appears"
else
    t_fail "idle proverbs vanished for sighted users too -- the fixture writes \
one to \$cwd/.jichi/wisdom.json and the default config has wisdom on, so this \
should appear. Occurrences: $($G -c 'MEASURE_TWICE' "$tmp/pln.txt")"
fi

t_done
