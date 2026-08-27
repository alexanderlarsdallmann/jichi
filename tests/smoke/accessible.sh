#!/bin/sh
# smoke: accessible mode, verified at the byte level (M118/M184/M362). The
# claims in docs/ACCESSIBILITY.md were never pinned by a driver until this
# audit; each check below is one of them, measured from raw PTY captures:
#   - the spinner animates by default and is ONE static line in accessible
#     mode (M118);
#   - the transcript is role-labelled (M184: "assistant (...):",
#     "tool call:", "tool result: ... ok");
#   - accessible input echo is incremental (M362): the wrap-aware redraw is
#     one ESC[J + ~39 bytes per keystroke -- measured 37 erase-below
#     sequences in a 28-character session before the fix, 4 after -- and a
#     screen reader re-announces every one of those repaints;
#   - NO_COLOR keeps the accessible transcript SGR-free, and under a C
#     locale it is pure ASCII;
#   - headless -p output carries no escape bytes at all;
#   - the APPROVAL PROMPT's key list is bracket-free in accessible mode
#     (M551) -- found by ear, not by reading code: `[y]es  [n]o  [a]lways`
#     is a key shown *inside* the word it names, which a screen reader
#     announces as "bracket y bracket e s bracket n bracket o". The operator's
#     listening test reported it as "every single character was read... the
#     options were unintelligible with the brackets". Checks 9-11 pin both
#     halves, because "no brackets" alone also passes for a build that lost
#     the prompt entirely.
# WHAT THAT COSTS, measured at M464 and written here so it is not re-derived: a
# fixed pre-send delay is a BET ON STARTUP TIME. jc_term_readline enters raw mode
# with TCSAFLUSH once per prompt, so a send that lands before the first prompt is
# echoed by the tty and then discarded. On this bench the window is under 100 ms
# (at a 1 ms delay the mock receives 0 requests), so 1200 ms is ample; on the
# OpenBSD guest it exceeds 5 s, and there this driver's first send is lost -- which
# is a result about this bet, not about jichi. The fix is an expect rather than a
# delay, and it is genuinely awkward *here* because arm 1 unsets NO_COLOR on
# purpose, which is what the next line is about. See
# docs/analysis/2026-08-17-the-lost-first-send.md.
#
# No expects on the colored prompt: sends are timed (generous fixed delays),
# because the default-mode run keeps color ON to prove the spinner animates.
. "$(dirname "$0")/_smoke.sh"

t_plan 22
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

yes "alpha bravo charlie" | head -10 > "$ws/small.txt"
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  delay 1800
  usage 4946 37
  tool read_file {"path":"small.txt"}
rule
  delay 1800
  usage 4946 37
  text all done here
EOF
cat > "$tmp/run.pd" <<'EOF'
delay 1200
send "read small.txt and summarize\r"
delay 6000
send "/exit\r"
waitexit 15
assertexit 0
EOF

mm_start "$tmp/replies.mm" "$tmp/cap1"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && unset NO_COLOR && with_deadline 60 "$SMOKE_TOOLS/ptydrive" \
    --deadline 60 --cols 100 --log "$tmp/a.log" "$tmp/run.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --auto \
    > /dev/null 2>&1); rca=$?
mm_stop

mm_start "$tmp/replies.mm" "$tmp/cap2"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$SMOKE_TOOLS/ptydrive" \
    --deadline 60 --cols 100 --log "$tmp/b.log" "$tmp/run.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --auto --accessible \
    > /dev/null 2>&1); rcb=$?
mm_stop

ESC=$(printf '\033')
aw=$(grep -o 'working' "$tmp/a.log" 2>/dev/null | wc -l)
bw=$(grep -o 'working' "$tmp/b.log" 2>/dev/null | wc -l)
aj=$(grep -o "$ESC\[J" "$tmp/a.log" 2>/dev/null | wc -l)
bj=$(grep -o "$ESC\[J" "$tmp/b.log" 2>/dev/null | wc -l)

