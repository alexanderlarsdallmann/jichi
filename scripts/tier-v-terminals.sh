#!/bin/sh
# tier-v-terminals.sh - run Tier V row V6 ("terminal reality") from
# docs/plans/2026-07-hardware-testing.md against REAL X11 terminal emulators.
#
# The smoke tier already drives the TUI through a pty (paste.sh, typed.sh,
# typeahead.sh, tui_basic.sh) and that is the right instrument for the line
# editor's logic. What it cannot answer is what a real terminal EMULATOR does:
# whether xterm's bracketed paste wraps the way M156 assumes, whether dragging
# a window edge delivers the SIGWINCH the redraw expects, whether Ctrl-C from a
# keyboard is the same thing as a pty write. V6 exists for those questions.
#
# Six checks per emulator, straight from the plan:
#   1  prompt renders, /help readable
#   2  type-ahead: text typed during a turn is visible and queued  (M254/M257)
#   3  paste: a three-line block arrives intact                    (M156)
#   4  resize mid-turn, then again at the prompt                   (SIGWINCH)
#   5  NO_COLOR=1 + LC_ALL=C: ASCII fallbacks, working line survives (M257)
#   6  Ctrl-C mid-turn, Ctrl-C twice at an empty prompt, Ctrl-D
#
# Usage:
#   scripts/tier-v-terminals.sh                  # every emulator found
#   scripts/tier-v-terminals.sh xterm            # just one
#   scripts/tier-v-terminals.sh --dry-run        # print the plan, run nothing
#   scripts/tier-v-terminals.sh --keep           # keep captures + screenshots
#
# Wants: an X display, xwininfo, `script`(1), python3 (only as a clipboard
# owner -- a real paste needs a real selection owner), and tests/tools/xdrive
# plus tests/tools/mockmodel (`make xdrive smoke-tools`).
#
# NOT part of make ci / make check-target: it needs a live X server and it
# steals the keyboard focus while it runs. Deliberately an operator-run tier,
# like scripts/tier-v-vm.sh.
#
# It does NOT source tests/smoke/_smoke.sh even though it reuses its patterns:
# that lib pins LANG=C, LC_ALL=C and NO_COLOR=1 for determinism, and the real
# UTF-8 colour terminal it thereby neutralises is precisely what V6 tests.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN="${JC_V6_BIN:-$ROOT/jichi}"
TOOLS="$ROOT/tests/tools"
XDRIVE="$TOOLS/xdrive"
MOCK="$TOOLS/mockmodel"

DRY=0
KEEP=0
WANT=""

for arg in "$@"; do
    case "$arg" in
        --dry-run) DRY=1 ;;
        --keep)    KEEP=1 ;;
        -h|--help) sed -n '2,34p' "$0"; exit 0 ;;
        -*)        echo "unknown option: $arg" >&2; exit 2 ;;
        *)         WANT="$WANT $arg" ;;
    esac
done

OUT="${JC_V6_OUT:-$ROOT/.v6-results}"
N_OK=0
N_FAIL=0
N_SKIP=0

say()  { echo "== $*"; }
ok()   { N_OK=$((N_OK + 1));   echo "ok - $*";      echo "ok   - $*" >> "$OUT/results.txt"; }
bad()  { N_FAIL=$((N_FAIL + 1)); echo "not ok - $*"; echo "FAIL - $*" >> "$OUT/results.txt"; }
skip() { N_SKIP=$((N_SKIP + 1)); echo "skip - $*";   echo "skip - $*" >> "$OUT/results.txt"; }

# ------------------------------------------------------------------ preflight

[ -n "${DISPLAY:-}" ] || { echo "v6: no DISPLAY -- this row needs a live X server" >&2; exit 1; }
for t in xwininfo script python3 import; do
    command -v "$t" >/dev/null 2>&1 || { echo "v6: missing $t" >&2; exit 1; }
done
[ -x "$BIN" ]    || { echo "v6: build jichi first (make)" >&2; exit 1; }
[ -x "$XDRIVE" ] || { echo "v6: build the key injector first (make xdrive)" >&2; exit 1; }
[ -x "$MOCK" ]   || { echo "v6: build the mock model first (make smoke-tools)" >&2; exit 1; }
"$XDRIVE" probe >/dev/null || exit 1

# Which emulators are actually here. The plan names gnome-terminal OR the
# desktop's own; on XFCE that is xfce4-terminal.
EMUS=""
for e in xterm xfce4-terminal gnome-terminal; do
    command -v "$e" >/dev/null 2>&1 && EMUS="$EMUS $e"
