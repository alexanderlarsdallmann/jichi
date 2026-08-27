#!/bin/sh
# smoke: doctor tells you when the interface language and the reading voice
# cannot agree (M567).
#
# THE REPORT, from the operator listening to a German session: "the synthesized
# speech uses English pronunciation for the strings, and that does not sound
# German, at all."
#
# MEASURED with espeak-ng -q -x, which prints phonemes and needs no listening:
#
#   text          English voice        German voice
#   Erlauben?     '3:lO:b@n            Erl'aUb@n      ok
#   1             w'0n     "one"       'aIns  "eins"  ok
#   0             z'i@roU  "zero"      n'Ul   "null"  ok
#   abgelehnt     a#bdZ'EleInt         'apg@l,e:nt    ok
#
# So THE TEXT WAS NEVER THE PROBLEM -- the German voice renders every string
# correctly. The voice was English because the desktop was: LANG=en_US.UTF-8,
# speech-dispatcher's DefaultLanguage commented out, Orca's voices all
# `established: False`. Note the digits: the READER speaks them in ITS language
# while the label is in the TEXT's language, so `1 ja` is heard as "one ya".
#
# WHY THIS IS A doctor CHECK AND NOT A RENDERER FIX. A TERMINAL HAS NO LANGUAGE
# CHANNEL. HTML says lang="de" and a screen reader switches voices; a TTY
# carries bytes and nothing else. An application therefore CANNOT tell a reader
# what language it is printing, and no amount of work on the strings fixes a
# voice mismatch. Same conclusion as the Japanese finding (M556), different
# mechanism: kanji had NO reading, German has the WRONG one. What jichi can do
# is notice the disagreement and say what to set.
#
# WHY IT IS NOT NOISE, which check 5 pins: a mismatch requires a deliberate
# override ($JICHI_LANG or config `language` pointing away from the locale),
# because the locale is otherwise resolve()'s last fallback and agrees with
# itself. The default configuration must stay silent.
#
# LC_ALL MATTERS TO THIS DRIVER MORE THAN TO ANY OTHER, and getting it wrong
# would have made every check below vacuous. _smoke.sh exports LC_ALL=C, and
# jc_locale_is_utf8 reads LC_ALL FIRST -- so with the tier's default every
# non-English catalog falls back to English, `ui` is always JC_MSGL_EN, and the
# mismatch branch is unreachable. Each run therefore sets LC_ALL explicitly.
# No locale needs to EXIST on the host: jc_locale_is_utf8 and match_value both
# only parse the string, which is what keeps this portable.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
G=/usr/bin/grep
[ -x "$G" ] || G=grep
tmp=$(smoke_tmp)

# A config that names no reachable host: `doctor` without --live makes no
# request, and a port nothing listens on keeps that honest.
# `lowResource: false` is pinned because smoke_lint requires it of every inline
# driver config, and the reason applies here: on a low-RAM host jichi reshapes
# the configuration itself (auto-lite), and a reshaped config could change which
# doctor rows appear -- turning a language assertion into a memory-tier
# assertion without saying so.
cat > "$tmp/config.json" <<'EOF'
{ "lowResource": false,
  "models": [ { "name": "chat", "provider": "openai", "model": "local/test",
  "apiBase": "http://127.0.0.1:1/v1", "apiKeyEnv": "JICHI_API_KEY",
  "roles": ["chat"] } ] }
EOF

# `env -u` MUST PRECEDE THE ASSIGNMENTS. Written the other way round it is not
# a warning but an error -- `env: '-u': No such file or directory`, rc 127, no
# output -- and it cost this driver two vacuous passes before the first run:
# check 5 asserts an ABSENCE, and an absence holds trivially in the empty
# output of a command that never ran. That is why doc() now records rc and
# check 1 floors all five captures instead of two.
doc() {   # doc <name> <lc_all> <lang> [jichi_lang]
    name="$1"; shift
    if [ -n "$3" ]; then
        env LC_ALL="$1" LANG="$2" JICHI_LANG="$3" \
            "$BIN" --config "$tmp/config.json" doctor < /dev/null \
            > "$tmp/$name" 2>&1
    else
        env -u JICHI_LANG LC_ALL="$1" LANG="$2" \
            "$BIN" --config "$tmp/config.json" doctor < /dev/null \
            > "$tmp/$name" 2>&1
    fi
    echo $? > "$tmp/$name.rc"
}

doc mismatch en_US.UTF-8 en_US.UTF-8 de
doc agree    de_DE.UTF-8 de_DE.UTF-8 de
doc nonutf8  de_DE       de_DE       ""
doc cjk      ja_JP.UTF-8 ja_JP.UTF-8 ja
doc default  en_US.UTF-8 en_US.UTF-8 ""

