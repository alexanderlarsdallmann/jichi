#!/bin/sh
# smoke lint: a documented jichi invocation must not use a form jichi REFUSES
# (M394).
#
# The M392 documentation review found SEVEN of ~15 copy-pasteable `jichi`
# invocations in the newest tutorials could not work, and the dominant cause was
# this project's own guard from the day before: M375 made `-p` refuse a
# flag-shaped argument (exit 2), and the probe documentation still prescribed
# `jichi -p --no-session "..."` -- so the page telling that bug's story was still
# teaching the bug, in two repositories. A reader alone, with a command that
# exits 2, has no way forward. That is the class this lint owns.
#
# WHAT IS AND IS NOT IN SCOPE, stated rather than implied (the M305 rule):
#
#   * Only FENCED CODE BLOCKS are scanned. That is the principled discriminator,
#     not an exception list (the M295 preference): a broken command inside a
#     fence is a thing a reader COPIES, while the same string in prose is a page
#     TEACHING what not to type -- CHOOSING_A_MODEL and SCRIPTING both quote the
#     refused form deliberately, and must be able to keep doing so.
#   * `jichi -p -` is LEGAL and common (the prompt is stdin; eight pages use it).
#     The guard refuses `-` followed by `-` or an alphanumeric, so a bare dash is
#     fine -- check 4 proves the matcher makes that distinction rather than
#     merely passing today.
#   * Registers and history are excluded (ROADMAP, ANECDOTES, DECISIONS,
#     analysis/, plans/, proposals/): their subject IS the broken invocation, the
#     same reason docs_flags.sh exempts DECISIONS.md.
#   * `--prompt-b64 <b64> <subcommand>` is NOT checked: it is refused at a third
#     layer that M376 recorded as deferred, and no fenced block uses it.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
tmp=$(smoke_tmp)

cd "$SMOKE_ROOT" || exit 1

# The corpus a reader copies from: user-facing pages only.
files=$(ls docs/*.md docs/*/*.md README.md TUTORIAL.md 2>/dev/null \
        | grep -vE '^docs/(ROADMAP|ANECDOTES|DECISIONS)\.md$' \
        | grep -vE '^docs/(analysis|plans|proposals)/')

# One awk pass per corpus: track fence state, and inside a fence flag the two
# shapes jichi refuses. `bad_flag` is the M375 form (-p then a flag); `bad_acp`
# is the M376 collision (--acp with -p in one invocation).
scan() {
    awk '
        # Reset per FILE. Without this the fence state leaks across the corpus:
        # two pages have an odd number of fence lines (a ``` inside a nested or
        # quoted context that this tracker deliberately does not follow), and the
        # leak made the NEXT file scan as if it were entirely inside a fence --
        # which produced this lint first "finding": a prose blockquote in
        # SCRIPTING.md that is teaching the refused form on purpose. Suite state
        # leaking between cases is TEST_INTEGRITY failure mode "never let a
        # suite s own state leak", found here in the lint rather than the suite.
        FNR == 1 { infence = 0 }
        /^[ \t]*```/ { infence = !infence; next }
        !infence { next }
        {
            line = $0
            sub(/^[ \t]*\$ /, "", line)          # drop a shown shell prompt
            # jichi must be in COMMAND POSITION, and the flags examined must be
            # its own. Matching any line containing the word flagged
            # `tmux capture-pane -p -t jichi:0.1` -- a tmux command whose SESSION
            # is named jichi, where the -p belongs to tmux. So: anchor on a
            # command boundary and require whitespace after the name, then test
            # only the substring from there.
            if (!match(line, /(^|[|;&][ \t]*|\([ \t]*)(\.\/)?jichi[ \t]/)) next
            cmd = substr(line, RSTART)
            # -p (or --print) immediately followed by a flag-shaped token:
            # "-" plus "-" or an alphanumeric. A bare "-" (stdin) is legal.
            if (cmd ~ /(-p|--print)[ \t]+-([-A-Za-z0-9])/) {
                printf "%s:%d: %s\n", FILENAME, FNR, line
            }
            else if (cmd ~ /--acp/ && cmd ~ /(-p|--print)[ \t]/) {
                printf "%s:%d: %s\n", FILENAME, FNR, line
            }
        }
    ' "$@"
}

# Count the invocations actually inspected, so a broken fence tracker or a moved
# corpus fails loudly instead of silently checking nothing.
ninv=$(awk '
    FNR == 1 { infence = 0 }
    /^[ \t]*```/ { infence = !infence; next }
    !infence { next }
    {
        line = $0
        sub(/^[ \t]*\$ /, "", line)
        if (match(line, /(^|[|;&][ \t]*|\([ \t]*)(\.\/)?jichi[ \t]/)) n++
    }
    END { print n + 0 }
' $files)

if [ "$ninv" -ge 50 ]; then
    t_ok "inspected $ninv fenced jichi invocations across the reader-facing docs"
else
    t_fail "only $ninv fenced invocations found -- the corpus or the fence tracking moved; fix the extraction, not the floor"
fi

scan $files > "$tmp/bad" 2>/dev/null
if [ ! -s "$tmp/bad" ]; then
    t_ok "no documented invocation uses a form jichi refuses"
else
    t_fail "documented invocation(s) jichi would refuse (exit 2):"
    sed 's/^/    | /' "$tmp/bad"
    printf '    put every flag BEFORE -p (it takes the prompt as its argument);\n'
    printf '    --acp and -p are different run modes.\n'
fi

# --- the matcher itself, both directions (the cache_probe discipline) ---------
cat > "$tmp/probe.md" <<'EOF'
```sh
jichi --no-session -p "fine: flags first"
jichi -p -                     # fine: the prompt is stdin
cat x | jichi -q --readonly -p -
jichi -p --no-session "BAD_FLAG_AFTER_P"
jichi --acp -p "BAD_ACP_WITH_P"
```
Prose outside a fence: `jichi -p --no-session "..."` must NOT be flagged.
EOF
hits=$(scan "$tmp/probe.md" | grep -c .)
flagged_bad=$(scan "$tmp/probe.md" | grep -c 'BAD_')
if [ "$hits" -eq 2 ] && [ "$flagged_bad" -eq 2 ]; then
    t_ok "the matcher flags both refused forms and nothing else (2/2)"
else
    t_fail "matcher wrong: $hits hits, $flagged_bad of them the planted bad ones (want 2 and 2)"
    scan "$tmp/probe.md" | sed 's/^/    | /'
fi

if scan "$tmp/probe.md" | grep -q 'stdin'; then
    t_fail "the matcher flagged 'jichi -p -', which is legal (prompt from stdin)"
else
    t_ok "the matcher leaves 'jichi -p -' and prose mentions alone"
fi

# --- 5: the documented FORMS are executed, not only parsed (M407) ------------
# Checks 1-4 are static, and subcommands_lint.sh checks 7-11 (M326e) already RUN
# every advertised subcommand bare. Between the two sits a small uncovered strip:
# the flag-carrying forms the docs actually print. `jichi assignments --output
# json` is a different invocation from `jichi assignments`, and the M375 defect --
# the one this whole driver exists for -- was a FORM defect, not a name defect.
#
# ITS REACH IS SIX FORMS, and that is stated rather than dressed up. A sweep for
# documented forms that are safe to execute here (a read-only verb, at least one
# flag, no shell metacharacters, no <placeholder>) yields exactly six. Four of them
# legitimately exit non-zero because the fixture HOME has no sessions and no
# telemetry logs -- so the assertion is the one that holds regardless of state:
# a documented form must never answer with a usage dump or an error line. That is
# precisely the M375 signature.
#
# A separate driver was written for this and deleted: the bare-verb tier already
# existed, and a new file for six forms would have been a duplicate wearing a new
# name. The measurement that killed it is in docs/DEFERRED.md.
tmpc=$(smoke_tmp)
cat > "$tmpc/config.json" <<'CFGEOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:9/v1","apiKeyEnv":"JC_SMOKE_ABSENT_KEY",
"roles":["chat"]}],"snapshots":false,"repoMap":false,"lowResource":false,
"maxRetries":0}
CFGEOF
HOME=$tmpc; export HOME              # isolate: no developer sessions or logs

