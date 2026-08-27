#!/bin/sh
# smoke: `init` scaffolds .jichi/ assets into a fresh project, idempotently
# and non-destructively; packs compose; the asset loaders then pick the
# scaffold up. (Port of tests/e2e/init.py, M210; absorbs the former
# init_scaffold.sh, whose user-edit-preserved check is case 4.)
. "$(dirname "$0")/_smoke.sh"

t_plan 15
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# --list names the default + archetype packs
(cd "$ws" && "$BIN" init --list < /dev/null > "$tmp/list" 2>&1); rc=$?
missing=""
for pk in default c-cli zig-cli python-cli godot docs systems-analysis; do
    grep -q "$pk" "$tmp/list" || missing="$missing $pk"
done
if [ $rc -eq 0 ] && [ -z "$missing" ]; then
    t_ok "init --list names the packs"
else
    t_fail "init --list rc=$rc; missing:$missing"
fi

# init writes the expected tree
(cd "$ws" && "$BIN" init < /dev/null > "$tmp/out" 2>&1); rc=$?
missing=""
for rel in AGENTS.md .jichi/agents/reviewer.md \
           .jichi/agents/docs-proofreader.md \
           .jichi/skills/commit-message/SKILL.md \
           .jichi/commands/explain.md; do
    [ -f "$ws/$rel" ] || missing="$missing $rel"
done
if [ $rc -eq 0 ] && [ -z "$missing" ]; then
    t_ok "init writes the expected tree"
else
    t_fail "init rc=$rc; missing:$missing"
fi

# a scaffolded agent has frontmatter
if head -1 "$ws/.jichi/agents/reviewer.md" | grep -q '^---$' \
   && grep -q "description:" "$ws/.jichi/agents/reviewer.md"; then
    t_ok "scaffolded agent carries valid frontmatter"
else
    t_fail "reviewer.md missing frontmatter"
fi

# re-running is non-destructive: skipped, and a user edit is preserved
echo "user edit" >> "$ws/AGENTS.md"
sum_before=$(cksum "$ws/AGENTS.md" | awk '{print $1, $2}')
(cd "$ws" && "$BIN" init < /dev/null > "$tmp/out2" 2>&1); rc=$?
sum_after=$(cksum "$ws/AGENTS.md" | awk '{print $1, $2}')
if [ $rc -eq 0 ] && grep -q "skipped" "$tmp/out2" \
   && grep -q "0 written" "$tmp/out2" \
   && [ "$sum_before" = "$sum_after" ]; then
    t_ok "re-run skips everything and preserves user edits"
else
    t_fail "re-run rc=$rc; edited AGENTS.md changed or not skipped"
fi

# --dry-run never writes
ws2=$(smoke_tmp)
(cd "$ws2" && "$BIN" init --dry-run < /dev/null > /dev/null 2>&1); rc=$?
if [ $rc -eq 0 ] && [ ! -e "$ws2/.jichi" ]; then
    t_ok "--dry-run writes nothing"
else
    t_fail "--dry-run rc=$rc or created files"
fi

# unknown pack is a usage error
(cd "$ws" && "$BIN" init nope < /dev/null > /dev/null 2>&1); rc=$?
if [ $rc -eq 2 ]; then
    t_ok "unknown pack -> exit 2"
else
    t_fail "init nope rc=$rc (want 2)"
fi

# an archetype pack writes domain assets + a valid config.example.json
ws3=$(smoke_tmp)
(cd "$ws3" && "$BIN" init c-cli < /dev/null > /dev/null 2>&1); rc=$?
missing=""
for rel in AGENTS.md config.example.json .jichi/agents/c-reviewer.md \
           .jichi/skills/valgrind-triage/SKILL.md; do
    [ -f "$ws3/$rel" ] || missing="$missing $rel"
done
if [ $rc -eq 0 ] && [ -z "$missing" ] \
   && [ "$("$JQ" '.testCommand' "$ws3/config.example.json")" = "make test" ]; then
    t_ok "c-cli pack: domain assets + valid example config"
else
    t_fail "c-cli rc=$rc; missing:$missing"
fi

# the docs pack ships audience-aware agents; proofreaders are read-only
ws4=$(smoke_tmp)
(cd "$ws4" && "$BIN" init docs < /dev/null > /dev/null 2>&1); rc=$?
missing=""
for rel in .jichi/agents/docs-writer-beginner.md \
           .jichi/agents/docs-proofreader-expert.md \
           .jichi/agents/docs-writer-master.md \
           .jichi/agents/support-responder.md; do
    [ -f "$ws4/$rel" ] || missing="$missing $rel"
done
if [ $rc -eq 0 ] && [ -z "$missing" ] \
   && grep -q "readonly: true" "$ws4/.jichi/agents/docs-proofreader-beginner.md"; then
    t_ok "docs pack: audience agents, proofreaders read-only (M13)"
else
    t_fail "docs pack rc=$rc; missing:$missing"
fi

# asset introspection sees the scaffold
intro_ok=1
(cd "$ws" && "$BIN" agents < /dev/null 2>&1 | grep -q "reviewer") || intro_ok=0
(cd "$ws" && "$BIN" commands < /dev/null 2>&1 | grep -q "/explain") || intro_ok=0
(cd "$ws" && "$BIN" rules < /dev/null 2>&1 | grep -q "Rules from") || intro_ok=0
if [ $intro_ok -eq 1 ]; then
    t_ok "agents/commands/rules introspection sees the scaffold (M15)"