# ---- 1: the denominator -- ALL FIVE runs succeeded and said something -----
# Floored at every capture, not one. The first version checked two of five and
# so could not see that three runs had exited 127 without producing a byte --
# which is the state in which checks 4 and 5 pass while testing nothing. Each
# run must exit 0 AND emit one of the three language rows, since exactly one of
# them fires on every path through the check.
bad=""
for f in mismatch agree nonutf8 cjk default; do
    # 0 or 1 are doctor's own verdicts (jc_doctor_exit_code: 1 iff any FAIL),
    # and this fixture deliberately earns a 1 -- no API key, an apiBase nothing
    # listens on. What must NOT appear is a SHELL failure: 127 is the `env`
    # misuse that produced the vacuous passes, 126 an unexecutable binary.
    rc=$(cat "$tmp/$f.rc" 2>/dev/null || echo 999)
    case "$rc" in 0|1) ;; *) bad="$bad $f(rc=$rc)" ;; esac
    $G -q 'locale disagree\|terminal is not UTF-8\|language agrees' \
        "$tmp/$f" || bad="$bad $f(no-language-row)"
done
if [ -z "$bad" ]; then
    t_ok "all five doctor runs reached the language check"
else
    t_fail "run(s) produced nothing to test:$bad -- every absence assertion \
below holds trivially in empty output, so this is the check that keeps the \
rest honest. First bad capture: \
$(head -2 "$tmp/mismatch" 2>/dev/null | tr '\n' ' ' | head_bytes 200)"
fi

# ---- 2: THE REPORTED CASE warns ------------------------------------------
# German interface, English desktop. Both halves: the warning present AND the
# ok row absent, because a build that emitted both would be green on a
# presence-only assertion.
if $G -q 'interface language and the locale disagree' "$tmp/mismatch" &&
   ! $G -q 'interface language agrees' "$tmp/mismatch"
then
    t_ok "a non-English interface on a foreign locale warns"
else
    t_fail "no warning for the interface/locale mismatch -- this is the exact \
configuration that read German text in an English voice. Rows seen: \
$($G -i 'interface language' "$tmp/mismatch" | tr '\n' ' ' | head_bytes 200)"
fi

# ---- 3: agreement does NOT warn -----------------------------------------
if $G -q 'interface language agrees with the locale' "$tmp/agree" &&
   ! $G -q 'disagree' "$tmp/agree"
then
    t_ok "a matching locale is reported ok"
else
    t_fail "a German interface on a German locale should be OK and is not: \
$($G -i 'interface language' "$tmp/agree" | tr '\n' ' ' | head_bytes 200)"
fi

# ---- 4: the two warnings are DISTINCT, not one message for two causes ----
# A non-UTF-8 locale naming a translated language gets English on purpose
# (mojibake reads worse), and that needs different advice from a voice
# mismatch. Asserting the OTHER warning is absent is what makes this a test of
# discrimination rather than of presence.
if $G -q 'terminal is not UTF-8' "$tmp/nonutf8" &&
   ! $G -q 'locale disagree' "$tmp/nonutf8" &&
   $G -q 'Han characters' "$tmp/cjk"
then
    t_ok "the UTF-8 gate and the CJK synthesizer each get their own warning"
else
    t_fail "the two secondary cases are not distinguished. non-UTF-8 run: \
$($G -ic 'not UTF-8' "$tmp/nonutf8"); cjk run: $($G -ic 'Han' "$tmp/cjk"). \
Each cause needs its own advice -- a UTF-8 locale, versus installing a \
synthesizer that has kanji readings."
fi

# ---- 5: THE NOISE GUARD -- the default configuration says nothing --------
# The check earns its place only if it is silent for everybody who did not
# deliberately override the language. If this reddens, every user sees a
# warning and the signal is worthless.
if ! $G -q 'disagree\|not UTF-8\|Han characters' "$tmp/default"; then
    t_ok "no language warning in the default configuration"
else
    t_fail "the default configuration WARNS, which makes the check noise: \
$($G -i 'disagree\|not UTF-8\|Han' "$tmp/default" | tr '\n' ' ' | head_bytes 200)"
fi

# ---- 6: the advice names an action, not just a diagnosis ----------------
# CLAUDE.md: an error names what to do next. A warning that says only "these
# disagree" leaves a blind user to guess which of two things to change.
if $G -q 'LANG' "$tmp/mismatch" && $G -q "reader's voice" "$tmp/mismatch" &&
   $G -q 'open-jtalk' "$tmp/cjk"
then
    t_ok "each warning names the setting to change"
else
    t_fail "a warning diagnoses without prescribing. The mismatch row must \
name BOTH remedies (\$LANG, or the reader's voice) because either is valid, \
and the CJK row must name the package. Mismatch detail: \
$($G -A2 'locale disagree' "$tmp/mismatch" | tr '\n' ' ' | head_bytes 240)"
fi

t_done
