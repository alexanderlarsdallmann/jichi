#!/bin/sh
# jhub-verify.sh - Tier J1: does jichi's terminal contract survive a REAL
# JupyterLab terminal?  Root-free, headless, repeatable.
#
# WHY A RIG AND NOT A PARAGRAPH.  docs/plans/2026-08-jichi-with-jupyterhub.md
# lists four things jichi needs from a terminal and marks every one
# [unverified], because nothing had been run.  This runs them.
#
# THE METHOD IS DIFFERENTIAL.  The same jichi turn is driven twice: once on a
# bare pty via tests/tools/ptydrive (the CONTROL), once inside a JupyterLab
# terminal over terminado's websocket (the TREATMENT).  A check compares the
# two.  An absolute assertion would pass while jichi printed nothing; a
# differential one fails when Jupyter is what differs.  docs/TEST_INTEGRITY.md
# is the argument.
#
# WHAT IT HONESTLY DOES NOT COVER: xterm.js.  This speaks the websocket, so it
# tests jupyter-server + terminado completely and the BROWSER not at all.
# jichi binds Ctrl-R and Ctrl-G; a browser binds Ctrl-R to reload and Firefox
# binds Ctrl-G to find-next.  Only a human at a browser can answer that -- see
# the manual checklist this writes at the end.
#
# NOT part of make ci: it needs a Python venv with JupyterLab in it.  An
# operator tier, like scripts/tier-v-terminals.sh.
#
# PROVE THE CHECKS CAN FAIL.  `--negative-control` runs the identical probe
# against a stub that consumes input and emits nothing.  Every check that
# claims something about jichi must go RED; the two that describe the TERMINAL
# (pty, winsize) and the one that describes the environment ($HOME) must stay
# green, because they are true whatever binary runs.  A suite never seen red
# has never been seen working -- docs/SESSION_RUNBOOK.md step 4.
#
# One check cannot be exercised this way and says so: "a pasted block does NOT
# submit" is a NEGATIVE assertion, and a stub satisfies it by doing nothing.
# Its teeth are in the next check, which requires the paste to arrive intact.
#
# Usage:  sh jhub-verify.sh [--dry-run] [--keep] [--negative-control] [--out FILE]
# Env:    JHUB_DIR, JHUB_REPO, JHUB_JICHI, JHUB_MOCKMODEL, JHUB_PTYDRIVE
# Exit:   0 all checks passed   1 a check failed   2 usage   4 setup failed
set -eu

DRY=0
KEEP=0
NEG=0
OUT=""
for a in "$@"; do
    case "$a" in
        --dry-run) DRY=1 ;;
        --keep)    KEEP=1 ;;
        --negative-control) NEG=1 ;;
        --out)     OUT="__next__" ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        -*) echo "unknown option: $a" >&2; exit 2 ;;
        *)  case "$OUT" in __next__) OUT="$a"; continue ;; esac
            echo "unexpected argument: $a" >&2; exit 2 ;;
    esac
done

DIR="${JHUB_DIR:-$HOME/.cache/jichi-jupyterhub}"
STAGE=$(cd "$(dirname "$0")" && pwd)          # scripts/ -- this file lives there now
REPO="${JHUB_REPO:-$(cd "$(dirname "$0")/.." && pwd)}"
BIN="${JHUB_JICHI:-$REPO/jichi}"
MOCK="${JHUB_MOCKMODEL:-$REPO/tests/tools/mockmodel}"
PTY="${JHUB_PTYDRIVE:-$REPO/tests/tools/ptydrive}"
VENV="$DIR/venv"
[ -n "$OUT" ] || OUT="$DIR/results/tier-j1.txt"

J="$DIR/j1"
PORT="${JHUB_LAB_PORT:-18899}"
TOKEN="jichi-j1-$$"

cleanup() {
    [ -n "${LABPID:-}" ] && kill "$LABPID" 2>/dev/null || true
    [ -n "${MMPID:-}"  ] && kill "$MMPID"  2>/dev/null || true
    sleep 1
    [ -n "${LABPID:-}" ] && kill -9 "$LABPID" 2>/dev/null || true
    [ -n "${MMPID:-}"  ] && kill -9 "$MMPID"  2>/dev/null || true
}
trap cleanup EXIT INT TERM

if [ "$DRY" = 1 ]; then
    echo "would verify with:"
    echo "  jichi:      $BIN"
    echo "  mockmodel:  $MOCK"
    echo "  ptydrive:   $PTY"
    echo "  venv:       $VENV"
    echo "  workspace:  $J/ws        (isolated HOME at $J/home)"
    echo "  lab:        127.0.0.1:$PORT"
    echo "  results:    $OUT"
    echo "steps: baseline(ptydrive) -> mockmodel -> jupyter lab -> websocket probe -> notebook cell"
    exit 0
fi

if [ "$NEG" = 1 ]; then
    mkdir -p "$DIR/j1"
    cat > "$DIR/j1/stub-not-jichi" <<'STUB'