# --- 1: both sessions ran to a clean exit -------------------------------------
if [ "$rca" -eq 0 ] && [ "$rcb" -eq 0 ]; then
    t_ok "default and accessible sessions both exited 0"
else
    t_fail "rc default=$rca accessible=$rcb"
fi

# --- 2: the spinner animates by default and is static in accessible mode ------
# The default run repaints the working line per tick; accessible prints ONE
# static line per model call (two calls here).
if [ "$bw" -eq 2 ] && [ "$aw" -gt "$bw" ]; then
    t_ok "working line: $aw frames default vs $bw static lines accessible (M118)"
else
    t_fail "working counts: default=$aw accessible=$bw (want accessible==2 < default)"
fi

# --- 3: the role-labelled transcript (M184, reworded as prose in M553) -------
# The three role markers still exist and are still in order; what changed is that
# each is a SENTENCE rather than a label. M184's `assistant (m (mock) - chat -
# 11:01:20):` became `Model m (mock) responds with the following:` on the
# operator's instruction -- "I think we need to use phrases, and sentences".
# The ROLE still has to be identifiable, which is what this asserts: who is
# speaking, that a tool is being called, and how it ended.
if grep -q 'responds with the following:' "$tmp/b.log" \
   && grep -q 'Calling the tool read_file' "$tmp/b.log" \
   && grep -q 'The tool read_file finished successfully' "$tmp/b.log"; then
    t_ok "accessible transcript names the roles in prose: model / call / result"
else
    t_fail "a role marker is missing from the accessible transcript: \
$(grep -c . "$tmp/b.log") lines, \
$(grep -oE '(responds|Calling the tool|The tool)' "$tmp/b.log" | sort -u | tr '\n' ' ')"
fi

# --- 4: incremental input echo (M362) -----------------------------------------
# Before the fix this session produced 37 erase-below sequences (one full
# prompt+line repaint per keystroke); with fast echo the survivors are the
# boundary renders only (initial prompt, Enter commits, exit).
if [ "$bj" -le 8 ]; then
    t_ok "accessible session emits $bj ESC[J erase-belows (was 37 pre-M362)"
else
    t_fail "accessible session still repaints per keystroke: $bj ESC[J"
fi
# --- 5: the DEFAULT session is incremental too, since M558 ---------------
# THIS CHECK ASSERTED THE OPPOSITE UNTIL M558, and the inversion is the point.
# It required `aj > bj` -- "the default session repaints more, so the fast path
# is mode-gated" -- which was true, and was a statement of the defect: the
# incremental echo was gated on --accessible for no technical reason.
# jc_term_fast_echo_ok / jc_term_fast_bs_ok already refuse every case where
# appending a byte differs from a repaint, so the flag only chose whether to take
# the cheaper of two identical outcomes.
#
# Measured before flipping it: the default arm went from 37 erase-belows to 4,
# and the request the model receives is byte-identical. Every sighted user now
# gets 37 fewer full-line repaints per session.
#
# The assertion is now that BOTH arms are incremental. A differential
# reappearing means somebody re-gated it.
if [ "$aj" -le 8 ] && [ "$bj" -le 8 ]; then
    t_ok "both sessions are incremental ($aj default, $bj accessible; was 37/4)"
else
    t_fail "a session is still repainting per keystroke: $aj default, $bj \
accessible (want both <= 8). M558 made the incremental echo unconditional -- if \
this is red, either the fast path regressed or the mode gate is back."
fi

# --- 5: NO_COLOR + C locale: SGR-free, pure-ASCII accessible transcript -------
# The pattern is composed in two halves so the smoke lint's bashism scan
# does not trip on a literal open-bracket pair in a regex.
SGRHEAD="$ESC\["
nsgr=$(grep -oE "$SGRHEAD"'[0-9;]*m' "$tmp/b.log" 2>/dev/null | wc -l)
nutf=$(LC_ALL=C grep -c "$(printf '\342')" "$tmp/b.log" 2>/dev/null || true)
if [ "$nsgr" -eq 0 ] && [ "$nutf" -eq 0 ]; then
    t_ok "accessible + NO_COLOR + C locale: zero SGR sequences, zero UTF-8 glyphs"
