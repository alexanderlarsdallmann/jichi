#!/bin/sh
# loop.sh -- a task-queue supervisor for one or more autonomous jichi instances.
#
# Works a directory-backed queue of task files, running each as a bounded,
# non-interactive `jichi --auto` turn and routing on the EXIT CODE
# (authoritative; jq is optional and only enriches logging). Safe to run as
# several instances against the same queue: claiming a task is an atomic
# rename(), so no two instances ever grab the same one -- no lock file needed.
#
# Queue layout (created on demand), all under $QUEUE:
#   pending/   tasks waiting to run        (drop *.task files here)
#   running/   claimed, in flight
#   done/      completed (exit 0)
#   failed/    quarantined (exhausted retries, or a misconfig/usage error)
#   attempts/  per-task retry counters
#
# Per-run BOUNDS live here, not in the config: the config sets the security
# posture + tools; the supervisor decides budget/verify/scope per task-set.
# See docs/AUTONOMOUS_LOOPS.md.
set -eu

# ---- configuration (env-overridable) ---------------------------------------
JICHI_BIN="${JICHI_BIN:-jichi}"
JICHI_CONFIG="${JICHI_CONFIG:?set JICHI_CONFIG to your hardened config path}"
QUEUE="${QUEUE:-./queue}"
WORKSPACE="${WORKSPACE:-.}"

BUDGET_TOKENS="${BUDGET_TOKENS:-400k}"   # per-run token budget
DEADLINE="${DEADLINE:-30m}"              # per-run wall-clock deadline
MAX_TOOL_CALLS="${MAX_TOOL_CALLS:-80}"   # per-run tool-call cap
VERIFY="${VERIFY:-}"                     # optional per-run verify command
EDIT_SCOPE="${EDIT_SCOPE:-}"             # optional per-run edit-scope glob

MAX_ATTEMPTS="${MAX_ATTEMPTS:-2}"        # retries before quarantine
BACKOFF="${BACKOFF:-30}"                 # seconds to wait after a failure
POLL="${POLL:-10}"                       # seconds between empty-queue polls
RUN_ONCE="${RUN_ONCE:-0}"                # 1 = drain the queue and exit
COST_CAP_TOKENS="${COST_CAP_TOKENS:-0}"  # 0 = no loop-wide cap
JOURNAL_DIR="${JOURNAL_DIR:-$HOME/.jichi.d/runs}"

mkdir -p "$QUEUE/pending" "$QUEUE/running" "$QUEUE/done" "$QUEUE/failed" \
         "$QUEUE/attempts" "$JOURNAL_DIR"

# Optional posture preflight (M158b): refuse to start the loop on a config
# that is unsafe for unattended operation (root, privilegedCommands:allow,
# audit off, fence off). Opt-in because `doctor` also probes each configured
# server's reachability, which a fully-offline queue host may not want.
if [ "${PREFLIGHT:-0}" = "1" ]; then
  "$JICHI_BIN" --config "$JICHI_CONFIG" doctor --unattended >&2 || {
    echo "loop: preflight failed -- unsafe unattended posture; not starting" >&2
    exit 2
  }
fi

spent_tokens=0

log() { printf '%s loop[%s]: %s\n' "$(date -u +%H:%M:%SZ)" "$$" "$*" >&2; }