done
[ -n "$WANT" ] && EMUS="$WANT"
[ -n "$EMUS" ] || { echo "v6: no supported terminal emulator found" >&2; exit 1; }

say "V6 -- terminal reality"
echo "   host      : $(uname -n)  $(. /etc/os-release 2>/dev/null; echo "${PRETTY_NAME:-?}")"
echo "   display   : $DISPLAY   desktop: ${XDG_CURRENT_DESKTOP:-?}"
echo "   emulators :$EMUS"
echo "   locale    : ${LANG:-?}"
echo "   results   : $OUT"
echo

if [ "$DRY" -eq 1 ]; then
    for e in $EMUS; do
        echo "[dry run] $e: checks 1..6 (help, type-ahead, paste, resize, no-color, signals)"
    done
    echo "[dry run] the Linux virtual console is NOT covered here -- see the plan"
    exit 0
fi

rm -rf "$OUT"; mkdir -p "$OUT"
: > "$OUT/results.txt"
{
    echo "Tier V row V6 -- terminal reality"
    echo "run: $(date -u '+%Y-%m-%dT%H:%M:%SZ')  host: $(uname -n)"
    echo "os: $(. /etc/os-release 2>/dev/null; echo "${PRETTY_NAME:-?}")  desktop: ${XDG_CURRENT_DESKTOP:-?}"
    echo "emulators:$EMUS"
    echo
} >> "$OUT/results.txt"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/jichi_v6.XXXXXX")
cleanup() {
    [ -n "${MM_PID:-}" ] && kill "$MM_PID" 2>/dev/null
    [ -n "${TERM_PID:-}" ] && kill "$TERM_PID" 2>/dev/null
    [ -n "${CLIP_PID:-}" ] && kill "$CLIP_PID" 2>/dev/null
    [ "$KEEP" -eq 1 ] || rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

# ------------------------------------------------------------------- helpers

# mock_start <script-file> <capture-dir> -> MM_PORT
mock_start() {
    rm -f "$2/.port"
    mkdir -p "$2"
    "$MOCK" --script "$1" --capture "$2" --port-file "$2/.port" \
        --deadline 180 >/dev/null 2>&1 &
    MM_PID=$!
    _i=0
    while [ ! -s "$2/.port" ]; do
        _i=$((_i + 1))
        [ "$_i" -gt 15 ] && { echo "v6: mockmodel never announced a port" >&2; return 1; }
        sleep 1
    done
    MM_PORT=$(cat "$2/.port")
}
mock_stop() {
    [ -n "${MM_PID:-}" ] && { kill "$MM_PID" 2>/dev/null; wait "$MM_PID" 2>/dev/null; }
    MM_PID=""
}

write_config() {  # write_config <path> <port>
    cat > "$1" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$2/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF
}

# term_launch <emulator> <title> <capture> <extra-env> <jichi-args>
# Runs jichi under script(1) so we keep the exact byte stream it emitted while
# its controlling terminal is still the real emulator: script propagates the
# window size and forwards SIGWINCH, so resize and bracketed paste both stay
# genuine.
term_launch() {
    _te=$1; _tt=$2; _tcap=$3; _tenv=$4; _targs=$5
    _cmd="cd '$WS' && $_tenv exec '$BIN' --config '$CFG' --no-route $_targs"
    case "$_te" in
        xterm)
            # Ctrl-Shift-V is not an xterm default; bind it so the paste check
            # uses the CLIPBOARD the same way it does on VTE terminals.
            xterm -T "$_tt" -geometry 110x32+40+40 \
                -xrm 'XTerm*VT100.translations: #override Ctrl Shift <Key>V: insert-selection(CLIPBOARD)' \
                -e script -q --flush -c "$_cmd" "$_tcap" >/dev/null 2>&1 &
            ;;
        xfce4-terminal|gnome-terminal)
            # --disable-server: a new process, so our env actually applies
            # instead of being handed to an already-running factory.
            "$_te" --disable-server -T "$_tt" --geometry=110x32+40+40 \
                -x script -q --flush -c "$_cmd" "$_tcap" >/dev/null 2>&1 &
            ;;
        *) echo "v6: unsupported emulator $_te" >&2; return 1 ;;
    esac
    TERM_PID=$!
    WID=""
    _i=0
    while [ -z "$WID" ]; do
        _i=$((_i + 1))
        [ "$_i" -gt 20 ] && { echo "v6: window '$_tt' never appeared" >&2; return 1; }
        sleep 1
        WID=$(xwininfo -root -tree 2>/dev/null | grep "$_tt" \
              | grep -oE '0x[0-9a-f]+' | head -1)
    done
    "$XDRIVE" focus "$WID" sleep 700
}

