#!/bin/sh
# smoke: a writer must target the file its READER will read (M533).
#
# THE SHAPE, found three times in one reading. jichi has several
# "one-of-two-files" readers, and two of its writers named a file unconditionally
# instead of asking the reader's question. The result is not a wrong value; it is
# a whole file silently stopping being read:
#
#   config:  the loader takes `local/config.json` if present and
#            `.jichi/config.json` OTHERWISE. `config set` always wrote
#            `local/config.json` -- so on a project using `.jichi/config.json`,
#            one unrelated key edit created a new file holding that one key, which
#            then WON the exclusive choice. Measured: the project's model and
#            `pathFence`, `privilegedCommands: deny` and `permissions.deny` all
#            stopped applying, while the file sat on disk unread. It also
#            self-locked, because `configEditable` was among the orphans.
#
#   rules:   `jc_rules.c:add_dir_rules` reads `AGENTS.md` and, only if absent,
#            `CLAUDE.md`. `learn apply` always wrote `AGENTS.md` -- so in a
#            CLAUDE.md project (jichi itself) it CREATED the one file CLAUDE.md
#            explicitly warns against, shadowing all of it for every later run.
#
# WHAT IS AND IS NOT CHECKED (the M305 rule):
#   checked      -- both writers land in the file the reader would read, in both
#                   directions, each with a control proving the other branch still
#                   works. Plus the board's damaged-file refusal, which is the
#                   same family: a file that could not be READ must not be
#                   overwritten.
#   NOT checked  -- other one-of-two readers. `~/.jichi.env` vs the environment,
#                   and the session store's naming, were not examined; if a third
#                   instance turns up it belongs here.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home

# The config-precedence checks need the AMBIENT project-config path live, so they
# run with $JC_CONFIG unset -- `smoke_home` pins it (M376) precisely so a driver
# cannot reach the dev box's ./local/config.json and make live calls. That hazard
# does not apply here because every one of these checks `cd`s into a fresh
# smoke_tmp workspace first, so the only local/config.json in reach is the fixture
# this driver just wrote. Stated because M376's comment is a standing warning.
cfg_body='{"models":[{"name":"projmodel","provider":"openai","model":"proj/x",
 "apiBase":"http://127.0.0.1:9/v1","roles":["chat"]}],
 "configEditable":true,"pathFence":true,"lowResource":false}'

# --- 1-2: config set lands in .jichi/config.json when that is the reader's file
ws=$(smoke_tmp); mkdir -p "$ws/.jichi"
printf '%s\n' "$cfg_body" > "$ws/.jichi/config.json"
(cd "$ws" && unset JC_CONFIG && with_deadline 30 "$BIN" config set snapshots false) > "$ws/out" 2>&1
if [ -f "$ws/local/config.json" ]; then
    t_fail "config set created local/config.json and orphaned .jichi/config.json,
 whose model and fences now do not apply: $(cat "$ws/local/config.json")"
else
    t_ok "config set writes .jichi/config.json when that is what the loader reads"
fi
out=$(cd "$ws" && unset JC_CONFIG && with_deadline 30 "$BIN" doctor < /dev/null 2>&1)
if printf '%s' "$out" | grep -q 'projmodel' &&
   printf '%s' "$out" | grep -q 'path fence on'; then
    t_ok "and the project's model and fence still apply afterwards"
else
    t_fail "the project config stopped applying after one key edit:
 $(printf '%s' "$out" | grep -E 'active:|path fence' | head -2)"
fi

# --- 3-4: the control -- a local/config.json project must still be written there
ws2=$(smoke_tmp); mkdir -p "$ws2/local"
printf '%s\n' "$cfg_body" > "$ws2/local/config.json"
(cd "$ws2" && unset JC_CONFIG && with_deadline 30 "$BIN" config set snapshots false) > "$ws2/out" 2>&1
if grep -q '"snapshots"' "$ws2/local/config.json"; then
    t_ok "control: a local/config.json project is still written there"
else
    t_fail "control failed: the key did not land in local/config.json"
fi
if [ -f "$ws2/.jichi/config.json" ]; then
    t_fail "control failed: it created .jichi/config.json as well"
else
    t_ok "control: and no second config file was invented"
fi

# --- 5-6: learn apply must not create AGENTS.md in a CLAUDE.md project -------
lw=$(smoke_tmp); mkdir -p "$lw/.jichi"
printf '# Project rules\n\nExisting house rule.\n' > "$lw/CLAUDE.md"
cat > "$lw/.jichi/lessons.draft.md" <<'EOF'
## Project rules
- always run the gate before claiming a milestone
EOF
(cd "$lw" && with_deadline 30 "$BIN" learn apply) > "$lw/out" 2>&1
if [ -f "$lw/AGENTS.md" ]; then
    t_fail "learn apply created AGENTS.md in a CLAUDE.md project -- add_dir_rules
 takes AGENTS.md first and RETURNS, so the whole of CLAUDE.md is now shadowed
 (which CLAUDE.md itself warns against)"
else
    t_ok "learn apply does not create AGENTS.md in a CLAUDE.md project"
fi
if grep -q 'always run the gate' "$lw/CLAUDE.md"; then
    t_ok "and the rule landed in CLAUDE.md, where the loader will read it"
else
    t_fail "the rule went nowhere the loader reads: $(tail -c 200 "$lw/out")"
fi

# --- 7: control -- a project with NEITHER file still gets AGENTS.md ----------
nw=$(smoke_tmp); mkdir -p "$nw/.jichi"
cat > "$nw/.jichi/lessons.draft.md" <<'EOF'
## Project rules
- a rule for a project with no rules file yet
EOF
(cd "$nw" && with_deadline 30 "$BIN" learn apply) > "$nw/out" 2>&1
if [ -f "$nw/AGENTS.md" ] && grep -q 'no rules file yet' "$nw/AGENTS.md"; then
    t_ok "control: a project with neither file still gets AGENTS.md"
else
    t_fail "control failed: no AGENTS.md was created for a project with neither
 file, so check 5 may pass because rule-writing is broken: $(tail -c 200 "$nw/out")"
fi

# --- 8: a board that could not be READ must not be overwritten --------------
# Same family: an unreadable file used to load as an EMPTY one, and the next save
# wrote the empty version over it. Measured: three cards, one truncated save, one
# `board add`, and the file held one card. docs/BOARD.md says to commit this file.
bw=$(smoke_tmp)
(cd "$bw" && with_deadline 30 "$BIN" board add "card ONE" >/dev/null 2>&1)
(cd "$bw" && with_deadline 30 "$BIN" board add "card TWO" >/dev/null 2>&1)
head_bytes 40 "$bw/.jichi/board.json" > "$bw/trunc" && mv "$bw/trunc" "$bw/.jichi/board.json"
before=$(wc -c < "$bw/.jichi/board.json" | tr -d ' ')
(cd "$bw" && with_deadline 30 "$BIN" board add "card THREE") > "$bw/out" 2>&1
after=$(wc -c < "$bw/.jichi/board.json" | tr -d ' ')
if [ "$before" = "$after" ] && grep -q 'refusing to save' "$bw/out"; then
    t_ok "a damaged board is preserved and the save is refused out loud"
else
    t_fail "a damaged board was overwritten ($before -> $after bytes), destroying
 the cards still in it: $(tail -c 200 "$bw/out")"
fi

t_done