# ---- claim one task via atomic rename; echo the claimed path or nothing -----
claim_task() {
  for f in "$QUEUE"/pending/*.task; do
    [ -e "$f" ] || continue                 # empty glob
    name=$(basename "$f")
    dest="$QUEUE/running/$name.$$"
    if mv "$f" "$dest" 2>/dev/null; then     # atomic: only one winner
      printf '%s\n' "$dest"
      return 0
    fi
  done
  return 1
}

attempts_of() { c=$(cat "$QUEUE/attempts/$1" 2>/dev/null || echo 0); echo "$c"; }
bump_attempts() { echo "$(( $(attempts_of "$1") + 1 ))" >"$QUEUE/attempts/$1"; }

# ---- run one claimed task ---------------------------------------------------
run_task() {
  running="$1"
  base=$(basename "$running")
  base=${base%.$$}                          # strip our pid suffix
  prompt=$(cat "$running")
  jpath="$JOURNAL_DIR/$base-$(date -u +%s).jsonl"
  out=$(mktemp)

  set -- "$JICHI_BIN" --config "$JICHI_CONFIG" --auto \
         --output json -q --no-session \
         --budget-tokens "$BUDGET_TOKENS" --deadline "$DEADLINE" \
         --max-tool-calls "$MAX_TOOL_CALLS" --journal "$jpath"
  [ -n "$VERIFY" ]     && set -- "$@" --verify "$VERIFY"
  [ -n "$EDIT_SCOPE" ] && set -- "$@" --edit-scope "$EDIT_SCOPE"
  set -- "$@" -p "$prompt"

  log "run $base (attempt $(( $(attempts_of "$base") + 1 )))"
  rc=0
  ( cd "$WORKSPACE" && "$@" ) >"$out" 2>>"$jpath.err" || rc=$?

  # Optional: enrich logs + accumulate a loop-wide token total with jq.
  if command -v jq >/dev/null 2>&1; then
    used=$(jq -r '.tokens.input + .tokens.output // 0' "$out" 2>/dev/null || echo 0)
    reason=$(jq -r '.stop_reason // "?"' "$out" 2>/dev/null || echo '?')
    case "$used" in ''|*[!0-9]*) used=0 ;; esac
    spent_tokens=$(( spent_tokens + used ))
    log "  -> rc=$rc stop_reason=$reason used=$used total=$spent_tokens"
  else
    log "  -> rc=$rc"
  fi

  cp "$out" "$QUEUE/$base.result.json" 2>/dev/null || true
  rm -f "$out"

  # ---- route on the exit code (see docs/SCRIPTING.md) ----
  case "$rc" in
    0)                                        # success
      mv "$running" "$QUEUE/done/$base"
      rm -f "$QUEUE/attempts/$base"
      log "  done: $base" ;;
    2)                                        # usage/misconfig -- do not retry
      mv "$running" "$QUEUE/failed/$base"
      log "  FAILED (misconfig, exit 2): $base -- quarantined" ;;
    130|143)                                  # SIGINT / SIGTERM -- shutdown
      mv "$running" "$QUEUE/pending/$base"    # requeue, we're stopping
      log "  interrupted (exit $rc); requeued $base"
      return 2 ;;                             # signal the main loop to stop
    *)                                        # verify_failed / error / budget
      bump_attempts "$base"
      if [ "$(attempts_of "$base")" -ge "$MAX_ATTEMPTS" ]; then
        mv "$running" "$QUEUE/failed/$base"
        log "  FAILED (exit $rc, $(attempts_of "$base") attempts): quarantined $base"
      else
        mv "$running" "$QUEUE/pending/$base"
        log "  retry later (exit $rc); backoff ${BACKOFF}s"
        sleep "$BACKOFF"
      fi ;;
  esac
  return 0
}

# ---- main loop --------------------------------------------------------------
log "supervisor up: queue=$QUEUE workspace=$WORKSPACE bin=$JICHI_BIN"
while :; do
  if [ "$COST_CAP_TOKENS" -gt 0 ] && [ "$spent_tokens" -ge "$COST_CAP_TOKENS" ]; then
    log "loop-wide cost cap reached ($spent_tokens >= $COST_CAP_TOKENS); stopping"
    break
  fi
  if task=$(claim_task); then
    run_task "$task" || { [ $? -eq 2 ] && break; }
  else
    [ "$RUN_ONCE" = "1" ] && { log "queue drained; exiting (RUN_ONCE)"; break; }
    sleep "$POLL"
  fi
done
