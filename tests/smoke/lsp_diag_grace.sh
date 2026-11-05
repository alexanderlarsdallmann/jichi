#!/bin/sh
# smoke: a file whose diagnostics arrive AFTER the first (empty) batch is read
# as dirty, not clean (M938, the 2026-10-08 review R09-F1).
#
# THE GAP. `collect_diag_params` (src/lsp/jc_lsp.c) timed its grace window on
# `jc_now_seconds()` -- a WHOLE-SECOND clock (`(double)time(NULL)`, the
# review's h_lsp5.c measurement). A 0.4 s grace therefore expired at the NEXT
# second boundary: worst case the wait was 1.0 s, best case 0.1 s. A server
# that publishes an empty set at didOpen and the real set ~0.6 s later
# (clangd, measured; re-indexing after a save) was then read as "latest =
# empty set" -> count 0 -> `tu_append_diagnostics` appends NOTHING, so the
# model sees a clean edit of a dirty file. Two fixes, both proven here:
# (1) the wait is measured on the monotonic millisecond clock, grace grown
# 0.4 -> 2.0 s, so the real set (1.4 s, or the review's 0.6 s) lands inside
# it; (2) the hardcoded didOpen version 1 is now an incrementing
# per-connection counter, because a version-aware server does NOT re-publish
# for a same-version re-open, and jichi would then wait out the whole
# LSP_DIAG_TIMEOUT and read the timeout as a second stale state.
#
# THE MOCK. `tests/tools/mocklsp` (a C89 stdio LSP; byte-framed JSON-RPC,
# unlike MCP's newline framing, which is why the sh mock in _smoke.sh could
# not do this) plays a re-indexing server: EMPTY set immediately on every
# didOpen, the REAL set ~1.4 s later (a thread on its OWN monotonic clock;
# 0.6 s with --fast). The staging lives in the mock so the product under test
# does the waiting. Modes: default (staged re-index), --version (version-aware
# too: the real set arrives inline on a NEWER didOpen version, nothing on a
# same-version one), --clean (the control: never re-publishes), --record FILE
# (appends "OPEN <version> <uri>" per didOpen so check 4 reads the exact
# versions jichi sent).
#
# DETERMINISM. The mock stages on its own monotonic clock; the driver holds no
# sub-second sleep. The staged real set (1.4 s) sits BETWEEN the old worst
# case (1.0 s) and the new grace (2.0 s), so the clock fix is the only
# product change that turns checks 1 and 2 green. Fences on, caps off: the
# reproduction is a measurement; the mock's --deadline is a fence, not a cap.
#
# CHECKS (each is red without its fix -- the design's teeth table):
#   1  a dirty file: the edit's tool result CARRIES the real diagnostic --
#      the false-clean read it as a clean edit (no "Diagnostics (1):" block)
#   2  a SECOND edit on the same connection carries it too (connection reuse;
#      the wait stays honest after the first real set)
#   3  a CLEAN file still reads as a clean edit (no diagnostic block appears:
#      the grace did not make a clean file dirty, and did not wait out the
#      15 s timeout -- the run finishes promptly)
#   4  under a version-aware server, the versions jichi sends are 1, 2, 3
#      (the counter's own teeth, from the mock's --record file) and the file
#      still reads dirty (same-version re-opens earn no re-sync, so without
#      the counter this waits out the timeout)
#   5  the review's own 0.6 s reproduction (--fast): the real set is inside
#      the grace, so the fix covers the reported case, not only the 1.4 s
#      probe
. "$(dirname "$0")/_smoke.sh"

G=/usr/bin/grep
[ -x "$G" ] || G=grep
t_plan 5
smoke_home
tmp=$(smoke_tmp)

# The tool result lands in the NEXT captured request (the one whose history
# gained the role:tool message). The edit's own arguments (with the NEW
# string) are in the PREVIOUS request, so matching a request for
# "tool message present AND next-edit string absent" isolates one specific
# edit's result. Returns 0 and prints the content if found.
tool_res_for() {  # $1 = capture dir, $2 = new_string of the edit (present in
                  # the result's PRECEDING request, absent in its own)
    for f in "$1"/req.*; do
        [ -f "$f" ] || continue
        $G -q "\"new_string\":\"$2\"" "$f" && continue
        r=$($G -o '"role":"tool"[^}]*"content":"[^"]*Edited notes\.txt[^"]*"' "$f" | head -n 1)
        if [ -n "$r" ]; then
            printf '%s\n' "$r"
            return 0
        fi
    done
    return 1
}