term_close() {
    [ -n "${TERM_PID:-}" ] && { kill "$TERM_PID" 2>/dev/null; wait "$TERM_PID" 2>/dev/null; }
    TERM_PID=""
}

# Strip CSI/OSC escapes so an assertion reads the text a human would see.
plain() { _esc=$(printf '\033'); _bel=$(printf '\007'); sed "s/${_esc}\][^${_bel}]*${_bel}//g; s/${_esc}\[[0-9;?]*[a-zA-Z]//g; s/${_esc}[()][B0]//g" "$1"; }   # M471: no GNU-only hex escape

shot() { import -window "$WID" "$OUT/$1.png" 2>/dev/null || true; }

# A real paste needs a real selection owner; tkinter is the shortest one that
# actually holds CLIPBOARD (xclip/xsel are not installed here).
clip_own() {
    python3 - "$1" <<'PY' &
import sys, tkinter
r = tkinter.Tk(); r.withdraw()
r.clipboard_clear(); r.clipboard_append(sys.argv[1])
r.after(60000, r.destroy); r.mainloop()
PY
    CLIP_PID=$!
    sleep 2
}
clip_drop() {
    [ -n "${CLIP_PID:-}" ] && { kill "$CLIP_PID" 2>/dev/null; wait "$CLIP_PID" 2>/dev/null; }
    CLIP_PID=""
}

new_case() {  # new_case <name> -> sets WS, CFG, CAP, CAPDIR
    CASE=$1
    WS="$WORK/$EMU-$CASE-ws"; CAPDIR="$WORK/$EMU-$CASE-cap"
    CFG="$WORK/$EMU-$CASE.json"; CAP="$WORK/$EMU-$CASE.raw"
    mkdir -p "$WS" "$CAPDIR"
    rm -f "$CAP"
}

# ============================================================ the six checks

check_help() {
    new_case help
    printf 'rule\n  text Hello from the mock.\n' > "$WORK/$CASE.mm"
    mock_start "$WORK/$CASE.mm" "$CAPDIR" || return
    write_config "$CFG" "$MM_PORT"
    term_launch "$EMU" "V6HELP$$" "$CAP" "" "" || { mock_stop; return; }
    "$XDRIVE" slow 25 "/help" key Return sleep 1800
    shot "$EMU-1-help"
    "$XDRIVE" slow 25 "/exit" key Return sleep 1200
    term_close; mock_stop

    if plain "$CAP" | grep -q "Mode & model" && plain "$CAP" | grep -q "Workspace & undo"; then
        ok "$EMU/1 prompt renders and /help is readable"
    else
        bad "$EMU/1 /help did not render its sections"
    fi
}

check_typeahead() {
    new_case typeahead
    cat > "$WORK/$CASE.mm" <<'EOF'
wire openai
rule
  count 1
  delay 4000
  tool list_files {"path":"."}
rule
  match "[operator] also read the docs"
  text STEER_OK
rule
  text STEER_MISSED
EOF
    mock_start "$WORK/$CASE.mm" "$CAPDIR" || return
    write_config "$CFG" "$MM_PORT"
    term_launch "$EMU" "V6TYPE$$" "$CAP" "" "--type-ahead" || { mock_stop; return; }
    "$XDRIVE" slow 25 "hello" key Return sleep 1200 \
              slow 40 "also read the docs" sleep 600
    shot "$EMU-2-typeahead"
    "$XDRIVE" key Return sleep 6000
    "$XDRIVE" slow 25 "/exit" key Return sleep 1500
    term_close; mock_stop

    if plain "$CAP" | grep -qi "queued"; then
        ok "$EMU/2 text typed during a turn is echoed and queued"
    else
        bad "$EMU/2 no 'queued' confirmation for the typed-ahead line"
    fi
    if [ -f "$CAPDIR/req.2" ] && grep -q "\[operator\] also read the docs" "$CAPDIR/req.2"; then
        ok "$EMU/2b the typed text reached the next model call as [operator]"
    else
        bad "$EMU/2b the steering message never made it onto the wire"
    fi
}

