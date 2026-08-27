#!/bin/sh
# smoke lint: a boolean a HUMAN writes must be read leniently (M519).
#
# WHAT THIS EXISTS FOR. `jc_json_get_bool` requires a real JSON bool: given the
# number `1` it sees "wrong type" and returns the caller's default. Config keys
# were read with it, and people write `1` for true -- so two silent inversions
# shipped, both found on 2026-08-21 by pointing jichi at its own example pack
# and reading `doctor`:
#
#   1. `"pathFence": 1` appeared in FIFTEEN examples/ configs and turned the
#      fence OFF in every one. The key's presence check fired, so path_fence was
#      set to the default 0 -- overriding the -1 tri-state that means "on in
#      autonomous postures". A config that reads as fencing was unfencing, in
#      exactly the autonomous runs where a fence is the whole safety story.
#   2. The seven keys whose default is 1 (wisdom, fuzzyEdit, snapshots, ...)
#      could not be switched off: `"snapshots": 0` fell back to 1 and ran with
#      snapshots ON.
#
# Neither was a wrong value in a config; both were a right value read wrongly, so
# no amount of fixing the fifteen files would have prevented the sixteenth. The
# fix is `jc_json_get_bool_lenient` (include/jc_json.h) at every site that reads
# JSON a human or a foreign program wrote. This lint holds that rule, because the
# next config key will be added by someone who has not read this story.
#
# WHAT IS AND IS NOT CHECKED (the M305 rule):
#   checked      -- that the three files reading human/foreign JSON use ONLY the
#                   lenient accessor, floored at today's exact counts; that the
#                   strict accessor still exists and is still used elsewhere; and
#                   the EFFECT, asked of the binary, from a shipped config and
#                   both inversion directions.
#   checked      -- (M530) that the strict reader is used by EXACTLY the three
#                   files that read jichi's own sinks. The M519 pass deferred an
#                   audit of the others "worth doing"; done at M530, it found
#                   four real instances of the same defect -- an editor's ACP
#                   capability flags, a supervisor script's control request, a
#                   session file's `isError` (a failed tool call reloading as a
#                   success), and a workflow map's `readonly` -- plus a fence:
#                   `spawn_subagent`'s `readonly` argument, where a model asking
#                   for a read-only child with `1` got a WRITABLE one.
#   NOT checked  -- whether telemetry/journal readers should ever be lenient.
#                   They read what jichi itself wrote, so a non-bool there is a
#                   bug to see. If one of them ever parses a user-written file,
#                   it moves to the lenient side and check 4's list changes.
#   NOT checked  -- whether any particular default is the right one. That is a
#                   design question; this lint only holds that the written value
#                   is the value that takes effect.
. "$(dirname "$0")/_smoke.sh"

t_plan 16
smoke_home
tmp=$(smoke_tmp)

# --- 1-3: the human/foreign-JSON readers use the lenient accessor only -------
# Floors are today's exact counts: a new key must raise the floor deliberately,
# which is the moment a reader meets this header.
check_file() {
    f="$1"; floor="$2"
    # grep -c prints 0 AND exits 1 when there are no matches, so `|| echo 0`
    # yields a two-line value and `[` calls it an illegal number -- noise on
    # stderr while the check still says ok, which is how a broken lint hides.
    strict=$(grep -c 'jc_json_get_bool(' "$SMOKE_ROOT/$f" 2>/dev/null)
    lenient=$(grep -c 'jc_json_get_bool_lenient(' "$SMOKE_ROOT/$f" 2>/dev/null)
    [ -n "$strict" ] || strict=missing
    [ -n "$lenient" ] || lenient=missing
    if [ "$strict" = missing ] || [ "$lenient" = missing ]; then
        t_fail "$f is unreadable from this lint -- the extraction is broken, and a
 broken extraction reports a clean universe"
    elif [ "$strict" -ne 0 ]; then
        t_fail "$f has $strict STRICT jc_json_get_bool call(s): a human writing 1
 for true there gets the default instead, silently. Use jc_json_get_bool_lenient"
    elif [ "$lenient" -lt "$floor" ]; then
        t_fail "$f has $lenient lenient call(s), floor is $floor -- calls
 disappeared; if that is deliberate, lower the floor in this lint and say why"
    else
        t_ok "$f: $lenient lenient, 0 strict (floor $floor)"
    fi
}
check_file src/config/jc_config.c 40
check_file src/convert/jc_convert_opencode.c 10
check_file src/mcp/jc_mcp_proto.c 2
# M530: the sites the M519 pass deferred, audited and converted. Each reads JSON
# somebody ELSE wrote: an editor (ACP capabilities), a supervisor script (the
# control socket), a hand-editable session file, a human's workflow map, and a
# MODEL's tool arguments.
check_file src/acp/jc_acp_proto.c 4
check_file src/util/jc_control_proto.c 1
check_file src/session/jc_session.c 2
check_file src/util/jc_workflow.c 1
check_file src/tui/jc_tui.c 2
check_file src/tools/tool_util.c 1

