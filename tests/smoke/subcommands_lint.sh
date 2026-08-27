#!/bin/sh
# smoke lint: every subcommand jichi dispatches must appear in --help, and every
# subcommand --help names must dispatch (M297).
#
# M295 found `learn corrections` missing from print_help -- M294 had added the
# subcommand and its usage error but not the help text, and nothing covered it:
# docs_flags.sh lints --flag spellings, not subcommands. Measuring the gap before
# building this found it was not isolated. `board`, `packages` and `benchmark` were
# dispatched, worked when run, and appeared in NEITHER --help nor the man page; four
# second-level verbs (`mcp resources`, `mcp prompts`, `packages recommend`,
# `learn review`) were dispatched and named nowhere in --help.
#
# A working feature nobody can discover is a feature that was not shipped. This is
# the last user-facing namespace with no check, after docs_flags.sh (flags),
# tool_names_lint.sh (tool names), builtin_cmds_lint.sh (the slash-command
# registry) and slash_commands_lint.sh (slash-command mentions).
#
# TWO DIRECTIONS, because either alone would have called the extraction complete.
# A first pass at the ground truth used only `strcmp(args.pos[0], "x")`, got 47
# names, and MISSED FOUR: `embed`, `rerank` and `index` dispatch through a predicate
# helper, and `ls` through `strcmp(args.print_prompt, "ls")`. That surfaced only by
# diffing against --help -- so this lint checks dispatched-but-undocumented AND
# documented-but-not-dispatched, and floors both extractions.
#
# TWO LEVELS, because the motivating defect was at the second. `learn corrections`
# is a second-level omission, so a top-level-only lint would again be quiet on the
# string that prompted it -- the trap M295 hit and had to fix mid-implementation.
#
# WHY THE SECOND LEVEL MATCHES THE WHOLE HELP LINE, not the command column. jichi's
# help documents some verbs in the description rather than the command column
# (`control <sock> <verb>  Steer ... : status | inject <text> | pause`), which is
# legitimate. A column-only rule reported 15 findings, 11 of them that formatting
# choice or simply a command list longer than 25 columns. Matching the full help
# text brought it to the 4 real ones. A rule that produces mostly false findings
# gets the whole lint ignored (the M203/M285 reasoning).
#
# NOT IN SCOPE: the man page. 24 of the 47 top-level subcommands are absent from
# `man jichi.1`'s .SH COMMANDS, but the man page is DELIBERATELY terse --
# docs_flags.sh encodes that with a TERSE table of consciously-deferred flags. The
# equivalent triage for subcommands is a reviewed editorial decision, not a
# mechanical one, and is left to its own pass rather than guessed at here. `--help`
# is different in kind: it is the binary's own self-description, so an omission is
# undiscoverable by construction, and this lint FAILS on it. (The M284 asymmetry:
# unresolvable = FAIL, under-documented-by-policy = not this lint's business.)
#
# CHECKS 7-11 (M326e): does an advertised verb ANSWER, or merely resolve? Checks 1-6
# passed throughout the M326c defect -- `config path` and `config validate` were
# dispatched (twice, in fact: a second handler shadowed the one serving them) and
# named in --help, and replied "config editing is off". Dispatch is not behaviour.
# Every subcommand --help offers is now RUN bare in a throwaway workspace and must
# exit 0 with something on stdout, and so is every second-level verb that can be
# paired to its parent.
#
# The top-level run alone was NOT enough, and that was measured rather than assumed:
# against the pre-fix binary checks 7-8 pass, because bare `config` defaults to
# `show`, which worked. Only the second-level run (checks 9-10) reproduces it. Same
# trap as the M295 note above, one layer down.
#
# SCOPE, stated rather than implied: only subcommands and verbs that are meaningful
# with no arguments, offline, and without side effects. The rest are excluded BY NAME
# in the tables below, each with its reason -- and check 11 fails if an excluded name
# stops being a subcommand, so a rename cannot leave a stale exclusion quietly
# masking a real one. New subcommands are IN scope by default: the exclusion is the
# thing you must write, which is the right way round for a lint whose failure mode is
# silence.
. "$(dirname "$0")/_smoke.sh"

t_plan 11
smoke_home
tmp=$(smoke_tmp)
root="$SMOKE_ROOT"

# --- ground truth: what the binary dispatches ----------------------------------
# Shape 1: the common `strcmp(args.pos[0], "x")`.
grep -ohE 'strcmp\(args\.pos\[0\], "[a-z0-9-]+"\)' "$root/src/main.c" \
    | grep -oE '"[a-z0-9-]+"' | tr -d '"' > "$tmp/l1_raw"