else
    t_fail "found $nsgr SGR sequences / $nutf UTF-8 glyph bytes"
fi

# --- 6: the default UTF-8 session DOES use glyphs (the fallback's other half) -
if locale -a 2>/dev/null | grep -qi '^C\.utf'; then
    mm_start "$tmp/replies.mm" "$tmp/cap3"
    write_config "$tmp/config.json" "$MM_PORT"
    (cd "$ws" && unset NO_COLOR && LC_ALL=C.utf8 LANG=C.utf8 \
        with_deadline 60 "$SMOKE_TOOLS/ptydrive" \
        --deadline 60 --cols 100 --log "$tmp/c.log" "$tmp/run.pd" -- \
        "$BIN" --config "$tmp/config.json" --no-session --auto \
        > /dev/null 2>&1)
    mm_stop
    if [ "$(LC_ALL=C grep -c "$(printf '\342')" "$tmp/c.log")" -ge 1 ]; then
        t_ok "UTF-8 locale renders glyphs (the M310 pair for the ASCII fallback)"
    else
        t_fail "no glyph bytes in a UTF-8 default session"
    fi
else
    # t_skip_one, NOT t_skip: this is one check of eight, and t_skip ends the
    # whole driver -- which silently dropped checks 7 and 8 on every host
    # without C.UTF-8 (glibc < 2.35), then failed the tier on a count (M450).
    t_skip_one "no C.utf8 locale on this host -- the glyph half is not exercised"
fi

# --- 7: headless output is escape-free ----------------------------------------
mm_start "$tmp/replies.mm" "$tmp/cap4"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --no-session \
    --auto -p "read small.txt and summarize" < /dev/null \
    > "$tmp/h.out" 2> /dev/null) || true
mm_stop
if [ -s "$tmp/h.out" ] \
   && [ "$(LC_ALL=C grep -c "$ESC" "$tmp/h.out")" -eq 0 ]; then
    t_ok "headless -p stdout contains zero escape bytes"
else
    t_fail "headless output missing or carries escapes"
fi

# --- 8: the approval prompt, in both modes -----------------------------------
# WHY THIS NEEDS ITS OWN PAIR OF RUNS: every arm above passes --auto, so the
# approval prompt is never rendered by any of them. The catalog is unit-tested
# (tests/test_msg.c asserts both forms across all five languages); what no unit
# test can see is whether --accessible SELECTS the accessible entry. That is
# the wiring, and the wiring is the half that ships broken -- M439 said so,
# M536 found it twice more, M542 wrote a driver for exactly this shape.
#
# The tier exports LANG=C LC_ALL=C (see _smoke.sh), so jc_msg_lang_resolve
# yields English deterministically and the literal "Allow?" is safe to match.
printf 'the quick brown fox\n' > "$ws/edit_me.txt"
cat > "$tmp/edit.mm" <<'EOF'
wire openai
rule
  count 1
  usage 4946 37
  tool edit_file {"path":"edit_me.txt","old_string":"quick","new_string":"slow"}
rule
  usage 4946 37
  text i was denied
EOF
# Sync on the tool name rather than on "Allow?": if the prompt is MISSING, the
# assertions below must fail on the log's content with a readable message,
# not time out inside ptydrive on the very string under test.
cat > "$tmp/prompt.pd" <<'EOF'
delay 1200
send "edit edit_me.txt\r"
expect "edit_file" 25
delay 800
send "n"
delay 1500
send "/exit\r"
waitexit 20
EOF

mm_start "$tmp/edit.mm" "$tmp/cap5"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 70 "$SMOKE_TOOLS/ptydrive" \
    --deadline 65 --cols 100 --log "$tmp/p_def.log" "$tmp/prompt.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session \
    > /dev/null 2>&1); rcp=$?
mm_stop