# one <capdir> <n:1|2> <extra-mocklsp-args json fragment>
case_run() {
    _cap="$1"; _n="$2"; _x="$3"
    mkdir -p "$_cap"
    if [ "$_n" = 1 ]; then
        cat > "$_cap/r.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"notes.txt"}
rule
  count 2
  tool edit_file {"path":"notes.txt","old_string":"v1","new_string":"v2"}
rule
  count 3
  text done
EOF
    else
        cat > "$_cap/r.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"notes.txt"}
rule
  count 2
  tool edit_file {"path":"notes.txt","old_string":"v1","new_string":"v2"}
rule
  count 3
  tool read_file {"path":"notes.txt"}
rule
  count 4
  tool edit_file {"path":"notes.txt","old_string":"v2","new_string":"v3"}
rule
  count 5
  text done
EOF
    fi
    mm_start_unbounded "$_cap/r.mm" "$_cap/cap"
    cat > "$_cap/cfg.json" <<EOF
{"toolProfile":"full","lowResource":false,
"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"maxRetries":0,
"lspServers":[{"name":"mocklsp","command":"$SMOKE_TOOLS/mocklsp",
"args":["--deadline","30"$_x],"extensions":["txt"]}]}
EOF
    _ws=$(smoke_tmp)
    printf 'x v1 y\n' > "$_ws/notes.txt"
    ( cd "$_ws" && with_deadline 120 "$BIN" --config "$_cap/cfg.json" --no-session \
        --auto --output json -p "fix notes.txt" \
        < /dev/null > "$_cap/out" 2> "$_cap/err" )
    mm_stop
}

# --- case A: the staged re-index (checks 1 + 2) ------------------------------
CAP=$(smoke_tmp)
case_run "$CAP" 2 ""
t1=$(tool_res_for "$CAP/cap" "v2")
if [ -n "$t1" ] &&
   printf '%s' "$t1" | $G -q 'Diagnostics (1):' &&
   printf '%s' "$t1" | $G -q 'MOCKLSP intentional error' ; then
    t_ok "a dirty file's edit result carries the REAL diagnostic (not the false-clean 'clean edit')"
elif [ -z "$t1" ]; then
    t_fail "no edit tool result found in the capture -- the server never answered: $(tail -c 200 "$CAP/err")"
else
    t_fail "first edit read a dirty file as clean: res=$(head_bytes 200 "$t1")"
fi

t2=$(tool_res_for "$CAP/cap" "v3")
if [ -n "$t2" ] &&
   printf '%s' "$t2" | $G -q 'Diagnostics (1):' &&
   printf '%s' "$t2" | $G -q 'MOCKLSP intentional error' ; then
    t_ok "the second edit (same connection) carries it too -- the wait stays honest after the first real set"
else
    t_fail "second edit lost the diagnostic: res=[$(head_bytes 200 "${t2:-none}")]"
fi

# --- case B: the clean control (check 3) -------------------------------------
CAPB=$(smoke_tmp)
case_run "$CAPB" 1 ',"--clean"'
tb=$(tool_res_for "$CAPB/cap" "v2")
if [ -n "$tb" ] &&
   ! printf '%s' "$tb" | $G -Eq '(Diagnostics|intentional error|no diagnostics received)' ; then
    t_ok "a clean file still reads as a clean edit after the fix (the grace made nothing dirty, nothing timed out)"
else
    t_fail "clean control misread: res=[$(head_bytes 200 "${tb:-none}")]"
fi

# --- case C: version-aware server, the counter's teeth (check 4) -------------
CAPC=$(smoke_tmp)
case_run "$CAPC" 2 ',"--version","--record","'"$CAPC"'/rec"'
tc_res=$(tool_res_for "$CAPC/cap" "v2")
vers=$(tr '|' '\n' < "$CAPC/rec" 2>/dev/null | sed -n 's/OPEN \([0-9][0-9]*\) .*/\1/p' | tr '\n' ' ')
if [ "$vers" = "1 2 " ] &&
   printf '%s' "$tc_res" | $G -q 'MOCKLSP intentional error' ; then
    t_ok "didOpen versions increment 1, 2 across the connection, and the file still reads dirty (same-version re-opens earn no re-sync)"
else
    t_fail "version sequence wrong (want 1 2): got [$vers] res=[$(head_bytes 120 "${tc_res:-none}")]"
fi

# --- case D: the review's own 0.6 s reproduction (--fast) (check 5) ----------
CAPD=$(smoke_tmp)
case_run "$CAPD" 1 ',"--fast"'
td=$(tool_res_for "$CAPD/cap" "v2")
if [ -n "$td" ] &&
   printf '%s' "$td" | $G -q 'Diagnostics (1):' &&
   printf '%s' "$td" | $G -q 'MOCKLSP intentional error' ; then
    t_ok "the review's 0.6 s reproduction: the real set is inside the grace (read dirty)"
else
    t_fail "0.6 s real set missed: res=[$(head_bytes 200 "${td:-none}")]"
fi

t_done
