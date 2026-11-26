#!/bin/sh
# smoke: per-verb `jichi <verb> --help` prints the verb's OWN section, not the
# top-level help (M1199, G4).
#
# THE DEFECT (reproduced on head, run 0064): src/main.c parsed -h/--help in the
# flag loop BEFORE verb dispatch, so `jichi init --help`, `jichi config set
# --help`, `jichi guide --help` all printed the TOP-LEVEL print_help -- the
# 106-line learner guide in the trial "caught and corrected three of its own
# errors against --help" precisely because per-verb help was unreliable.
# G4's fix: the help text is built ONCE (build_help in main.c) and `<verb>
# --help` extracts that verb's block from it, so the section cannot drift from
# the full help (the M262 lesson: a hand-maintained second copy is a promise
# nobody can keep).
#
# THE LINT: enumerate the verbs from the full --help (its `  <verb>` command
# column) AND from the dispatch sites in main.c (two independent routes), hold
# the two routes to each other, then for EVERY help verb assert:
#   (a) `<verb> --help` exits 0;
#   (b) the output's first line is the verb's OWN block ("  <verb> ...");
#   (c) the output is a SECTION, not the whole help (strictly fewer lines).
# Red on head: every verb reprints the full N-line help, so (b) and (c) fail.
#
# M1199 batch G follow-up (g): three pins the original driver left open.
# (1)+(2) the FULL-HELP dispatch arms -- `jichi --help` (no positional) and
#   `jichi <unknown> --help` -- exit 0 and print the full help (the "no invented
#   refusal" behaviour), not a section.
# (3) a committed GOLDEN of the full --help (golden_help_full.txt) pins the
#   byte-identity claim of build_help: a future drift of the section machinery
#   that reflows the full help trips it. The first line is argv0-relative
#   ("Usage: ./jichi ..."), so the golden and the live output are both normalised
#   to the canonical "Usage: jichi ..." form before the byte comparison.
. "$(dirname "$0")/_smoke.sh"

smoke_home
tmp=$(smoke_tmp)
# t_plan is dynamic: 3 + (number of verbs extracted from --help).

# --- ground truth, route 1: the command column of the full --help --------------
"$BIN" --help </dev/null 2>"$tmp/e_help" > "$tmp/help.txt"; rc_help=$?
if [ "$rc_help" -ne 0 ]; then
    t_plan 1
    t_fail "could not read the full --help (rc=$rc_help) -- nothing to enumerate"
    t_done
fi
# The column: a line of two spaces + a lowercase verb token, inside the
# Commands section (continuation lines carry 25 spaces, so `^  [a-z]` never
# matches them; `  -V`/`  -h` are flags and start with '-'). `  learn analyze`
# and `  context tools` name the verb `learn` / `context` (the first word).
awk '/^Commands:/ { in_c = 1; next }
     in_c && /^  [a-z][a-z0-9-]*/ { print }' "$tmp/help.txt" \
    | sed 's/^  //' | awk '{ print $1 }' | sort -u > "$tmp/verbs_help"
nv=$(grep -c . "$tmp/verbs_help" 2>/dev/null || true)
[ -z "$nv" ] && nv=0
if [ "$nv" -ge 20 ]; then
    t_ok "route 1 (--help command column): $nv verbs"
else
    t_plan 1
    t_fail "route 1 extracted only $nv verbs from --help -- the parse broke and the per-verb cells below would be vacuous"
    t_done
fi

# --- ground truth, route 2: the dispatch sites in main.c -----------------------
# Every verb jichi ACTUALLY dispatches on args.pos[0]: the strcmp/else-if chain.
# This is the second route the note asks for -- the help column is what a human
# reads, the dispatch sites are what a verb means. They must agree as far as
# the help documents (a verb dispatched but not in --help is an M297-class
# defect, subcommands_lint's job; the agreement here is one-directional:
# everything in the help column is a real dispatched verb).
grep -ohE 'strcmp\(args\.pos\[0\], ?"[a-z][a-z0-9-]*"' "$SMOKE_ROOT/src/main.c" \
    | grep -oE '"[a-z][a-z0-9-]*"' | tr -d '"' | sort -u > "$tmp/verbs_dispatch"
nd=$(grep -c . "$tmp/verbs_dispatch" 2>/dev/null || true)
[ -z "$nd" ] && nd=0
# A shrinking dispatch extraction must fail loudly (the M285 discipline).
if [ "$nd" -ge 30 ]; then
    t_ok "route 2 (main.c dispatch sites): $nd verbs"
else
    t_fail "route 2 extracted only $nd verbs from the dispatch sites -- did the dispatch shape change? Fix the extraction, do not lower the floor"
fi

# --- agreement: every --help verb is dispatched (as far as the help documents) -
# The four help-only names (ls, index, embed, rerank) are dispatched through
# other routes (print_prompt / role helpers / subcommand tables), so the honest
# agreement is: the dispatch set covers the help set MINUS those four, and the
# help set is the authoritative enumeration of the per-verb lint.
missing=$(comm -23 "$tmp/verbs_help" "$tmp/verbs_dispatch" \
    | grep -v -x -e ls -e index -e embed -e rerank || true)
if [ -z "$missing" ]; then
    t_ok "help-column verbs are dispatch sites (modulo the four routed elsewhere: ls index embed rerank)"