mm_start "$tmp/edit.mm" "$tmp/cap6"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 70 "$SMOKE_TOOLS/ptydrive" \
    --deadline 65 --cols 100 --log "$tmp/p_acc.log" "$tmp/prompt.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --accessible \
    > /dev/null 2>&1); rcq=$?
mm_stop

# --- 9: both prompt runs actually REACHED the prompt (the denominator) -------
# Without this, checks 10 and 11 are clean whenever the tool never ran -- and a
# denied-by-default fence or a mock that answered nothing produces exactly that
# silence.
#
# THE FIRST TWO VERSIONS OF THIS CHECK WERE WRONG, and only the perturbations
# saw either. Both are recorded because they are different mistakes:
#
#   (a) VACUOUS. It matched a bare `denied` -- and the mock's own closing reply
#       is the text "i was denied", so with the tool swapped for a read-only
#       one no prompt rendered at all and the denominator still passed. An
#       assertion satisfied by something other than the thing it names.
#   (b) MODE-BLIND. The replacement matched `tool result: edit_file failed
#       denied`, which exists only in the ACCESSIBLE log: the role-labelled
#       transcript is itself an accessible-mode feature (check 3), and default
#       mode renders the same event as `x error denied`. A pattern taken from
#       one arm's capture and applied to both.
#
# What is printed by BOTH arms is `confirm_echo`'s `-> denied`, which only
# `jc_term_read_key` returning 'n' AT THE PROMPT can produce -- so it proves
# the keypress was consumed by the prompt rather than by the line editor. The
# pattern was checked with grep against both recorded captures, plus two
# negative controls (the prose alone, and a prompt-stripped log), before
# landing here -- CLAUDE.md, "verify a gate's pattern with the gate's own
# tool, against a recorded real response". `-e` because the pattern begins
# with a dash.
# M553 SPLIT THIS BY ARM, because the echo itself is now mode-dependent: the
# sighted form keeps `-> denied` (the arrow is alignment) and the accessible
# form is the bare word plus a period. Asserting both arms separately is
# stronger than asserting a common substring -- it proves each rendering,
# where `denied` alone would pass for either.
if grep -q 'Allow?' "$tmp/p_def.log" \
   && grep -q 'Allow?' "$tmp/p_acc.log" \
   && grep -q -e '-> denied' "$tmp/p_def.log" \
   && grep -q 'denied\.' "$tmp/p_acc.log"; then
    t_ok "both no-auto runs reached the approval prompt and n denied the edit"
else
    t_fail "the approval prompt was never answered (rc def=$rcp acc=$rcq) -- \
checks 10-11 would pass on nothing: \
$(tail -4 "$tmp/p_acc.log" 2>/dev/null | tr '\n' ' ' | head_bytes 200)"
fi

# --- 10: accessible mode: NO brackets, and all five keys still offered -------
# Both halves. Dropping a key is as bad as spelling one out: a listener who is
# not told about `v` cannot view the diff before approving it.
accline=$(grep 'Allow?' "$tmp/p_acc.log" 2>/dev/null | head -1)
ok=1
case "$accline" in *'[y]'*|*'[n]'*|*'[a]'*|*'[e]'*|*'[v]'*) ok=0 ;; esac
for k in y n a e v; do
    case "$accline" in *" $k "*) ;; *) ok=0 ;; esac
done
if [ -n "$accline" ] && [ "$ok" -eq 1 ]; then
    t_ok "accessible approval prompt is bracket-free and offers y/n/a/e/v"
else
    t_fail "accessible prompt still reads as characters, or lost a key: \
[$accline]"
fi

# --- 11: default mode KEPT the bracket form (the control) --------------------
# This is what makes check 10 mean something. Without it, deleting the key list
# altogether would turn check 10 green -- "no brackets" is trivially true of a
# prompt that offers nothing. The sighted rendering must be unchanged.
defline=$(grep 'Allow?' "$tmp/p_def.log" 2>/dev/null | head -1)
if printf '%s' "$defline" | grep -q '\[y\]es' \
   && printf '%s' "$defline" | grep -q '\[v\]iew'; then
    t_ok "default mode keeps the visual bracket form (accessible is a BRANCH)"
