#!/bin/sh
# smoke: the journal's `no_changes` reports whether the TREE changed, not whether a
# mutating tool ran (M496).
#
# THE DEFECT THIS EXISTS FOR, measured twice while dogfooding jichi on two sibling
# projects, on two different models. `no_changes` was computed as
# `green_commit[0] == '\0'` -- "no pre-edit checkpoint was taken", which means "no
# MUTATING TOOL ran". `run_terminal_command` is a mutating tool, so the checkpoint is
# taken the moment the model runs a shell command, whether or not that command writes
# anything. One run made 9 shell calls, another 46; both left `git status` clean; both
# reported `no_changes: false`. The second is proven by mtime -- its only modified
# file predates the run window entirely -- so the journal told a supervisor that work
# had been done on a tree nothing had touched.
#
# That is the dominant case, not a corner: the shell is by far the most-called tool
# (46 of 62 calls in that run), and read-only investigation through it is exactly what
# an analysis run does.
#
# WHAT IS ASSERTED: a run that only LOOKS says true, a run that WRITES says false.
# Both directions, because a field pinned in one direction can be stuck.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "needs git (no snapshots, so the field is omitted by design)"

t_plan 3
smoke_home
tmp=$(smoke_tmp)

# The field is only emitted with snapshots on, and snapshots need a git workspace.
mk_ws() {   # $1 = dir
    ( cd "$1" && git init -q . && git config user.email t@t && git config user.name t
      printf 'original\n' > kept.txt
      git add -A && git commit -qm init )
}

mk_cfg() {  # $1 = path, $2 = port
    cat > "$1" <<EOC
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$2/v1","apiKey":"x","roles":["chat"]}],
"snapshots":true,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOC
}

# ---- 1. a run that only LOOKS -----------------------------------------------
# `true` is a mutating TOOL (run_terminal_command) running a command that writes
# nothing -- the exact shape that produced the false report.
ws=$(smoke_tmp); mk_ws "$ws"
cat > "$tmp/look.mm" <<'EOF'
wire openai
rule
  match "\"messages\""
  nomatch "\"role\":\"tool\""
  tool run_terminal_command {"command":"true"}
rule
  match "\"messages\""
  text LOOKED
EOF
mm_start "$tmp/look.mm" "$tmp/cap1" 6
mk_cfg "$tmp/c1.json" "$MM_PORT"
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/c1.json" --no-session --auto \
    --journal "$tmp/j1.jsonl" -p 'look around' < /dev/null > /dev/null 2>&1)
mm_stop

if grep -q '"no_changes":true' "$tmp/j1.jsonl" 2>/dev/null; then
    t_ok "a run that ran a shell command but wrote nothing reports no_changes:true"
else
    t_fail "a run that changed NOTHING reported: \
$(grep -o '"no_changes":[a-z]*' "$tmp/j1.jsonl" 2>/dev/null | tail -1) \
-- a supervisor cannot then tell 'did the work' from 'did nothing' (git status: \
$(cd "$ws" && git status --porcelain | wc -l) changed path(s))"
fi

# ---- 2. and the tree really was clean, so check 1 is about the FIELD ---------
# Without this, check 1 could pass because the run failed to do anything at all for
# some unrelated reason, and the field would be right by accident.
if [ "$(cd "$ws" && git status --porcelain | wc -l | tr -d ' ')" = "0" ]; then
    t_ok "the workspace really is unchanged (so check 1 tested the field, not luck)"
else
    t_fail "the fixture wrote something after all: $(cd "$ws" && git status --porcelain | head -3)"
fi

# ---- 3. a run that WRITES ---------------------------------------------------
# The other direction: a field stuck at true would pass check 1 and fail here.
ws2=$(smoke_tmp); mk_ws "$ws2"
cat > "$tmp/write.mm" <<'EOF'
wire openai
rule
  match "\"messages\""
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"made.txt","content":"new\n"}
rule
  match "\"messages\""
  text WROTE
EOF
mm_start "$tmp/write.mm" "$tmp/cap2" 6
mk_cfg "$tmp/c2.json" "$MM_PORT"
(cd "$ws2" && with_deadline 90 "$BIN" --config "$tmp/c2.json" --no-session --auto \
    --journal "$tmp/j2.jsonl" -p 'write it' < /dev/null > /dev/null 2>&1)
mm_stop

if grep -q '"no_changes":false' "$tmp/j2.jsonl" 2>/dev/null; then
    t_ok "a run that wrote a file reports no_changes:false"
else
    t_fail "a run that created made.txt reported \
$(grep -o '"no_changes":[a-z]*' "$tmp/j2.jsonl" 2>/dev/null | tail -1) \
(file exists: $(test -f "$ws2/made.txt" && echo yes || echo no))"
fi

t_done