check_paste() {
    new_case paste
    cat > "$WORK/$CASE.mm" <<'EOF'
wire openai
rule
  match "line1"
  match "line2"
  match "line3"
  text PASTE_OK
rule
  text PASTE_BAD
EOF
    mock_start "$WORK/$CASE.mm" "$CAPDIR" || return
    write_config "$CFG" "$MM_PORT"
    clip_own 'line1
line2
line3'
    term_launch "$EMU" "V6PASTE$$" "$CAP" "" "" || { mock_stop; clip_drop; return; }
    "$XDRIVE" key ctrl+shift+v sleep 1500
    shot "$EMU-3-paste"
    "$XDRIVE" key Return sleep 4000
    "$XDRIVE" slow 25 "/exit" key Return sleep 1200
    term_close; mock_stop; clip_drop

    # Assert on what the MODEL received, not on what the terminal rendered.
    # The render is a second-hand witness -- a killed session can lose buffered
    # output and report a working paste as a broken one, which is exactly how
    # this check first went wrong. The captured request is the ground truth.
    _req="$CAPDIR/req.1"
    if [ ! -f "$_req" ]; then
        bad "$EMU/3 the paste never reached the model at all (no request sent)"
    elif grep -q "line1" "$_req" && grep -q "line2" "$_req" && grep -q "line3" "$_req"; then
        ok "$EMU/3 a real three-line clipboard paste reached the model intact"
    else
        bad "$EMU/3 the paste was truncated (the request is missing lines)"
    fi
    if plain "$CAP" | grep -q "PASTE_OK"; then
        ok "$EMU/3b the block submitted as ONE logical line (M156)"
    elif [ ! -s "$CAP" ]; then
        skip "$EMU/3b render capture empty -- cannot judge the submitted shape"
    else
        bad "$EMU/3b the block did not submit as one logical line"
    fi
}

check_resize() {
    new_case resize
    cat > "$WORK/$CASE.mm" <<'EOF'
wire openai
rule
  delay 4000
  text RESIZE_SURVIVED
EOF
    mock_start "$WORK/$CASE.mm" "$CAPDIR" || return
    write_config "$CFG" "$MM_PORT"
    term_launch "$EMU" "V6SIZE$$" "$CAP" "" "" || { mock_stop; return; }
    "$XDRIVE" slow 25 "hello" key Return sleep 1500
    "$XDRIVE" resize "$WID" 640 500 sleep 2500          # mid-turn
    "$XDRIVE" sleep 4000
    "$XDRIVE" resize "$WID" 1100 620 sleep 2000         # back at the prompt
    shot "$EMU-4-resize"
    "$XDRIVE" slow 25 "/exit" key Return sleep 1500
    term_close; mock_stop

    if plain "$CAP" | grep -q "RESIZE_SURVIVED"; then
        ok "$EMU/4 the turn survived a mid-turn resize and one at the prompt"
    else
        bad "$EMU/4 the session did not survive being resized"
    fi
}