else
    t_fail "the sighted prompt lost its bracket key list -- check 10 would \
pass for a build with no key list at all: [$defline]"
fi

# --- 12: the token line is a SENTENCE in accessible mode --------------------
# The operator's own example: "100 input tokens used, and 30 output tokens
# used." `[tokens in=20 out=5]` is five punctuation marks out of nine spoken
# tokens, repeated after EVERY model call.
if grep -q 'input tokens used, and .* output tokens used\.' "$tmp/b.log" \
   && ! grep -q 'tokens in=' "$tmp/b.log"; then
    t_ok "accessible token line is prose, and the bracket form is gone"
else
    t_fail "accessible token line is not a sentence: \
$(grep -o '.*tokens.*' "$tmp/b.log" | head -2 | tr '\n' ' ' | head_bytes 120)"
fi

# --- 13: and the SIGHTED token line is untouched, in BOTH colour states -----
# Without this, deleting the line altogether would turn check 12 green.
#
# TWO CAPTURES, and the first draft of this check had only one -- which the
# perturbation ritual caught. print_token_line has THREE branches: colour,
# accessible, plain. Arm `a` deliberately unsets NO_COLOR (so check 2 can see
# the spinner animate), so it exercises the COLOUR branch and nothing else.
# Rewriting the PLAIN branch to prose left this check green: a sighted user on a
# NO_COLOR terminal would have silently got the accessible rendering with no
# test objecting. `p_def.log` is the sighted run under the tier's NO_COLOR, so
# the two captures together cover all three branches.
if grep -q 'tokens in=' "$tmp/a.log" \
   && ! grep -q 'input tokens used' "$tmp/a.log" \
   && grep -q 'tokens in=' "$tmp/p_def.log" \
   && ! grep -q 'input tokens used' "$tmp/p_def.log"; then
    t_ok "the sighted token line keeps its bracket form, colour and plain"
else
    t_fail "the sighted token line changed -- accessible must be a BRANCH. \
colour arm: $(grep -o '.*tokens.*' "$tmp/a.log" | head -1 | head_bytes 60) / \
plain arm: $(grep -o '.*tokens.*' "$tmp/p_def.log" | head -1 | head_bytes 60)"
fi

# --- 14: the assistant header names the model, and drops the clock ----------
# A wall-clock time read in full on every message is the thing M553 removed;
# the sighted header keeps it, which is what makes this check mean something.
# The timestamp pattern is digits:digits:digits -- the C-locale %X form.
acc_ts=$(grep -cE '[0-9][0-9]:[0-9][0-9]:[0-9][0-9]' "$tmp/b.log" || true)
def_ts=$(grep -cE '[0-9][0-9]:[0-9][0-9]:[0-9][0-9]' "$tmp/a.log" || true)
[ -n "$acc_ts" ] || acc_ts=0
[ -n "$def_ts" ] || def_ts=0
if grep -q 'responds with the following:' "$tmp/b.log" \
   && [ "$acc_ts" -eq 0 ] && [ "$def_ts" -ge 1 ]; then
    t_ok "accessible header is a sentence with no clock ($def_ts timestamps sighted)"
else
    t_fail "header: accessible timestamps=$acc_ts (want 0), sighted=$def_ts \
(want >= 1). A clock read on every message is the defect; removing it from BOTH \
arms is not the fix."
fi

# --- 15: the tool is announced ONCE in accessible mode ----------------------
# It used to be three times: the role label, this glyph line, then the approval
# question. The glyph line is the copy that went. Counting lines that START with
# the run glyph separates it from the readline prompt, which also contains '>'.
accg=$(grep -c '^> edit_file' "$tmp/p_acc.log" || true)
defg=$(grep -c '^> edit_file' "$tmp/p_def.log" || true)
[ -n "$accg" ] || accg=0
[ -n "$defg" ] || defg=0
if [ "$accg" -eq 0 ] && [ "$defg" -ge 2 ]; then
    t_ok "accessible drops the glyph tool line ($defg kept in the sighted arm)"
