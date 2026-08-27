#!/bin/sh
# smoke lint: the man page covers what the binary accepts (M539).
#
# THE DEFECT. man/jichi.1 documented 77 of the 148 long flags the parser accepts --
# 49% -- and 38 of the 55 subcommands, and advertised one command, `session`, that
# does not exist. Its EXIT STATUS section listed four codes where `describe`,
# `--help`, docs/EMBEDDING.md and docs/SCRIPTING.md all list five, the missing one
# being 143: a supervisor reading only the man page would treat a clean SIGTERM
# shutdown as an unknown failure.
#
# None of that is neglect. It is the predictable state of the last user-facing
# namespace with no parity lint: flags have docs_flags, subcommands have
# subcommands_lint, tool names have tool_names_lint, slash commands have two, and
# `describe` got one at M538. The man page had none, so nothing compared it to the
# binary it claims to describe. subcommands_lint's own header even records commands
# that appeared in "NEITHER --help nor the man page" -- noticed, and not gated.
#
# `session` is the same string that caused M537's incident, in a second place: it is
# the --session FLAG, never a subcommand, and `jichi session` falls through to being
# sent to the model as a prompt. Two documents named it as a command. ANECDOTES #66.
#
# THE UNIVERSE, AND THE GROUND TRUTH IT USES.
#   flags    -- every `strcmp(a, "--x")` in the parser. 148 today.
#   commands -- `--help`'s command column, NOT a grep of main.c: a grep of
#               `strcmp(args.pos[0], "x")` misses export, checkpoints, recover,
#               undo and rewind, which dispatch through other paths.
#               subcommands_lint records the same trap costing it four names, and
#               this driver's author walked into it again while measuring.
#   exit     -- the codes `describe` lists, which is the surface EMBEDDING.md
#               declares stable.
#
# ROFF ESCAPING IS THE TRAP IN THIS FILE. A man page writes every hyphen as `\-`,
# so a pattern matched against the raw source truncates `\-\-budget\-tokens` to
# `--budget`. That produced a first measurement of "61 documented, 17 phantoms"
# where the truth was 77 and 0, and it recurred THREE TIMES in one session -- once
# in this lint, once measuring for the ROADMAP, once floor-checking the generator.
# Every extraction here de-roffs FIRST and matches second.
#
# THERE IS NO EXEMPTION TABLE, deliberately. The plan for this lint had one: a
# commented list of flags allowed to stay undocumented. It turned out to be
# unnecessary -- writing all 148 was one pass with
# scripts/man-options-from-help.sh, which derives the roff from --help's already
# reviewed text. An exemption table nobody needs is a place for future omissions to
# hide, so the floor is simply "all of them".
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
G=/usr/bin/grep
[ -x "$G" ] || G=grep
MAN="$SMOKE_ROOT/man/jichi.1"
SRC="$SMOKE_ROOT/src/main.c"

# de-roffed once, reused by every check below
sed 's/\\-/-/g' "$MAN" > "$tmp/man.txt"
with_deadline 60 "$BIN" --help < /dev/null > "$tmp/help.txt" 2>&1
with_deadline 60 "$BIN" describe --output json < /dev/null > "$tmp/d.json" 2>&1

"$G" -o 'strcmp(a, "--[a-z0-9-]*")' "$SRC" \
    | sed 's/.*"\(--[a-z0-9-]*\)".*/\1/' | sort -u | "$G" -vx -- '--' > "$tmp/real.f"
"$G" -o -- '--[a-z0-9][a-z0-9-]*' "$tmp/man.txt" | sort -u > "$tmp/man.f"
"$G" -o -- '--[a-z0-9][a-z0-9-]*' "$tmp/help.txt" | sort -u > "$tmp/help.f"
sed -n '/^Commands:/,/^Exit codes/p' "$tmp/help.txt" \
    | "$G" -oE '^  [a-z][a-z-]*' | sed 's/^  //' | sort -u > "$tmp/help.c"
awk '/^\.SH COMMANDS/,/^\.SH INTERACTIVE/' "$tmp/man.txt" \
    | "$G" -oE '^\.B [a-z][a-z-]*' | sed 's/^\.B //' | sort -u > "$tmp/man.c"

# ---- 1: the extractions found something (the floor) -----------------------
# Every check below is a set difference, and a set difference against an EMPTY set
# is clean by construction. The roff-escaping trap above makes an empty or
# truncated extraction the LIKELY failure here, not an unlikely one, so the sizes
# are asserted first and floored at today's counts.
nr=$("$G" -c . "$tmp/real.f"); nm=$("$G" -c . "$tmp/man.f")
nhc=$("$G" -c . "$tmp/help.c"); nmc=$("$G" -c . "$tmp/man.c")
if [ "$nr" -ge 148 ] && [ "$nm" -ge 148 ] && [ "$nhc" -ge 55 ] && [ "$nmc" -ge 55 ]
then
    t_ok "extracted $nr parser flags, $nm man flags, $nhc/$nmc commands (floors 148/148/55/55)"