else
    t_fail "an introspection subcommand missed the scaffold"
fi

# empty project: friendly no-op
ws5=$(smoke_tmp)
(cd "$ws5" && "$BIN" agents < /dev/null > "$tmp/ag" 2>&1); rc=$?
if [ $rc -eq 0 ] && grep -q "no agent profiles" "$tmp/ag"; then
    t_ok "agents in an empty project is a friendly no-op"
else
    t_fail "agents empty rc=$rc: $(head_bytes 120 "$tmp/ag")"
fi

# M17: the assignments pack + subcommand
ws6=$(smoke_tmp)
(cd "$ws6" && "$BIN" init assignments < /dev/null > /dev/null 2>&1); rc=$?
missing=""
for rel in .jichi/agents/assignment-writer.md \
           .jichi/agents/solution-writer.md \
           .jichi/agents/solution-checker.md \
           .jichi/commands/assign.md .jichi/commands/solve.md \
           .jichi/commands/check.md; do
    [ -f "$ws6/$rel" ] || missing="$missing $rel"
done
if [ $rc -eq 0 ] && [ -z "$missing" ] \
   && grep -q "readonly: true" "$ws6/.jichi/agents/solution-checker.md"; then
    t_ok "assignments pack: agents + commands, checker read-only (M17)"
else
    t_fail "assignments pack rc=$rc; missing:$missing"
fi

# assignments listing: empty no-op, then lists + flags a solution sibling
asg_ok=1
(cd "$ws6" && "$BIN" assignments < /dev/null 2>&1 \
    | grep -q "no assignments") || asg_ok=0
mkdir -p "$ws6/docs/assignments"
printf -- '---\nphase: design\n---\n' > "$ws6/docs/assignments/parser.md"
printf 'sol\n' > "$ws6/docs/assignments/parser.solution.md"
printf 'x\n' > "$ws6/docs/assignments/cache.md"
(cd "$ws6" && "$BIN" assignments < /dev/null > "$tmp/asg" 2>&1)
grep -q "parser.md" "$tmp/asg" || asg_ok=0
grep -q "(+solution)" "$tmp/asg" || asg_ok=0
grep -q "cache.md" "$tmp/asg" || asg_ok=0
if grep -q "parser.solution.md" "$tmp/asg"; then asg_ok=0; fi
if [ $asg_ok -eq 1 ]; then
    t_ok "assignments listing: +solution flag, sibling hidden"
else
    t_fail "assignments listing wrong: $(head_bytes 200 "$tmp/asg")"
fi

# M186: the music pack + its engraving-gated example config
wsm=$(smoke_tmp)
(cd "$wsm" && "$BIN" init music < /dev/null > /dev/null 2>&1); rc=$?
missing=""
for rel in .jichi/agents/composer.md .jichi/agents/engraver.md \
           .jichi/skills/lilypond-notation/SKILL.md \
           .jichi/commands/engrave.md config.example.json; do
    [ -f "$wsm/$rel" ] || missing="$missing $rel"
done
if [ $rc -eq 0 ] && [ -z "$missing" ] \
   && "$JQ" '.verify' "$wsm/config.example.json" | grep -q "lilypond" \
   && grep -q "readonly: true" "$wsm/.jichi/agents/engraver.md"; then
    t_ok "music pack: assets + lilypond-gated config, engraver read-only"
else
    t_fail "music pack rc=$rc; missing:$missing"
fi

# M182/M184: multi-pack composition; first pack wins shared paths; a bad
# name rejects BEFORE anything is written; a11y advisors ship in every pack
ws7=$(smoke_tmp)
(cd "$ws7" && "$BIN" init default c-cli < /dev/null > "$tmp/multi" 2>&1); rc=$?
multi_ok=1
[ $rc -eq 0 ] || multi_ok=0
grep -q "Scaffolding 'default'" "$tmp/multi" || multi_ok=0
grep -q "Scaffolding 'c-cli'" "$tmp/multi" || multi_ok=0
grep -q "skipped" "$tmp/multi" || multi_ok=0
for rel in agents/accessibility-reviewer.md skills/a11y-checklist/SKILL.md \
           commands/a11y-review.md; do
    [ -e "$ws7/.jichi/$rel" ] || multi_ok=0
done
if grep -q "C89" "$ws7/AGENTS.md" || grep -q -- "-pedantic" "$ws7/AGENTS.md"
then
    multi_ok=0      # the c-cli variant must have been skipped
fi
if [ $multi_ok -eq 1 ]; then
    t_ok "multi-pack init: both scaffolded, first wins, a11y ships"
else
    t_fail "multi-pack init rc=$rc: $(head_bytes 200 "$tmp/multi")"
fi

ws8=$(smoke_tmp)
(cd "$ws8" && "$BIN" init default nope < /dev/null > /dev/null 2>&1); rc=$?
if [ $rc -eq 2 ] && [ ! -d "$ws8/.jichi" ]; then
    t_ok "a bad pack name in a multi-init rejects before writing"
else
    t_fail "bad multi-init rc=$rc (want 2) or wrote files"
fi

t_done