else
    t_fail "glyph tool lines: accessible=$accg (want 0), sighted=$defg (want \
>= 2). The sighted arm heads its approval block with that line and must keep it."
fi

# --- 16: no thousands separator inside a number, in accessible mode --------
# THE DEFECT, reported by ear on a German-locale reader: `4.946` is spoken "four
# Punkt nine four six", because a punctuation mark inside a numeral defeats the
# reader's number parser -- a listener gets four digits and the name of a dot.
# Not a German problem either: jc_config falls back to `.` when the locale
# supplies no separator, which is exactly what this tier's LC_ALL=C gives, so
# the sighted arm below renders `4.946` on an English system.
#
# 4946 is the smallest interesting figure. Three digits or fewer are never
# grouped, so a driver using the mock's default token counts (20/5) would pass
# on a build with the fix reverted -- which is why the fixture now sets
# `usage 4946 37` explicitly.
if grep -q '4946 input tokens used' "$tmp/b.log" \
   && ! grep -q '4\.946' "$tmp/b.log" \
   && ! grep -q '4,946' "$tmp/b.log"; then
    t_ok "accessible numbers are ungrouped (4946, not 4.946)"
else
    t_fail "a grouped number reached the accessible transcript -- a reader \
speaks the separator and then the digits: \
$(grep -o '[0-9.,]*[0-9] input tokens used' "$tmp/b.log" | head -1)"
fi

# --- 17: and the SIGHTED arm keeps its grouping (the control) ---------------
# Without this, dropping grouping everywhere would turn check 16 green -- and a
# bare six-digit integer is harder to SCAN, which is the opposite audience's
# need. The two want different things; this proves both are served.
if grep -q '4\.946' "$tmp/p_def.log" \
   && ! grep -q 'in=4946' "$tmp/p_def.log"; then
    t_ok "the sighted arm keeps its grouped number (4.946)"
else
    t_fail "the sighted arm lost its thousands separator -- accessible must be \
a BRANCH, and grouping is what makes a long number scannable: \
$(grep -o 'tokens in=[0-9.,]*' "$tmp/p_def.log" | head -1)"
fi

# --- 18: all THREE renderings of the assistant header are distinct ---------
# THE GAP THIS CLOSES (M560). The header has three branches and only two were
# ever checked. The colour branch carries "this is the assistant" in bold cyan
# and the mode's own colour; strip colour and that information was simply gone --
# `m (mock) - chat - 13:05:42`, four fields and two separators, indistinguishable
# from a log line in a piped transcript or a pasted bug report. It now carries
# the word.
#
# Asserted as a THREE-WAY discrimination rather than "the word is present
# somewhere", because each arm must keep its own form: a fix that gave every arm
# the same rendering would pass a presence check while undoing M553 (prose for a
# listener) or M118 (compact and coloured for a scanner).
#
#   a.log      colour on   -> SGR-styled, no literal role word needed
#   p_def.log  NO_COLOR    -> the word "assistant:"
#   b.log      accessible  -> the prose sentence
# The COLOUR arm's clause was vacuous in its first version: it grepped for any
# SGR sequence anywhere in a.log, and the colour arm has SGR on the prompt, the
# spinner and the tool lines -- so stripping the style off the HEADER left it
# green. It now identifies the header line itself (the model name beside a
# clock) and asserts that arm uses neither of the other two forms.
acc_hdr=0; def_hdr=0; col_hdr=0
grep -q 'responds with the following' "$tmp/b.log" && acc_hdr=1
grep -q '^assistant: ' "$tmp/p_def.log" && def_hdr=1
colhdrline=$(grep -E 'mock.*[0-9][0-9]:[0-9][0-9]:[0-9][0-9]' "$tmp/a.log" \
             | head -1)