else
    t_fail "extraction floors missed: parser=$nr man=$nm help-cmds=$nhc \
man-cmds=$nmc (want >= 148/148/55/55). Fix the extraction, do not lower the floor \
-- and check the roff escaping first, which is what broke it three times."
fi

# ---- 2: every flag the parser accepts is in the man page -----------------
miss=$(comm -23 "$tmp/real.f" "$tmp/man.f" | tr '\n' ' ')
if [ -z "$miss" ]; then
    t_ok "all $nr parser flags are documented"
else
    t_fail "flags the parser accepts and the man page does not mention: $miss"
fi

# ---- 3: no flag in the man page that the parser rejects ------------------
# The other direction. Both are needed: either alone would have called the
# extraction complete, which is the lesson subcommands_lint paid four names for.
ghost=$(comm -13 "$tmp/real.f" "$tmp/man.f" | tr '\n' ' ')
if [ -z "$ghost" ]; then
    t_ok "no phantom flags (every documented flag is accepted)"
else
    t_fail "the man page documents flags the parser rejects: $ghost"
fi

# ---- 4: the command lists agree in both directions ----------------------
cmiss=$(comm -23 "$tmp/help.c" "$tmp/man.c" | tr '\n' ' ')
cghost=$(comm -13 "$tmp/help.c" "$tmp/man.c" | tr '\n' ' ')
if [ -z "$cmiss" ] && [ -z "$cghost" ]; then
    t_ok "all $nhc subcommands documented, and none invented"
else
    t_fail "undocumented:${cmiss:- none} / phantom:${cghost:- none} -- a phantom \
here is what named a nonexistent \`session\` command in two places"
fi

# ---- 5: the safety off-switches are in the man page AND --help ----------
# NO EXEMPTION. These turn a guard OFF, and every one of them was accepted by the
# parser while appearing in no help output and no man page -- an operator could
# not discover that the guard existed, let alone that a run had disabled it.
# --no-preserve-discarded is the sharpest: it removes the `jichi recover <hash>`
# handle that makes an `undo` recoverable, which is the net M537's incident landed
# in. The list is explicit rather than pattern-matched on `--no-`, because
# --no-color and --no-markdown are preferences, not guards, and a pattern that
# cannot tell those apart would either miss --config-editable and --yes or drown
# the check in display flags.
SAFETY='--no-preserve-discarded --no-rollback --no-revert-out-of-scope
--no-self-review --no-strict-green --config-editable --yes'
bad=""
for f in $SAFETY; do
    "$G" -qx -- "$f" "$tmp/man.f" || bad="$bad man:$f"
    "$G" -qx -- "$f" "$tmp/help.f" || bad="$bad help:$f"
done
nsafe=$(printf '%s\n' $SAFETY | "$G" -c .)
if [ -z "$bad" ] && [ "$nsafe" -ge 7 ]; then
    t_ok "all $nsafe safety off-switches appear in both the man page and --help"
else
    t_fail "a guard can be disabled by a flag that is not documented where the \
operator would look:$bad (list size $nsafe, floor 7)"
fi

# ---- 6: EXIT STATUS names every code describe declares -----------------
# The exit codes are a STABLE interface (docs/EMBEDDING.md). 143 was missing from
# this page while four other surfaces carried it.
ecodes=$("$G" -o '"name":"[0-9]*"' "$tmp/d.json" | sed 's/.*"\([0-9]*\)".*/\1/' \
         | sort -un | tr '\n' ' ')
emiss=""
for c in $ecodes; do
    awk '/^\.SH EXIT STATUS/,/^\.SH FILES/' "$tmp/man.txt" \
        | "$G" -q "^\.B $c\$" || emiss="$emiss $c"
done
ne=$(printf '%s\n' $ecodes | "$G" -c .)
if [ -z "$emiss" ] && [ "$ne" -ge 5 ]; then
    t_ok "EXIT STATUS documents all $ne codes describe declares ($ecodes)"
else
    t_fail "exit codes declared but not in the man page:$emiss (describe lists \
$ne, floor 5) -- a supervisor reading only this page would misread them"
fi

# ---- 7: the page still renders, and the header is not stale ------------
# A man page that groff refuses is worse than a stale one: the reader gets
# nothing. `man --warnings` reports unknown requests and bad escapes.
warn=$(man --warnings -l "$MAN" 2>&1 >/dev/null | head -3 | tr '\n' ' ')
th=$("$G" -c '^\.TH JICHI 1 "2026' "$tmp/man.txt")
if [ -z "$warn" ] && [ "$th" = "1" ]; then
    t_ok "the page renders with no roff warnings and carries a dated .TH"
else
    t_fail "roff warnings: ${warn:-none}; dated .TH found: $th (want 1)"
fi

t_done