# --- 4: the boundary, asserted by NAME -------------------------------------
# The fix is two accessors, and the line between them is "who wrote this JSON".
# These three read jichi's OWN sinks -- telemetry, the run journals, and our own
# control server's reply -- where a non-bool is a bug to SEE, not a dialect to
# forgive. Asserted by name rather than counted (M530): a count says nothing
# about whether the right files are on the right side, and this check exists to
# say exactly that. If one of these is converted, the reason belongs here.
strict_set=$(find "$SMOKE_ROOT/src" -name '*.c' -exec grep -l 'jc_json_get_bool(' {} + \
             2>/dev/null | grep -v 'src/json/jc_json.c' \
             | sed "s|^$SMOKE_ROOT/||" | sort | tr '\n' ' ')
expect_set="src/main.c src/util/jc_runsview.c src/util/jc_telemetry.c "
if [ "$strict_set" = "$expect_set" ]; then
    t_ok "the strict reader is used by exactly the three own-sink readers"
else
    t_fail "the strict/lenient boundary moved.
 expected: $expect_set
 found:    $strict_set
 A file reading JSON that a HUMAN, an EDITOR, a SUPERVISOR SCRIPT or a MODEL
 wrote belongs on the lenient side (M519, M530). A file reading jichi's own
 telemetry or journals belongs on the strict side. If this list changed on
 purpose, say which of those it is and update the expectation."
fi

# --- 5: a shipped config that says pathFence 1 must FENCE ---------------------
# The effect, asked of the binary, from a file we actually ship. This is the
# check that would have caught the original defect; the pattern checks above
# only keep it caught.
cfg="$SMOKE_ROOT/examples/self-hosting/config.jichi-dev-local.json"
if ! grep -q '"pathFence": *1[^0-9]' "$cfg"; then
    t_fail "$cfg no longer says \"pathFence\": 1 -- this check measured that exact
 shape; point it at another numeric-bool config or the universe is empty"
else
    out=$(cd "$SMOKE_ROOT" && with_deadline 60 "$BIN" --config "$cfg" doctor \
          < /dev/null 2>&1)
    if printf '%s' "$out" | grep -q 'path fence on'; then
        t_ok "a shipped config with \"pathFence\": 1 reports the fence ON"
    else
        t_fail "a shipped config says \"pathFence\": 1 and doctor reports: $(printf '%s' "$out" | grep -i fence | head -1).
 The written value is not the value in effect"
    fi
fi

# --- 6-7: both inversion directions, through the binary ----------------------
# 0 must turn a default-ON key off, and 1 must turn a default-OFF key on.
# lowResource is PINNED false on purpose (smoke_lint check 7 caught its absence):
# auto-lite reshapes the very defaults checks 6-8 measure -- `snapshots` defaults
# to 0 under lite -- so on a small machine "prose fell through to the default"
# would read as a failure of leniency when it was a change of default.
mk() { printf '{"models":[{"name":"m","provider":"openai","model":"x",' > "$2"
       printf '"apiBase":"http://127.0.0.1:9/v1","roles":["chat"]}],' >> "$2"
       printf '"lowResource":false,%s}\n' "$1" >> "$2"; }
