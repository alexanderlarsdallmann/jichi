#!/bin/sh
# smoke lint: every name in `describe --output json` is a real name (M538).
#
# THE DEFECT, and what it cost. `describe` is advertised in its own header as
# "the stable contract a driving agent needs without reading the source", and SIX
# of its rows, across THREE tables, held prose where a name belongs:
#
#   subcommands[].name        = "session/export/rewind/undo"
#   key_flags[].name          = "-p, --print [text]"
#                               "--session <id|prefix>, -c/--continue"
#   conditional_tools[].name  = "git_status/git_diff/.../git_stash"      (8 tools)
#                               "find_definition/.../apply_code_action"  (7 tools)
#                               "generate_image/generate_audio/..."      (3 tools)
#                               "read_mcp_resource + <server>__<tool>"
#
# I found the first two by reading, fixed them, and wrote this lint -- and CHECK 2
# IMMEDIATELY FOUND THE OTHER FOUR. My analysis had a narrower universe than my
# gate, which is the whole argument for preferring a lint to an audit: the audit
# found what it knew to look for, and the lint found what was there. The
# conditional_tools rows were worse than the others, too: their `summary` field
# held the CONDITION ("in a git repository"), so both fields were misused and the
# table could not answer the only question it exists for -- is `rename_symbol`
# available to me, and under what condition? Each tool now gets a row with its
# condition in a `when` field, and `pattern: true` marks the one entry that is a
# naming convention rather than a name.
#
# AND THE FIX ITSELF NEEDED THE LESSON APPLIED ONE LEVEL DEEPER. The first cut put
# the argument placeholder in as `"[text]"` -- and notice_tags_lint immediately
# failed with "tags emitted but unregistered: [text]", because a bracketed token in
# a source string is how this project marks a NOTICE. It was right to complain for
# the right reason: `[]` and `<>` are human synopsis notation, and a milestone about
# not putting human notation in machine fields had left it in the field next door.
# `arg` now holds the argument's name and `arg_optional` is a boolean.
#
# The first is not a command, is not four commands, and cannot be split on any
# single separator. `session` is not a subcommand at all -- it is the --session
# FLAG, and `jichi session` falls through to being sent to the model as a prompt.
# The row also silently omitted `checkpoints` and `recover`.
#
# Reading that row as a list of subcommand names, I probed all four by RUNNING
# them, and `undo` reverted this repository's working tree: 768 files, 41,927
# deletions, reported as one line naming the checkpoint's label. A contract that
# hides a destructive command inside a slash-separated string gives its reader no
# way to see which of the four writes. ANECDOTES #66 has the incident; M537 is the
# blast-radius report it produced; this lint is so the contract cannot go stale
# again.
#
# WHY A LINT AND NOT A FIX. Four namespaces already have parity lints -- flags
# (docs_flags), subcommands (subcommands_lint), tool names (tool_names_lint),
# slash commands (two). `describe` had none, which is why it drifted: nothing
# compared its tables against the binary they claim to describe. The tool list is
# already built from the real registry; these two tables are hand-written, and a
# hand-written table with no gate is a table that is wrong eventually.
#
# THE UNIVERSE. Every `name` in `subcommands[]` must appear in --help's command
# column; every `name` in `key_flags[]`, and every entry in its `aliases`, must be
# accepted by the parser. --help's column is the ground truth for commands rather
# than a grep of main.c, because a grep of `strcmp(args.pos[0], "x")` MISSES
# `export`, `checkpoints`, `recover`, `undo` and `rewind` -- they dispatch through
# other paths. subcommands_lint.sh's header records the same trap costing it four
# names, and this driver's author walked into it again while measuring for M539.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
G=/usr/bin/grep
[ -x "$G" ] || G=grep

with_deadline 60 "$BIN" describe --output json < /dev/null > "$tmp/d.json" 2>&1
with_deadline 60 "$BIN" --help < /dev/null > "$tmp/help.txt" 2>&1

# ---- 1: the dump parsed, and both tables are populated (the denominator) ---
# Every check below reads one of these two arrays. An empty array satisfies "no
# bad names" perfectly, which is the vacuous shape this project keeps finding in
# its own checks -- so the sizes are asserted first and floored at today's count.
# `grep -c` counts LINES, and jsonq emits the array on ONE line, so the first
# cut of this check read 1 and 1 for tables of 21 and 9 -- a floor that can never
# be met, on a measurement that was never taken. `grep -o | wc -l` counts
# occurrences, which is what "how many rows" means.
ncmd=$("$JQ" '.subcommands' "$tmp/d.json" 2>/dev/null \
       | "$G" -o '"name"' | wc -l | tr -d ' ')
nflag=$("$JQ" '.key_flags' "$tmp/d.json" 2>/dev/null \
        | "$G" -o '"name"' | wc -l | tr -d ' ')