forms=$(awk '
    FNR == 1 { infence = 0 }
    /^[ \t]*```/ { infence = !infence; next }
    !infence { next }
    /^[ \t]*#/ { next }
    /^[ \t]*(\.\/)?jichi[ \t]/ {
        sub(/^[ \t]*/, ""); sub(/^\.\//, ""); sub(/[ \t]*#.*$/, "")
        print
    }' $files |
  grep -E '^jichi (assignments|ls|export|telemetry|runs|audit|context|config|models|status|board|constraints|attempts|checkpoints) ' |
  grep -- '--' | grep -vE '[|<>&`$\\]' | sort -u)

nforms=$(printf '%s\n' "$forms" | grep -c . || true)
: > "$tmpc/bad"
printf '%s\n' "$forms" | while read -r f; do
    [ -n "$f" ] || continue
    args=${f#jichi }
    out=$(with_deadline 40 "$BIN" --config "$tmpc/config.json" $args \
          < /dev/null 2>&1); rc=$?
    # EXIT 2 is the signal, not the text. A rejected invocation prints
    # `error: --since requires a duration` on stderr and exits 2; it prints no
    # `Usage:` and no `[jichi error]`, which is what the first version of this
    # check looked for -- and it was TOOTHLESS under a real perturbation as a
    # result. Exit 1 is kept legal on purpose: four of these six forms exit 1
    # because the fixture HOME has no sessions and no telemetry logs, which is
    # correct behaviour and not the class being guarded.
    [ "$rc" -eq 2 ] && echo "$f (exit 2: $(printf '%s' "$out" | head -1))" >> "$tmpc/bad"
    case "$out" in
        Usage:*) echo "$f (usage dump)" >> "$tmpc/bad" ;;
    esac
    true
done
# M579: THE DENOMINATOR, and the full mutant sweep is what demanded it. Every
# assertion above is an ABSENCE -- no exit 2, no "Usage:" dump -- and a binary
# that prints nothing and exits 0 satisfies all of them. This driver therefore
# stayed GREEN against a mute product, which is the definition of measuring its
# own fixtures.
#
# The fix is to prove the thing under test is alive before believing anything
# about it: the real jichi answers --version with its own name. A mute binary
# answers with nothing and reddens here instead of passing everything.
bin_ok=$(with_deadline 20 "$BIN" --version < /dev/null 2>&1 | grep -c '^jichi ' || true)
[ -n "$bin_ok" ] || bin_ok=0
if [ "$bin_ok" -lt 1 ]; then
    t_fail "the binary under test does not identify itself -- \
\`$BIN --version\` printed no \"jichi\" line, so every absence assertion below \
is satisfied by silence rather than by correct behaviour. This is the check the \
full mutant sweep asked for (M579)."
elif [ "$nforms" -ge 5 ] && [ ! -s "$tmpc/bad" ]; then
    t_ok "$nforms documented flag-carrying forms run without being rejected (exit 2)"
elif [ "$nforms" -lt 5 ]; then
    t_fail "extracted only $nforms documented forms -- the scan broke; fix it rather than the floor"
else
    t_fail "documented form answered with usage/error: $(tr '\n' ' ' < "$tmpc/bad")"
fi

t_done