#!/bin/sh
# negative control: consumes stdin, produces no jichi marker of any kind.
while IFS= read -r _line; do :; done
STUB
    chmod +x "$DIR/j1/stub-not-jichi"
    BIN="$DIR/j1/stub-not-jichi"
    echo "NEGATIVE CONTROL: driving $BIN instead of jichi" >&2
    echo "  expected: checks about JICHI go red; pty/winsize/\$HOME stay green" >&2
fi

for f in "$BIN" "$MOCK" "$PTY" "$VENV/bin/jupyter" "$VENV/bin/python"; do
    [ -x "$f" ] || { echo "missing or not executable: $f" >&2; exit 4; }
done

if [ "$NEG" = 1 ]; then
    cp "$BIN" "$DIR/.stub-keep"
fi
rm -rf "$J"
mkdir -p "$J/ws" "$J/home" "$J/cap" "$J/out" "$(dirname "$OUT")"
if [ "$NEG" = 1 ]; then
    mv "$DIR/.stub-keep" "$BIN"
    chmod +x "$BIN"
fi
printf 'a note about timeouts\n' > "$J/ws/note.txt"
printf '%s' '{}' > "$J/home/nc.json"

# --- the mock's replies are selected by CONTENT, never by request index -----
# `count N` would misalign the moment a check runs one extra turn, and the
# failure would look like a jichi bug. Content predicates are order-free.
cat > "$J/replies.mm" <<'EOF'
wire openai
# ORDER MATTERS, and this order was earned. `"role":"tool"` first looked
# right -- route the follow-up call after a tool result -- but that string
# stays in the HISTORY for the rest of the session, so it swallowed the paste
# and type-ahead turns too and they were answered with the wrong reply. Rules
# whose predicate is unique to ONE turn must come first.
rule
  match "PASTE_ONE"
  text PASTED_OK
rule
  match "typeahead"
  delay 3000
  text SLOWDONE
rule
  match "\"role\":\"tool\""
  text Here is the answer.
rule
  tool read_file {"path":"note.txt"}
EOF

"$MOCK" --script "$J/replies.mm" --capture "$J/cap" --port-file "$J/.port" \
        --deadline 900 >/dev/null 2>&1 &
MMPID=$!
i=0
while [ ! -s "$J/.port" ]; do
    i=$((i + 1))
    [ "$i" -gt 15 ] && { echo "mockmodel never announced a port" >&2; exit 4; }
    sleep 1
done
MPORT=$(cat "$J/.port")