[ -n "$ncmd" ] || ncmd=0
[ -n "$nflag" ] || nflag=0
if [ "$ncmd" -ge 21 ] && [ "$nflag" -ge 9 ]; then
    t_ok "describe lists $ncmd subcommands and $nflag key flags (floors 21/9)"
else
    t_fail "subcommands=$ncmd (want >= 21) key_flags=$nflag (want >= 9) -- the \
tables shrank, or the dump did not parse; nothing below would mean anything"
fi

# ---- 2: no name field carries prose ---------------------------------------
# The syntactic half, and the one that would have caught the original directly: a
# name containing a slash, a space or a bracket is a synopsis, not a name.
bad=$("$G" -o '"name":"[^"]*"' "$tmp/d.json" \
      | "$G" -E '"name":"[^"]*([/ ]|\[)' | head -5 | tr '\n' ' ')
if [ -z "$bad" ]; then
    t_ok "no name field contains a slash, space or bracket"
else
    t_fail "a name field holds prose rather than a name: $bad"
fi

# ---- 3: every subcommand name is in --help's command column ---------------
# The direction that catches an invented command. --help indents its command
# column by two spaces, which is how subcommands_lint reads it too.
# SCOPED to the subcommands array. The first cut of this check grepped the whole
# document for lowercase name fields and reported `ping` and `shutdown` missing
# from --help -- they are DAEMON REQUEST names, from an entirely different table.
# An over-wide extraction reports defects that are not there, which costs exactly
# as much trust as a too-narrow one that reports none.
missing=""
for c in $("$JQ" '.subcommands' "$tmp/d.json" 2>/dev/null \
           | "$G" -o '"name":"[a-z][a-z-]*"' \
           | sed 's/.*"\([a-z][a-z-]*\)"/\1/' | sort -u); do
    if "$G" -q "^  $c\( \|$\)" "$tmp/help.txt"; then
        continue
    fi
    missing="$missing $c"
done
if [ -z "$missing" ]; then
    t_ok "every subcommand describe advertises appears in --help"
else
    t_fail "describe names subcommands --help does not offer:$missing -- either \
the command was removed and the contract still promises it, or --help lost it"
fi

# ---- 4: every key_flag name is accepted by the parser --------------------
# `--nonsense-flag` must be rejected, so the probe below is only meaningful if
# rejection is observable at all: check 5 proves that. Here, each advertised flag
# must NOT produce an unknown-option error.
badflag=""
for f in $("$JQ" '.key_flags' "$tmp/d.json" 2>/dev/null \
           | "$G" -o '"--[a-z-]*"' | sed 's/"//g' | sort -u); do
    err=$(with_deadline 20 "$BIN" "$f" --help < /dev/null 2>&1 \
          | "$G" -ci 'unknown option\|unrecognized' || true)
    [ "$err" = "0" ] || badflag="$badflag $f"
done
if [ -z "$badflag" ]; then
    t_ok "every advertised key flag is accepted by the parser"
else
    t_fail "describe advertises flags the parser rejects:$badflag"
fi

# ---- 5: the probe in check 4 can actually fail --------------------------
# Without this, check 4 passes on a binary that never prints "unknown option" for
# anything -- a check whose instrument is blind reports clean skies. This is the
# fifth-plus time in this project that a check needed proof it could fail; see
# docs/analysis/2026-08-22-learning-from-errors.md.
if with_deadline 20 "$BIN" --definitely-not-a-flag < /dev/null 2>&1 \
   | "$G" -qi 'unknown option\|unrecognized'; then
    t_ok "an invented flag IS rejected, so check 4's probe can fail"
else
    t_fail "the binary does not report unknown options, so check 4 proves \
nothing: $(with_deadline 20 "$BIN" --definitely-not-a-flag < /dev/null 2>&1 \
| head -1 | head_bytes 120)"
fi

# ---- 6: `session` is not advertised as a subcommand --------------------
# Named explicitly because it is the string that caused the incident. NOT because
# the other checks would miss it: perturbing describe to emit a bare "session" row
# was caught by check 3 as well, since `session` is absent from --help's command
# column. The first draft of this comment claimed checks 2 and 3 "would both
# pass", and the perturbation run disproved it -- recorded rather than quietly
# corrected, because a header that overstates a check's uniqueness is the same
# species of error as one that overstates its coverage.
#
# What this check adds over check 3 is the DIAGNOSTIC: check 3 says "--help does
# not offer it", which sends a reader to --help. This one says what is actually
# true -- it is the --session flag, and running it sends the word to the model as
# a prompt -- which is the sentence that would have stopped me.
if "$JQ" '.subcommands' "$tmp/d.json" 2>/dev/null | "$G" -q '"name":"session"'; then
    t_fail "'session' is advertised as a subcommand; it is the --session FLAG, \
and running it sends the word to the model as a prompt"
else
    t_ok "'session' is not offered as a subcommand (it is the --session flag)"
fi

t_done