if [ -n "$colhdrline" ]; then
    case "$colhdrline" in
        *"assistant: "*|*"responds with the following"*) col_hdr=0 ;;
        *) col_hdr=1 ;;
    esac
fi
if [ "$acc_hdr" -eq 1 ] && [ "$def_hdr" -eq 1 ] && [ "$col_hdr" -eq 1 ] &&
   ! grep -q '^assistant: ' "$tmp/b.log"; then
    t_ok "the header renders three ways: styled, worded, prose -- each distinct"
else
    t_fail "header renderings collapsed: accessible-prose=$acc_hdr \
no-colour-word=$def_hdr colour-SGR=$col_hdr, and the accessible arm must NOT use \
the compact word form. Each audience needs its own: a scanner wants the fields, a \
listener wants the sentence, and a NO_COLOR reader needs the role in words \
because colour was carrying it. Headers seen: \
$(grep -hoE '^(assistant: )?[a-z]+ \(mock\)[^ ]*' "$tmp/p_def.log" "$tmp/b.log" 2>/dev/null | head -2 | tr '\n' ' ')"
fi

# --- 19: the accessible PROMPT is short, and keeps only what changes ------
# THE MOST-REPEATED STRING IN A SESSION (M562). `[auto:m:6%] > ` costs a listener
# five spoken symbols -- two brackets, two separators, a percent -- plus the model
# name, every turn, and before M558 on every keystroke. At Orca's punctuation
# level SOME every one of those is voiced, which is M559's finding and this was
# its largest remaining instance.
#
# Asserted as what is KEPT and what is GONE, because either half alone is
# satisfiable by the wrong thing: "no brackets" passes for a prompt that says
# nothing, and "names the mode" passes for the old bracketed form.
pline=$(grep -oE '(auto|chat|plan)[^>]*> ' "$tmp/b.log" 2>/dev/null | head -1)
ok=1
# KEPT: the mode. It is the one segment that changes what a keypress DOES --
# `auto` acts without asking -- so a listener must have it.
case "$pline" in *auto*|*chat*|*plan*) ;; *) ok=0 ;; esac
# GONE: brackets, the model name, and the percentage below the threshold.
#
# THE PERCENT CLAUSE WAS VACUOUS AT FIRST: it looked for a `%` symbol, and the
# accessible form spells the word "percent" out -- precisely because `%` is a
# spoken symbol (M559). So removing the 80% threshold entirely left this check
# green while the prompt read "auto, 8 percent full >". Match the WORD.
grep -qE '\[(auto|chat|plan)' "$tmp/b.log" && ok=0
case "$pline" in *percent*) ok=0 ;; esac
if [ -n "$pline" ] && [ "$ok" -eq 1 ]; then
    t_ok "accessible prompt is [$pline] -- mode kept, brackets/model/percent gone"
else
    t_fail "accessible prompt wrong: [$pline]. It must name the MODE (auto acts \
without asking, so a listener needs it) and must not carry brackets, the model \
name or a percent sign below the 80% threshold. Bracketed prompts found: \
$(grep -cE '\[(auto|chat|plan)' "$tmp/b.log" 2>/dev/null)"
fi

# --- 20: and the SIGHTED prompt is untouched (the control) -----------------
# Without this, shortening the prompt for everyone would turn check 19 green --
# and M296 added the model to the prompt deliberately, after finding the TUI
# named the tier and never the model. That intent survives in accessible mode a
# different way: `Model X responds with the following:` names it on every reply
# (M553), where the prompt named it before every keystroke. For a SIGHTED user
# the prompt is still the place.
if grep -q '\[chat' "$tmp/p_def.log" && grep -qE '\[chat[^]]*%\]' "$tmp/p_def.log"
then
    t_ok "the sighted prompt keeps its brackets, model segment and percentage"
else
    t_fail "the sighted prompt lost its compact form -- accessible must be a \
BRANCH, and M296 put the model there on purpose: \
$(grep -oE '\[.+\]' "$tmp/p_def.log" 2>/dev/null | head -1)"
fi