cat > "$J/config.json" <<EOF
{"models":[{"name":"fast","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MPORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"toolProfile":"full",
"lowResource":false,"maxRetries":0,"contextLimit":100000}
EOF

# --- the CONTROL: the same turn on a bare pty -------------------------------
cat > "$J/drive.pd" <<'EOF'
delay 1000
send "read the note\r"
expect "Here is the answer."
delay 500
send "/exit\r"
waitexit
EOF
( cd "$J/ws" && HOME="$J/home" JC_CONFIG="$J/home/nc.json" \
  LC_ALL=C.UTF-8 LANG=C.UTF-8 TERM=xterm-256color \
  "$PTY" --rows 24 --cols 100 --deadline 60 --log "$J/out/baseline.log" \
         "$J/drive.pd" -- "$BIN" --config "$J/config.json" --no-lite ) \
  >/dev/null 2>&1 || echo "note: baseline ptydrive returned $?" >&2
if [ "$NEG" = 0 ] && [ ! -s "$J/out/baseline.log" ]; then
    echo "the CONTROL produced no transcript -- a differential check against \
it would be meaningless" >&2
    exit 4
fi
touch "$J/out/baseline.log"

# --- the TREATMENT: a real JupyterLab server --------------------------------
# Started FROM the workspace: terminado inherits the server's cwd, not
# --ServerApp.root_dir. Observed, not assumed -- a terminal opened against a
# server launched elsewhere lands in that elsewhere.
( cd "$J/ws" && HOME="$J/home" \
  JUPYTER_CONFIG_DIR="$J/home/.jupyter" JUPYTER_DATA_DIR="$J/home/.jupyter-data" \
  nohup "$VENV/bin/jupyter" lab --no-browser \
      --ServerApp.ip=127.0.0.1 --ServerApp.port="$PORT" \
      --ServerApp.token="$TOKEN" --ServerApp.root_dir="$J/ws" \
      --ServerApp.open_browser=False --ServerApp.disable_check_xsrf=True \
      > "$J/out/lab.log" 2>&1 & echo $! > "$J/.labpid" )
LABPID=$(cat "$J/.labpid")
i=0
while ! curl -sf -o /dev/null "http://127.0.0.1:$PORT/api?token=$TOKEN" 2>/dev/null; do
    i=$((i + 1))
    [ "$i" -gt 60 ] && { echo "jupyter lab never came up" >&2; tail -5 "$J/out/lab.log" >&2; exit 4; }
    sleep 1
done

TNAME=$(curl -sS -X POST "http://127.0.0.1:$PORT/api/terminals?token=$TOKEN" \
        | sed -n 's/.*"name": *"\([^"]*\)".*/\1/p')
[ -n "$TNAME" ] || { echo "could not create a terminal" >&2; exit 4; }

{
    printf 'Tier J1 -- jichi in a JupyterLab terminal\n'
    printf 'date:      %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf 'host:      %s\n' "$(uname -srm)"
    printf 'jichi:     %s\n' "$("$BIN" --version 2>&1 | head -1)"
    printf 'jupyterlab %s / jupyter-server %s\n' \
        "$("$VENV/bin/jupyter-lab" --version 2>&1 | head -1)" \
        "$(curl -sS "http://127.0.0.1:$PORT/api?token=$TOKEN" | sed -n 's/.*"version": *"\([^"]*\)".*/\1/p')"
    printf 'method:    websocket to terminado; CONTROL = the same turn on a bare pty\n'
    printf 'NOT covered: xterm.js and browser key bindings (see the checklist below)\n\n'
} > "$OUT"

set +e
[ "$NEG" = 1 ] && JHUB_PROBE_TIMEOUT="${JHUB_PROBE_TIMEOUT:-8}" && export JHUB_PROBE_TIMEOUT
"$VENV/bin/python" "$STAGE/jhub-lab-probe.py" "$PORT" "$TOKEN" "$TNAME" \
    "$BIN" "$J/config.json" "$J/ws" "$J/home" "$J/out" "$J/out/baseline.log" \
    2>&1 | tee -a "$OUT"
RC=${PIPESTATUS:-$?}
set -e

# --- the notebook-cell shape (Shape B), executed for real -------------------
printf '\n--- Shape B: jichi from a notebook cell ---\n' | tee -a "$OUT"
cat > "$J/ws/cell.ipynb" <<EOF
{"cells":[{"cell_type":"code","execution_count":null,"id":"c0","metadata":{},
"outputs":[],"source":["import subprocess, json\n",
"p = subprocess.run(['$BIN','--config','$J/config.json','--no-lite','-q','-p',\n",
"                    'read the note','--output','jsonl','--no-session'],\n",
"                   capture_output=True, text=True)\n",
"types = [json.loads(l)['type'] for l in p.stdout.splitlines() if l.strip().startswith('{')]\n",
"print('EVENTS', ' '.join(types))\n"]}],
"metadata":{"kernelspec":{"display_name":"Python 3","language":"python","name":"python3"}},
"nbformat":4,"nbformat_minor":5}
EOF
if ( cd "$J/ws" && HOME="$J/home" timeout 180 "$VENV/bin/jupyter" nbconvert \
        --to notebook --execute --output executed.ipynb cell.ipynb \
        > "$J/out/nbconvert.log" 2>&1 ); then
    EV=$("$VENV/bin/python" - "$J/ws/executed.ipynb" <<'PY'
import json, sys
nb = json.load(open(sys.argv[1]))
for c in nb["cells"]:
    for o in c.get("outputs", []):
        print("".join(o.get("text", [])), end="")
PY
)
    if printf '%s' "$EV" | grep -q "EVENTS.*done"; then
        printf 'ok - a notebook cell drives headless jichi and gets parseable jsonl\n#   %s\n' \
            "$(printf '%s' "$EV" | tr -d '\n')" | tee -a "$OUT"
    else
        printf 'not ok - the cell ran but produced no parseable jsonl\n#   %s\n' \
            "$(printf '%s' "$EV" | tr -d '\n')" | tee -a "$OUT"
        RC=1
    fi
else
    printf 'not ok - nbconvert could not execute the cell\n' | tee -a "$OUT"
    sed 's/^/#   /' "$J/out/nbconvert.log" | tail -8 | tee -a "$OUT"
    RC=1
fi

cat >> "$OUT" <<'EOF'

--- What this rig CANNOT answer: the browser half ---
Run these by hand, in a browser, against a JupyterLab terminal. Each is a key
jichi binds that a browser also binds; the question is who wins.

  [ ] Ctrl-R   jichi: reverse history search. Browser: reload the page.
  [ ] Ctrl-G   jichi: ghost-text suggestion. Firefox: find-next.
  [ ] Ctrl-W   jichi/readline: delete word. Some browsers: close the tab.
  [ ] Ctrl-C   jichi: interrupt the turn (twice at an empty prompt = quit).
  [ ] Ctrl-D   jichi: end input / quit at an empty prompt.
  [ ] Ctrl-L   jichi/terminal: clear.
  [ ] paste     Ctrl-Shift-V or right-click: does a 3-line paste stay one line?
  [ ] resize    drag the browser window mid-turn: does the redraw follow?
  [ ] glyphs    are the tool lines drawn as UTF-8 (a font question, not a jichi one)?
EOF

printf '\nwrote %s\n' "$OUT"
[ "$KEEP" = 1 ] || rm -rf "$J/cap"
if [ "$NEG" = 1 ]; then
    # Inverted: under the negative control a GREEN suite is the failure.
    if [ "$RC" -ne 0 ]; then
        printf '\nnegative control OK: the suite went red against a stub\n'
        exit 0
    fi
    printf '\nNEGATIVE CONTROL FAILED: every check passed against a stub that\n'
    printf 'emits nothing. The suite is not measuring jichi.\n'
    exit 1
fi
exit "$RC"