mk '"snapshots":0' "$tmp/off.json"
out=$(cd "$SMOKE_ROOT" && with_deadline 60 "$BIN" --config "$tmp/off.json" doctor \
      < /dev/null 2>&1)
if printf '%s' "$out" | grep -q 'snapshots disabled'; then
    t_ok "a numeric 0 switches a default-ON key off"
else
    t_fail "\"snapshots\":0 did not disable snapshots: $(printf '%s' "$out" | grep -i snapshot | head -1).
 A config that says off is running on"
fi

mk '"snapshots":true,"pathFence":1' "$tmp/on.json"
out=$(cd "$SMOKE_ROOT" && with_deadline 60 "$BIN" --config "$tmp/on.json" doctor \
      < /dev/null 2>&1)
if printf '%s' "$out" | grep -q 'path fence on'; then
    t_ok "a numeric 1 switches a default-OFF key on"
else
    t_fail "\"pathFence\":1 did not enable the fence: $(printf '%s' "$out" | grep -i fence | head -1)"
fi

# --- 8: prose is still refused (the boundary) --------------------------------
# Leniency stops at unambiguous encodings. A value that has to be GUESSED at must
# fall through to the default, or a typo becomes a silent policy change -- the
# same boundary jc_json_get_num_lenient draws for numeric strings (M168).
mk '"snapshots":"probably"' "$tmp/prose.json"
out=$(cd "$SMOKE_ROOT" && with_deadline 60 "$BIN" --config "$tmp/prose.json" doctor \
      < /dev/null 2>&1)
if printf '%s' "$out" | grep -q 'snapshots enabled'; then
    t_ok "prose falls through to the default (\"probably\" did not disable snapshots)"
else
    t_fail "prose changed the setting: $(printf '%s' "$out" | grep -i snapshot | head -1).
 Guessing at prose is how a typo becomes a policy"
fi

# --- 15: no hand-rolled boolean anywhere a human writes one (M534) ---------
# The lint enumerated CALLERS OF THE ACCESSOR and therefore could not see three
# readers that never called it: `strcmp(str, "true")` on YAML frontmatter, two of
# them FENCES (`readonly:` on an agent profile, `restrict-tools:` on a skill).
# Measured before the fix: four profiles each declaring read-only, and only the
# one spelling `true` was fenced -- "audit the universe, not the result", failed
# by this very lint. So the universe is now the QUESTION, not the function.
# Comment lines are excluded: a comment quoting the old code is not the old code,
# and the first version of this check reported one of its own explanations.
hand=$(find "$SMOKE_ROOT/src" -name '*.c' -exec grep -Hn 'strcmp([a-z_]*, *"true")' {} + \
       2>/dev/null | grep -v ':[0-9]*: *\*' | grep -v ':[0-9]*: */\*' \
       | sed "s|^$SMOKE_ROOT/||")
if [ -z "$hand" ]; then
    t_ok "no hand-rolled boolean comparison remains in src/"
else
    t_fail "a hand-rolled boolean comparison is back. Use jc_bool_from_word
 (jc_str.h) so every file agrees what true spells -- two of the three this check
 was written for were fences that silently granted write access:
$hand"
fi

# --- 16: and the dialect is genuinely shared -------------------------------
# The point is one implementation, not three that happen to agree today. If a
# second word list appears, this catches it.
# The DEFINITION, not the word "yes" -- which also appears in prompts and
# comments. One definition is the property that stops the dialect drifting.
impls=$(find "$SMOKE_ROOT/src" -name '*.c' -exec grep -l 'int jc_bool_from_word(' {} + \
        2>/dev/null | sed "s|^$SMOKE_ROOT/||" | sort | tr '\n' ' ')
if [ "$impls" = "src/util/jc_str.c " ]; then
    t_ok "the boolean dialect is defined in exactly one place"
else
    t_fail "the boolean dialect is defined in: $impls -- one definition is what
 stops four readers disagreeing again (M519, M530, M534)"
fi

t_done