# Shape 2: a predicate helper over a `cmd` parameter (embed/rerank/index).
grep -ohE 'strcmp\(cmd, "[a-z0-9-]+"\)' "$root/src/main.c" \
    | grep -oE '"[a-z0-9-]+"' | tr -d '"' >> "$tmp/l1_raw"
# Shape 3: `ls`, reached through print_prompt rather than a positional. The name
# must START with a letter: this comparison is also how `-p -` (read the prompt
# from stdin) is spelled, and `-` is not a subcommand.
grep -ohE 'strcmp\(args\.print_prompt, "[a-z][a-z0-9-]*"\)' "$root/src/main.c" \
    | grep -oE '"[a-z][a-z0-9-]*"' | tr -d '"' >> "$tmp/l1_raw"
sort -u "$tmp/l1_raw" > "$tmp/l1"
n1=$(grep -c . "$tmp/l1" || true)

# Second level: two variable names, enumerated like tool_names_lint's two shapes.
grep -ohE 'strcmp\((sub|verb), "[a-z0-9-]+"\)' "$root/src/main.c" \
    | grep -oE '"[a-z0-9-]+"' | tr -d '"' > "$tmp/l2_raw"
# M511: and the THIRD shape at this level -- a verb compared straight off
# args.pos[1] with no local. Five were outside the universe this lint claims
# ("TWO LEVELS", above): `mcp call`, `constraints scan`, `context tools`,
# `context history`, `checkpoints gc`. All five are in --help today, so nothing
# was undiscoverable; what was missing was the universe, which is how the next
# one would have got in unseen. Found by enumerating every string-comparison
# target in main.c and diffing against what this extraction matches.
grep -ohE 'strcmp\(args(\.|->)pos\[1\], "[a-z0-9-]+"\)' "$root/src/main.c" \
    | grep -oE '"[a-z0-9-]+"' | tr -d '"' >> "$tmp/l2_raw"
# `-` is an argument convention (read from stdin), not a verb: `export -`,
# `brief-check -`. The character class above already excludes it; this line
# records that the exclusion is deliberate rather than lucky.
grep -E '^[a-z][a-z0-9-]*$' "$tmp/l2_raw" | sort -u > "$tmp/l2"
n2=$(grep -c . "$tmp/l2" || true)

# A shrinking extraction must fail LOUDLY: if a dispatch shape changes, the universe
# empties and this lint passes while checking nothing (the M285 discipline).
if [ "$n1" -ge 45 ] && [ "$n2" -ge 25 ]; then
    t_ok "dispatch: $n1 top-level subcommands, $n2 second-level verbs"
else
    t_fail "suspiciously few subcommands extracted (top=$n1 second=$n2) -- did a
 dispatch shape change? Fix the extraction; do not relax the floor"
fi

# --- what --help names ---------------------------------------------------------
"$BIN" --help > "$tmp/help.txt" 2>&1 </dev/null || true
# The command column, for the top-level check: a subcommand must be OFFERED, not
# merely mentioned in some description.
grep -E '^  [a-z]' "$tmp/help.txt" | sed 's/^  //' \
    | grep -oE '^[a-z][a-z0-9-]*' | sort -u > "$tmp/help_col"
# The whole text, for the second-level check (see the header note).
grep -oE '[a-z][a-z0-9-]+' "$tmp/help.txt" | sort -u > "$tmp/help_all"
nh=$(grep -c . "$tmp/help_col" || true)
if [ "$nh" -ge 40 ]; then
    t_ok "--help offers $nh subcommands in its command column"
else
    t_fail "only $nh subcommands parsed out of --help -- the parse is broken, and
 a broken parse makes this lint pass by comparing nothing"
fi

# --- check 1: dispatched => documented (the sharp one) -------------------------
comm -23 "$tmp/l1" "$tmp/help_col" > "$tmp/undoc"
nu=$(grep -c . "$tmp/undoc" || true)
if [ "$nu" -eq 0 ]; then
    t_ok "every dispatched subcommand appears in --help"
else
    t_fail "$nu subcommand(s) dispatched but absent from --help -- undiscoverable
 except by reading main.c:"
    sed 's/^/    | jichi /' "$tmp/undoc"
fi

# --- check 2: documented => dispatched ----------------------------------------
# The direction that caught this lint's own incomplete extraction. A name in the
# command column that nothing dispatches is either a stale help line or -- more
# likely -- a dispatch shape the extraction above does not know about.
comm -13 "$tmp/l1" "$tmp/help_col" > "$tmp/undisp"
nd=$(grep -c . "$tmp/undisp" || true)
if [ "$nd" -eq 0 ]; then
    t_ok "every subcommand --help offers is actually dispatched"
