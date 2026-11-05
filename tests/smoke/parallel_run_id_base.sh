#!/bin/sh
# M811 (P3): a spawn_parallel write batch names its worktree base by the RUN ID
# (worktrees/par-<run>), not by pid. The G3 defect (confirmed by repro) was that in
# a container every lead is the same pid, so a pid-named base let a DEAD run's orphan
# collide with the next run's and break its isolation. A run id is unique per
# invocation, so bases never collide. The base also carries an fcntl lock (.lock,
# held for the batch, dies with the holder) and a manifest (batch.json, written
# before the worktrees) -- the two things salvage (M812) needs to tell a dead run's
# trees from a live one's and to name its tasks.
#
# Observed on an ORPHAN: the batch is killed mid-run (parent dies, no teardown), so
# the base persists to be inspected. Before M811 the base was worktrees/<pid> with
# no manifest and no lock -- the red half.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)
marker="RUNIDBASE_kept"
cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  match "LEADPROMPT"
  match "\"role\":\"tool\""
  text LEADDONE
rule
  match "LEADPROMPT"
  tool spawn_parallel {"tasks":[{"task":"TASK_ORPH: write then wait","write":true}]}
rule
  match "TASK_ORPH"
  tool run_terminal_command {"command":"printf '$marker\\n' > orph.txt && sleep 30"}
EOF
mm_start_unbounded "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"preserveDiscarded":true}
EOF
# Own process group (setsid) so we can kill the WHOLE run, parent and children,
# leaving no teardown -- the dead-parent case.
( cd "$ws" && exec setsid "$BIN" --config "$tmp/config.json" -q --no-session --auto \
    -p "LEADPROMPT run a writer" < /dev/null > "$tmp/out.txt" 2>&1 ) &
lead=$!
# wait for the base worktree to exist, then kill the group
n=0; while [ -z "$(find "$HOME/.jichi.d/worktrees" -name 'wt-0' 2>/dev/null)" ] && [ "$n" -lt 150 ]; do
    n=$((n+1)); sleep 0.1
done
pgid=$(ps -o pgid= -p "$lead" 2>/dev/null | tr -d ' ')
[ -n "$pgid" ] && kill -KILL -"$pgid" 2>/dev/null
kill -KILL "$lead" 2>/dev/null
sleep 1
mm_stop 2>/dev/null

wtroot="$HOME/.jichi.d/worktrees"
basedir=$(find "$wtroot" -maxdepth 1 -mindepth 1 -type d 2>/dev/null | head -1)
bn=$(basename "$basedir" 2>/dev/null)
manifest="$basedir/batch.json"
mtask=0
[ -f "$manifest" ] && grep -q 'TASK_ORPH' "$manifest" && mtask=1
echo "# base dir: ${basedir:-none} (basename '$bn'); manifest=$([ -f "$manifest" ] && echo yes || echo no); manifest names task=$mtask; lock=$([ -f "$basedir/.lock" ] && echo yes || echo no)"
if [ -n "${E1_RES:-}" ] && [ -f "$manifest" ]; then mkdir -p "$E1_RES"; cp -p "$manifest" "$E1_RES/"; fi

[ -n "$basedir" ] && [ -e "$basedir/wt-0" ] \
    && t_ok "instrument: an orphan base with a worktree persisted after the parent was killed mid-batch" \
    || t_fail "instrument: no orphan base under $wtroot (basedir='$basedir')"
case "$bn" in par-*) t_ok "the base is named by the run id (par-...), not a bare pid" ;;
    *) t_fail "the base is '$bn' -- not par-<run> (a pid-named base collides across runs: G3)" ;; esac
[ -f "$manifest" ] && [ "$mtask" = 1 ] \
    && t_ok "the manifest batch.json was written first and records the task" \
    || t_fail "no batch.json manifest naming the task (salvage would have nothing to name the orphan)"
[ -f "$basedir/.lock" ] \
    && t_ok "the base carries the fcntl lock file (.lock) -- the liveness marker salvage reads" \
    || t_fail "no .lock in the base"
t_done