check_nocolor() {
    new_case nocolor
    cat > "$WORK/$CASE.mm" <<'EOF'
wire openai
rule
  count 1
  delay 2500
  tool list_files {"path":"."}
rule
  text ASCII_OK
EOF
    mock_start "$WORK/$CASE.mm" "$CAPDIR" || return
    write_config "$CFG" "$MM_PORT"
    term_launch "$EMU" "V6ASCII$$" "$CAP" "NO_COLOR=1 LC_ALL=C LANG=C" "" \
        || { mock_stop; return; }
    "$XDRIVE" slow 25 "hello" key Return sleep 8000
    shot "$EMU-5-nocolor"
    "$XDRIVE" slow 25 "/exit" key Return sleep 1500
    term_close; mock_stop

    # The glyphs the TUI must drop when the locale is not UTF-8.
    if LC_ALL=C grep -q -e '▸' -e '✓' -e '✗' -e '›' "$CAP" 2>/dev/null; then
        bad "$EMU/5 UTF-8 glyphs rendered under LC_ALL=C (ASCII fallback failed)"
    else
        ok "$EMU/5 ASCII fallbacks used under NO_COLOR=1 + LC_ALL=C"
    fi

    # M257's contract is TWO-SIDED, and testing only one side gets it wrong.
    # ctx.indicator = !accessible && (color || (type_ahead && is_tty)):
    #   with --type-ahead under NO_COLOR the working line MUST exist (the fix,
    #   or the typing is invisible and cannot be corrected);
    #   without the flag it MUST NOT (deliberately narrow, so the fix cannot
    #   surprise anyone who did not ask for the feature).
    # Asserting only the first would report a regression on a session that is
    # behaving exactly as designed -- which is how this check first failed.
    if plain "$CAP" | grep -qi "working"; then
        bad "$EMU/5b working line WITHOUT --type-ahead under NO_COLOR (M257 narrowness lost)"
    else
        ok "$EMU/5b no working line without --type-ahead under NO_COLOR (M257 narrowness)"
    fi

    new_case nocolor_ta
    cat > "$WORK/$CASE.mm" <<'EOF'
wire openai
rule
  count 1
  delay 4000
  tool list_files {"path":"."}
rule
  text ASCII_OK
EOF
    mock_start "$WORK/$CASE.mm" "$CAPDIR" || return
    write_config "$CFG" "$MM_PORT"
    term_launch "$EMU" "V6ASCIITA$$" "$CAP" "NO_COLOR=1 LC_ALL=C LANG=C" "--type-ahead" \
        || { mock_stop; return; }
    "$XDRIVE" slow 25 "hello" key Return sleep 2000
    "$XDRIVE" slow 40 "typed blind?" sleep 800
    shot "$EMU-5-nocolor-typeahead"
    "$XDRIVE" key Return sleep 6000
    "$XDRIVE" slow 25 "/exit" key Return sleep 1500
    term_close; mock_stop

    if tr '\r' '\n' < "$CAP" | grep -qi "working"; then
        ok "$EMU/5c the working line renders under NO_COLOR with --type-ahead (M257)"
    else
        bad "$EMU/5c no working line under NO_COLOR with --type-ahead -- typing is blind"
    fi
    if tr '\r' '\n' < "$CAP" | grep -qi "working.*typed blind?"; then
        ok "$EMU/5d the typed text is visible on that working line"
    else
        bad "$EMU/5d the typed text was captured but never shown"
    fi
}

check_signals() {
    new_case signals
    cat > "$WORK/$CASE.mm" <<'EOF'
wire openai
rule
  delay 12000
  text SHOULD_NOT_ARRIVE
EOF
    mock_start "$WORK/$CASE.mm" "$CAPDIR" || return
    write_config "$CFG" "$MM_PORT"
    term_launch "$EMU" "V6SIG$$" "$CAP" "" "" || { mock_stop; return; }
    "$XDRIVE" slow 25 "hello" key Return sleep 2500
    "$XDRIVE" key ctrl+c sleep 2500                 # interrupt mid-turn
    shot "$EMU-6-interrupt"
    # Back at an empty prompt: two Ctrl-C, then Ctrl-D, must end the session.
    "$XDRIVE" key ctrl+c sleep 700 key ctrl+c sleep 700 key ctrl+d sleep 2500
    term_close; mock_stop

    if plain "$CAP" | grep -q "SHOULD_NOT_ARRIVE"; then
        bad "$EMU/6 Ctrl-C did not interrupt the in-flight turn"
    else
        ok "$EMU/6 Ctrl-C interrupted the turn (the answer never landed)"
    fi
    if plain "$CAP" | tail -3 | grep -q "Script done" || [ ! -d /proc/${TERM_PID:-0} ]; then
        ok "$EMU/6b Ctrl-C twice then Ctrl-D ended the session"
    else
        bad "$EMU/6b the session did not exit on Ctrl-C/Ctrl-D"
    fi
}

# ===================================================================== driver

for EMU in $EMUS; do
    say "emulator: $EMU"
    echo "--- $EMU" >> "$OUT/results.txt"
    check_help
    check_typeahead
    check_paste
    check_resize
    check_nocolor
    check_signals
    echo
done

{
    echo
    echo "totals: $N_OK ok, $N_FAIL failed, $N_SKIP skipped"
    echo
    echo "Scope of this runner, so the gaps are never implied to be covered:"
    echo "  - the Linux VIRTUAL CONSOLE is a different instrument entirely (no"
    echo "    X, no clipboard, the kernel's own emulator): scripts/"
    echo "    tier-v-console.sh covers it -- 11/11 as of M274."
    echo "  - other distro/emulator combinations are the SAME script run inside"
    echo "    a guest; no VirtualBox is needed on a KVM host (M274 corrected"
    echo "    the row's original assumption). Whichever emulators the guest has"
    echo "    are the ones tested -- see the emulator list at the top."
} | tee -a "$OUT/results.txt"

say "totals: $N_OK ok, $N_FAIL failed, $N_SKIP skipped -- $OUT/results.txt"
[ "$KEEP" -eq 1 ] && echo "   captures kept in $WORK"
[ "$N_FAIL" -eq 0 ]