else
    t_fail "$nd name(s) in --help that nothing dispatches (a stale help line, or a
 dispatch shape this lint cannot see -- check which before deleting anything):"
    sed 's/^/    | jichi /' "$tmp/undisp"
fi

# --- check 3: second-level verbs are named somewhere in --help -----------------
comm -23 "$tmp/l2" "$tmp/help_all" > "$tmp/l2_undoc"
n2u=$(grep -c . "$tmp/l2_undoc" || true)
if [ "$n2u" -eq 0 ]; then
    t_ok "every second-level verb is named in --help"
else
    t_fail "$n2u second-level verb(s) dispatched but named nowhere in --help:"
    sed 's/^/    | /' "$tmp/l2_undoc"
fi

# --- the lint's own teeth ------------------------------------------------------
# A lint nobody has watched fail is a lint nobody has watched work. Plant both
# directions against a copy of the real help output, so the comparison logic is the
# one under test rather than a lookalike.
cp "$tmp/help_col" "$tmp/planted"
grep -v '^board$' "$tmp/planted" > "$tmp/planted.1" && mv "$tmp/planted.1" "$tmp/planted"
echo "frobnicate" >> "$tmp/planted"
sort -u "$tmp/planted" > "$tmp/planted.s" && mv "$tmp/planted.s" "$tmp/planted"
_a=$(comm -23 "$tmp/l1" "$tmp/planted" | grep -c .)   # board: dispatched, undocumented
_b=$(comm -13 "$tmp/l1" "$tmp/planted" | grep -c .)   # frobnicate: documented, absent
if [ "$_a" -eq 1 ] && [ "$_b" -eq 1 ]; then
    t_ok "both directions detect a planted omission and a planted phantom"
else
    t_fail "the comparison missed a planted case (undoc=$_a phantom=$_b, want 1/1)"
fi

# --- checks 4-5: an advertised verb must ANSWER, not merely resolve ------------
# Excluded from the bare run, with the reason. Anything not listed here is expected
# to work with no arguments, offline, in an empty workspace.
cat > "$tmp/excluded" <<'EOF'
assign
attempt
complete
control
embed
fim
grade
hint
prune
rerank
setup
test
workflow
recover
init
undo
rewind
export
checkpoints
index
learn
dream
improve
telemetry
serve
daemon
doctor
brief-check
EOF
#   assign attempt complete control embed fim grade hint prune rerank setup test
#     workflow recover brief-check -- require an argument (a spec file, a query, a
#     socket, a prompt, a commit, a brief). `brief-check` has no sensible bare
#     default: guessing which file is the brief is the kind of helpfulness a
#     pre-flight must not have, since the whole point is to check the exact text
#     that will be sent. `recover` is deliberately NOT given a bare-invocation
#     default: it materialises a discarded state, and guessing WHICH one is the
#     kind of helpfulness a recovery command must not have. `attempts` is the
#     no-argument half, and it answers, so it is not muted.
#   init undo rewind export checkpoints index learn dream improve -- mutate the
#     workspace, or need state a fresh one does not have (a session, a git repo,
#     an embed model, a draft).
#   telemetry -- reports on a log that does not exist yet; "no data" is its answer
#     and it says so on stderr with a non-zero exit.
#   serve daemon -- long-running servers; they do not answer and exit.
#   doctor -- its exit code is a VERDICT, not a status: exit 1 means it found a
#     problem, which is doctor working. Covered by tests/smoke/doctor.sh instead.
sort -u "$tmp/excluded" > "$tmp/excluded.s" && mv "$tmp/excluded.s" "$tmp/excluded"

# A throwaway workspace + an unreachable local server: nothing here should need the
# network, and a subcommand that tries fails fast on a refused connection.
runws=$(smoke_tmp)
cat > "$tmp/run.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:1/v1","apiKey":"x"}],
"snapshots":false,"repoMap":false,"references":false,"lowResource":false}
EOF

comm -23 "$tmp/help_col" "$tmp/excluded" > "$tmp/inscope"
ns=$(grep -c . "$tmp/inscope" || true)
: > "$tmp/mute"
for c in $(cat "$tmp/inscope"); do
    out=$(cd "$runws" && with_deadline 20 "$BIN" --config "$tmp/run.json" "$c" \
          < /dev/null 2>/dev/null)
    rc=$?
    if [ "$rc" -ne 0 ] || [ -z "$out" ]; then
        echo "$c (exit $rc, $(printf '%s' "$out" | wc -c) bytes on stdout)" \
            >> "$tmp/mute"
    fi
done
nm=$(grep -c . "$tmp/mute" || true)

if [ "$ns" -ge 20 ]; then
    t_ok "$ns advertised subcommands are in scope for the bare run"
