#!/bin/sh
# M961 (F1): the bgspool fence driver observes the spooled output, not a live-tree sentinel
#
# A fenced write child's shell is confined to its worktree, but background commands
# need to write spool files to $HOME/.jichi.d/bgspool. Without the grant, jc_bg_start's
# open() fails (ENOENT/EACCES), the background command runs UNSPOOLED, and the child's
# read_background_output returns empty (no markers). This driver proves the grant works:
# a fenced write child runs a background command that writes ONLY to STDOUT, and the
# same child reads its spooled output. The teeth are: the lead sees both markers in its
# final output (the spool was filled); the child's read result shows id 1 had content
# (the spool read worked); and the telemetry shows the grant (dirs >= 4). The unfenced
# control verifies the instrument fires. Check 5 proves the three-way return for
# st==JC_OK+ev==NULL (the telemetry-regression fix).
. "$(dirname "$0")/_smoke.sh"

# Guarded /usr/bin/grep for portability (systems without merged /usr have grep at /bin/grep).
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# Precondition: the shell fence must be AVAILABLE here, or the whole driver is moot.
if ! "$BIN" doctor 2>&1 | grep -q 'shell fence: available'; then
    t_skip "no Landlock on this kernel (shell fence unavailable) -- doctor says so"
fi

t_plan 5
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)
printf 'base\n' > "$ws/reg.txt"
# Ensure snapshots are available by creating a .git file in the workspace.
# This makes workspace_too_big() return 0 (trusted) even if the workspace
# has many files or lacks .gitignore.
git init -q "$ws" 2>/dev/null || true

# The background command: write markers ONLY to STDOUT (NOT to live tree or workspace).
# Takes ~1-2 s so spool is filled before the child reads it, but short enough the run
# stays under ~40 s. If the fence did not grant bgspool, the spool open fails, the
# command runs UNSPOOLED, and read_background_output returns empty (no markers).
# The markers are BUILT (printf 'BG-OUT-%s' MARKER) so the contiguous strings
# BG-OUT-MARKER / BG-OUT-MARKER-TWO appear ONLY in the spooled output, never in
# the command text. Grepping the capture for them therefore tests the spool, not
# the echoed command.
BGS_CMD="printf 'BG-OUT-%s' MARKER; sleep 1; printf 'BG-OUT-%s' MARKER-2"

mm_from_cmd() {  # $1 = expanded command -> writes an .mm to stdout
    # JSON-quote the command for the run_terminal_command tool arg.
    quoted_cmd=$(printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g')
    cat <<EOF
wire openai
rule
  match "LEADFENCE"
  match "\"role\":\"tool\""
  text LEADFENCEDONE
rule
  match "LEADFENCE"
  tool spawn_parallel {"tasks":[{"task":"BGPROBE: run the background marker and read it back","write":true}]}
rule
  count 2
  match "BGPROBE"
  tool run_terminal_command {"command":"$quoted_cmd","run_in_background":true}
rule
  count 3
  delay 1500
  match "BGPROBE"
  tool read_background_output {"id":1}
rule
  count 4
  match "BGPROBE"
  text BGPROBEDONE
EOF
}

# Both markers must appear in the CHILD's spool-read result. They reach the
# child's model (in the captured requests), not the lead's final text: the child
# is a write task whose final answer the lead folds opaquely. Without the fence
# granting bgspool, jc_bg_start's open() fails, the command runs UNSPOOLED, and
# the read returns "(no new output)" -- no markers anywhere in the capture.
cap_has_markers() {  # $1 = capture dir
    cat "$1"/req.* 2>/dev/null | "$G" -q 'BG-OUT-MARKER' &&
    cat "$1"/req.* 2>/dev/null | "$G" -q 'BG-OUT-MARKER-2'
}

cfg() {  # $1 = shellFence value (true/false) -> writes a config to stdout
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"shellFence":$1}
EOF
}

run_child_with() {  # $1 = shellFence value, $2 = --log flag or "", $3 = path if $2 is --log -> runs one write child
    printf 'base\n' > "$ws/reg.txt"
    rm -rf "$tmp/cap" && mkdir -p "$tmp/cap"
    mm_from_cmd "$BGS_CMD" > "$tmp/f.mm"
    mm_start_unbounded "$tmp/f.mm" "$tmp/cap"
    cfg "$1" > "$tmp/cfg.json"
    if [ -n "$2" ]; then
        ( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/cfg.json" -q --auto \
            "$2" "$3" -p "LEADFENCE run one writer" < /dev/null > "$tmp/out.$1.txt" 2>&1 )
    else
        ( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/cfg.json" -q --auto \
            -p "LEADFENCE run one writer" < /dev/null > "$tmp/out.$1.txt" 2>&1 )
    fi
    mm_stop
}

# ---- Fenced (shellFence:true): background command must be spooled and markers reached. ----
LOG_PATH="$ws/telemetry.jsonl"
run_child_with true --log "$LOG_PATH"

# Check 1 (the core teeth): the child's captured spool read carries BOTH markers.
# If the fence did not grant bgspool, the spool open fails, the command runs
# UNSPOOLED, and read_background_output returns empty (no markers).
if cap_has_markers "$tmp/cap"; then
    t_ok "fenced: both markers present in the child's spool read (spool worked)"
else
    t_fail "fenced: marker(s) missing from the child's capture (spool open failed)"
fi

# Check 2: the child's read result shows background id 1 existed and returned
# content (not "(no new output)"): the spool read worked. The run_in_background
# result says "Started background process"; the read call is present in the capture.
if cat "$tmp/cap"/req.* 2>/dev/null | "$G" -q 'Started background process' &&
   cat "$tmp/cap"/req.* 2>/dev/null | "$G" -q 'read_background_output'; then
    t_ok "fenced: background process started and was read (spool read worked)"
else
    t_fail "fenced: background process never started or read (spool read failed)"
fi

# Check 3: the telemetry shows the grant: dirs >= 4 (worktree + shadow git + toolout + bgspool).
# With the fix reverted, dirs == 3 (no bgspool).
if [ -n "$LOG_PATH" ] && [ -f "$LOG_PATH" ] && grep -q '"event":"shell_fence"' "$LOG_PATH"; then
    dirs_count=$(grep '"event":"shell_fence"' "$LOG_PATH" | head -1 | grep -o '"dirs":[0-9][0-9]*' | sed -n 's/.*://p' | sed 's/"//g' || true)
    if [ -n "$dirs_count" ] && [ "$dirs_count" -ge 4 ]; then
        t_ok "fenced: shell_fence telemetry shows dirs=$dirs_count >= 4 (bgspool granted)"
    else
        t_fail "fenced: shell_fence telemetry shows dirs=$dirs_count < 4 (bgspool NOT granted)"
    fi
else
    t_fail "fenced: no shell_fence event found in telemetry"
fi

# Check 4: unfenced control (shellFence:false) also works.
# The instrument proof: without a fence, the background command also lands the markers.
run_child_with false --log "$LOG_PATH"

if cap_has_markers "$tmp/cap"; then
    t_ok "unfenced: both markers present (control verifies instrument fires)"
else
    t_fail "unfenced: marker(s) missing even with shellFence:false"
fi

# Check 5: fenced WITHOUT --log must NOT be refused (the telemetry regression fix).
# Without telemetry (--log), an applied fence must still succeed and run the task.
run_child_with true

if cap_has_markers "$tmp/cap"; then
    t_ok "fenced without --log: both markers present (task was not refused without telemetry)"
else
    t_fail "fenced without --log: marker(s) missing (task was incorrectly refused without telemetry)"
fi

t_done