else
    t_fail "these --help verbs have NO strcmp(args.pos[0], ...) dispatch site:"
    printf '    | %s\n' "$missing"
fi

t_plan $((6 + nv))

# --- per-verb cells: section, not the whole help -------------------------------
N=$(wc -l < "$tmp/help.txt" | tr -d '[:space:]')
first_help=$(head -1 "$tmp/help.txt" 2>/dev/null)
while IFS= read -r v; do
    [ -n "$v" ] || continue
    "$BIN" "$v" --help </dev/null 2>"$tmp/e_$v" > "$tmp/out_$v"; rc=$?
    nlines=$(wc -l < "$tmp/out_$v" | tr -d '[:space:]')
    first=$(head -1 "$tmp/out_$v" 2>/dev/null)
    if [ "$rc" -ne 0 ]; then
        t_fail "$v --help exited $rc (want 0): $(head -1 "$tmp/e_$v" 2>/dev/null)"
        continue
    fi
    # The first line must be the verb's OWN block: exactly two leading spaces,
    # the verb, then a space (a subcommand) or end-of-line (a bare verb). Strip
    # the literal "  <verb>" prefix, then require the next char to be a
    # [[:space:]] or the string to be empty -- exactly the case pattern
    # "  $v "* | "  $v" used before M1199-g. Pure parameter expansion + a
    # whitespace case (no filter fed operands from an expansion), so
    # posix_utils_lint's operand matcher has nothing to misread: the old form
    # put the $v-bearing pattern on its own line as a quoted case, which the
    # matcher's first alternative (quote, whitespace, $) flagged at :98.
    own=0
    if [ "${first#"  $v"}" != "$first" ]; then
        case ${first#"  $v"} in
            '') own=1 ;;
            [[:space:]]*) own=1 ;;
        esac
    fi
    if [ "$nlines" -ge "$N" ]; then
        t_fail "$v --help prints the FULL help ($nlines lines, want a section of the $N-line help) -- the M1199 defect: the flag loop stole --help before verb dispatch"
        continue
    fi
    if [ "$own" -eq 1 ]; then
        t_ok "$v --help prints its own section ($nlines of $N lines, starts '  $v')"
    else
        t_fail "$v --help ($nlines lines) does not start with its own block; first line: [$first]"
    fi
done < "$tmp/verbs_help"

# --- FULL-HELP arm 1: no positional at all --------------------------------------
# `jichi --help` (nothing before the flag) prints the FULL top-level help, exits 0,
# and does NOT invent a refusal -- the section lookup finds no verb, so the full
# help prints. Line count and first line must match the full help captured above.
"$BIN" --help </dev/null 2>"$tmp/e_a1" > "$tmp/out_a1"; rc_a1=$?
na1=$(wc -l < "$tmp/out_a1" | tr -d '[:space:]')
f1_a1=$(head -1 "$tmp/out_a1" 2>/dev/null)
if [ "$rc_a1" -eq 0 ] && [ "$na1" -eq "$N" ] && [ "$f1_a1" = "$first_help" ]; then
    t_ok "--help (no positional) prints the full help (exit 0, $na1 lines, first line is the Usage line)"
else
    t_fail "--help (no positional) must print the full help, no invented refusal (rc=$rc_a1, $na1 of $N lines, first [$f1_a1] vs [$first_help])"
fi

# --- FULL-HELP arm 2: an unknown first word -------------------------------------
# `jichi frobnicate --help`: an unknown word has no Commands block, so the section
# lookup misses and the full help prints -- exit 0, no invented refusal.
"$BIN" frobnicate --help </dev/null 2>"$tmp/e_a2" > "$tmp/out_a2"; rc_a2=$?
na2=$(wc -l < "$tmp/out_a2" | tr -d '[:space:]')
f1_a2=$(head -1 "$tmp/out_a2" 2>/dev/null)
if [ "$rc_a2" -eq 0 ] && [ "$na2" -eq "$N" ] && [ "$f1_a2" = "$first_help" ]; then
    t_ok "frobnicate --help (unknown first word) prints the full help (exit 0, $na2 lines, no invented refusal)"
else
    t_fail "frobnicate --help must print the full help, no invented refusal (rc=$rc_a2, $na2 of $N lines, first [$f1_a2] vs [$first_help])"
fi

# --- FULL-HELP golden: byte-identity of build_help -------------------------------
# The committed golden_help_full.txt is the full --help with its first line
# normalised to the canonical form (the argv0 in "Usage: <argv0>" is the ONLY
# path-dependent byte of the help). Normalise the live help the same way -- set
# line 1 to the canonical form, whatever argv0 this $BIN printed -- and byte-diff.
# A future drift of the section machinery that reflows the full help trips it.
_golden="$SMOKE_DIR/golden_help_full.txt"
sed '1s/.*/Usage: jichi [options] [prompt...]/' "$tmp/help.txt" > "$tmp/help_norm"
if [ -f "$_golden" ] && diff "$tmp/help_norm" "$_golden" > "$tmp/golden_diff" 2>&1; then
    t_ok "full --help is byte-identical to the golden (golden_help_full.txt)"
else
    t_fail "full --help drifted from the golden (build_help reflow, or the golden is stale); diff:
$(head -n 8 "$tmp/golden_diff" 2>/dev/null)"
fi

t_done