else
    t_fail "only $ns subcommands in scope -- the help parse or the exclusion table
 grew wrong, and a shrinking scope makes this check pass by running nothing"
fi

if [ "$nm" -eq 0 ]; then
    t_ok "every in-scope subcommand answers when run with no arguments"
else
    t_fail "$nm advertised subcommand(s) resolve but do not answer -- either fix
 the subcommand or add it to the exclusion table WITH a reason:"
    sed 's/^/    | jichi /' "$tmp/mute"
fi

# --- checks 10-11: SECOND-LEVEL verbs must answer too --------------------------
# Checks 7-9 would NOT have caught M326c, and that was measured rather than
# assumed: run against the pre-fix binary they pass, because bare `config`
# defaults to `show`, which worked. The defect was `config path` -- a verb. This
# is the same trap the header records M295 hitting: the motivating defect is at
# the second level, so a top-level-only check is quiet on the very thing that
# prompted it.
#
# Pairs are derived, not listed: awk walks main.c tracking the most recent
# top-level dispatch and attaches each following strcmp(sub|verb, "...") to it.
# LIMIT, stated: this sees the sub/verb dispatch shape only. Verbs read straight
# out of args.pos[1] (mcp's, for one) are not paired here -- they are covered by
# check 5 (named in --help) but not run. A partial check with a stated boundary,
# not a claim on the namespace.
awk '
  {
    line=$0
    if (match(line, /strcmp\(args\.pos\[0\], "[a-z0-9-]+"\)/)) {
      seg=substr(line, RSTART, RLENGTH)
      if (match(seg, /"[a-z0-9-]+"/)) { parent=substr(seg, RSTART+1, RLENGTH-2) }
      next
    }
    if (match(line, /strcmp\((sub|verb), "[a-z0-9-]+"\)/)) {
      seg=substr(line, RSTART, RLENGTH)
      if (match(seg, /, "[a-z0-9-]+"\)/)) {
        v=substr(seg, RSTART+3, RLENGTH-5)
        if (parent != "") print parent " " v
      }
    }
  }
' "$root/src/main.c" | sort -u > "$tmp/pairs_all"

# Verbs that cannot be run bare, with the reason.
cat > "$tmp/pairs_excluded" <<'EOF'
config set
config telemetry
learn analyze
learn apply
learn corrections
learn review
EOF
#   config set / config telemetry -- take arguments and WRITE the config.
#   learn apply / corrections / review -- need a reviewed draft, and mutate
#     memory/skills when they find one.
#   learn analyze -- reports on telemetry a fresh HOME has none of.
sort -u "$tmp/pairs_excluded" > "$tmp/pe.s" && mv "$tmp/pe.s" "$tmp/pairs_excluded"

comm -23 "$tmp/pairs_all" "$tmp/pairs_excluded" > "$tmp/pairs_in"
np=$(grep -c . "$tmp/pairs_in" || true)
: > "$tmp/pairs_mute"
# Read pairs a line at a time: each is two words and must stay two words.
while read -r parent verb; do
    [ -n "$parent" ] || continue
    out=$(cd "$runws" && with_deadline 20 "$BIN" --config "$tmp/run.json" \
          "$parent" "$verb" < /dev/null 2>/dev/null)
    rc=$?
    if [ "$rc" -ne 0 ] || [ -z "$out" ]; then
        echo "$parent $verb (exit $rc, $(printf '%s' "$out" | wc -c) bytes)" \
            >> "$tmp/pairs_mute"
    fi
done < "$tmp/pairs_in"
npm=$(grep -c . "$tmp/pairs_mute" || true)

if [ "$np" -ge 4 ]; then
    t_ok "$np second-level verbs in scope for the bare run"
else
    t_fail "only $np second-level verbs in scope -- the awk pairing broke, and a
 pairing that finds nothing makes this check pass by running nothing"
fi

if [ "$npm" -eq 0 ]; then
    t_ok "every in-scope second-level verb answers"
else
    t_fail "$npm advertised verb(s) resolve but do not answer (M326c was exactly
 this: dispatched, in --help, and replying with an error):"
    sed 's/^/    | jichi /' "$tmp/pairs_mute"
fi

# The exclusion table cannot outlive what it excludes: a renamed subcommand would
# leave a stale line here, silently taking the new name out of scope forever.
comm -23 "$tmp/excluded" "$tmp/help_col" > "$tmp/stale"
nst=$(grep -c . "$tmp/stale" || true)
if [ "$nst" -eq 0 ]; then
    t_ok "no stale entries in the exclusion table"
else
    t_fail "$nst excluded name(s) are no longer subcommands --help offers; drop
 them so the real names come back into scope:"
    sed 's/^/    | /' "$tmp/stale"
fi

t_done