# --- 21: at the threshold the percentage IS announced ----------------------
# THE THRESHOLD IS THE ONLY POLICY IN M562 -- "say it when it changes something"
# -- and check 19 can only see the quiet side: this fixture runs at about 8% of
# context, so the >= 80 branch was never exercised by anything. An untested
# threshold is exactly the kind of one-line rule that silently inverts.
#
# Driven by shrinking the CONTEXT rather than growing the history: a tiny
# `contextLength` puts the same short session far over 80%, which is cheaper and
# more deterministic than trying to fill a real window.
mm_start "$tmp/replies.mm" "$tmp/cap_thr"
# contextLength is read from the MODEL object, not the top level
# (jc_config.c: jc_json_get_num(model, "contextLength")), so it goes in
# write_config's FOURTH argument. Passing it as the third put it at the
# top level where nothing reads it, and the threshold stayed unreachable.
write_config "$tmp/cfg_thr.json" "$MM_PORT" "" '"contextLength":700'
(cd "$ws" && with_deadline 60 "$SMOKE_TOOLS/ptydrive" \
    --deadline 55 --cols 100 --log "$tmp/thr.log" "$tmp/run.pd" -- \
    "$BIN" --config "$tmp/cfg_thr.json" --no-session --auto --accessible \
    > /dev/null 2>&1)
mm_stop
tline=$(grep -oE '(auto|chat|plan)[^>]*> ' "$tmp/thr.log" 2>/dev/null | head -1)
case "$tline" in
    *"percent full"*) t_ok "at a tiny context the prompt warns: [$tline]" ;;
    *) t_fail "the 80% threshold never announced itself. Prompt at a 700-token \
context was [$tline] -- expected a 'N percent full' notice, because that is the \
point at which compaction rewrites the conversation and a listener has no \
colour to warn them. If this is red the threshold is unreachable or inverted." ;;
esac

# --- 22: ACCESSIBLE **WITH COLOUR** -- the combination nobody ran ----------
# THE BUG THIS CLOSES (M563), and it shipped. print_token_line read
# `if (c->color) ... else if (c->accessible)`, so the prose form was DEAD CODE
# for any accessible user whose terminal had colour -- and accessible mode does
# not imply NO_COLOR. The operator ran the shipped build and got:
#
#     Model chat (jlu/qwen3-coder-next) responds with the following:
#     [tokens in=3960 out=10]          <- should have been prose
#
# WHY EVERY EXISTING CHECK MISSED IT: _smoke.sh exports NO_COLOR=1 for the whole
# tier, and arm `b` inherits it. So accessible mode has never been exercised with
# colour on -- twenty-one checks about accessible rendering, none of them in the
# configuration a real user is most likely to have. That is the M551 shape again
# (eight checks, every arm --auto) and the M558 shape (redraw.py, one mode): an
# instrument that exists and never covers the combination.
#
# This arm exists to cover the COMBINATION, so it asserts the thing the ordering
# decides -- prose present, brackets absent -- and nothing else. Checks 12/13
# still own the NO_COLOR pair.
mm_start "$tmp/replies.mm" "$tmp/cap_ac"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && unset NO_COLOR && with_deadline 60 "$SMOKE_TOOLS/ptydrive" \
    --deadline 55 --cols 100 --log "$tmp/acc_colour.log" "$tmp/run.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --auto --accessible \
    > /dev/null 2>&1)
mm_stop
if grep -q 'input tokens used, and .* output tokens used\.' \
        "$tmp/acc_colour.log" &&
   ! grep -q 'tokens in=' "$tmp/acc_colour.log"; then
    t_ok "accessible + colour still renders prose (the M563 ordering)"
else
    t_fail "accessible mode with COLOUR fell back to the sighted token line -- \
the branch order puts colour before accessible, so the prose form is dead code \
for any accessible user who has not also set NO_COLOR. Found: \
$(grep -oE '(\[tokens in=[^]]*.|[0-9,]+ input tokens used)' "$tmp/acc_colour.log" \
  2>/dev/null | head -1)"
fi

t_done
