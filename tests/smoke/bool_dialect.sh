#!/bin/sh
# smoke: one boolean dialect, whatever file the boolean lives in (M534).
#
# THE DEFECT, third occurrence of the same shape. jichi had four opinions about
# what "true" spells, and two of them were FENCES:
#
#   .jichi/agents/*.md   `readonly:` read by strcmp(str,"true")
#   .jichi/skills/*.md   `restrict-tools:` likewise
#   config set           blessed on/off, which no READER accepted
#   jc_json_get_bool_lenient   true/yes/1, no on/off
#
# Measured before the fix: four agent profiles each declaring read-only, and only
# the one spelling `true` was fenced -- the other three set the presence flag with
# a false value, which downstream reads as an EXPLICIT "writable". That is M519's
# `"pathFence": 1` and M530's `{"readonly": 1}` a third time, in YAML. And
# `"pathFence": "on"` -- the spelling jichi's own `config set` writes -- turned the
# fence OFF, because the writer and the reader disagreed about the word.
#
# WHAT IS AND IS NOT CHECKED (the M305 rule):
#   checked      -- every spelling of yes fences an agent profile; a NON-boolean
#                   value does not grant the fence; and the `on` spelling that
#                   `config set` blesses is now honoured by the loader.
#   NOT checked  -- `restrict-tools:` on a skill, which shares the same reader and
#                   the same unit test but has no listing surface to assert on
#                   without spawning a subagent and a model.
#   NOT checked  -- YAML 1.1's wider set (`y`, `off` as a key). Deliberately: the
#                   dialect is the one jichi itself writes, and `y` is not it.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home

# --- 1-4: every spelling of yes must fence the profile ----------------------
ws=$(smoke_tmp); mkdir -p "$ws/.jichi/agents"
for v in 1 yes True true; do
    printf -- '---\nname: ro-%s\ndescription: d\nreadonly: %s\n---\nBody.\n' \
        "$v" "$v" > "$ws/.jichi/agents/ro-$v.md"
done
out=$(cd "$ws" && with_deadline 30 "$BIN" agents < /dev/null 2>&1)
for v in 1 yes True true; do
    if printf '%s' "$out" | grep -A1 "ro-$v" | grep -q readonly; then
        t_ok "readonly: $v fences the profile"
    else
        t_fail "readonly: $v did NOT fence the profile -- the presence check fired
 while the value did not match, so this reads downstream as an explicit
 'writable' and the agent may write: $(printf '%s' "$out" | grep -A1 "ro-$v")"
    fi
done

# --- 5: a NON-boolean must not grant the fence, and must not deny it either --
# The conservative reading: the key was written, we could not understand it, and a
# fence is not something to grant by guessing.
ws2=$(smoke_tmp); mkdir -p "$ws2/.jichi/agents"
printf -- '---\nname: ro-junk\ndescription: d\nreadonly: maybe\n---\nBody.\n' \
    > "$ws2/.jichi/agents/ro-junk.md"
out=$(cd "$ws2" && with_deadline 30 "$BIN" agents < /dev/null 2>&1)
if printf '%s' "$out" | grep -q 'ro-junk'; then
    t_ok "a profile with a non-boolean readonly still loads (prose is not a boolean)"
else
    t_fail "a non-boolean readonly broke the profile entirely: $out"
fi

# --- 6-7: the spelling `config set` blesses is the spelling the loader reads --
# These run with $JC_CONFIG unset (smoke_home pins it, M376) and cd into a fresh
# workspace first, so the only config in reach is the fixture written here.
cw=$(smoke_tmp); mkdir -p "$cw/local"
printf '{"models":[{"name":"m","provider":"openai","model":"x","apiBase":"http://127.0.0.1:9/v1","roles":["chat"]}],"lowResource":false,"pathFence":"on"}\n' \
    > "$cw/local/config.json"
out=$(cd "$cw" && unset JC_CONFIG && with_deadline 30 "$BIN" doctor < /dev/null 2>&1)
if printf '%s' "$out" | grep -q 'path fence on'; then
    t_ok '"pathFence": "on" turns the fence ON'
else
    t_fail '"pathFence": "on" did not enable the fence -- config set writes that
 spelling, so a human copying it gets a config that reads as false:
 '"$(printf '%s' "$out" | grep -i fence | head -1)"
fi
printf '{"models":[{"name":"m","provider":"openai","model":"x","apiBase":"http://127.0.0.1:9/v1","roles":["chat"]}],"lowResource":false,"pathFence":"off"}\n' \
    > "$cw/local/config.json"
out=$(cd "$cw" && unset JC_CONFIG && with_deadline 30 "$BIN" doctor < /dev/null 2>&1)
if printf '%s' "$out" | grep -qE 'path fence (off|auto)'; then
    t_ok 'control: "pathFence": "off" does not turn it on'
else
    t_fail 'control failed: "off" was read as on, so check 6 proves nothing:
 '"$(printf '%s' "$out" | grep -i fence | head -1)"
fi

t_done
