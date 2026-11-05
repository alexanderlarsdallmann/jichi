#!/bin/sh
# smoke: a saved session's mode may narrow the process's mode, never widen it
# (M845, D5).
#
# THE DEFECT. ACP's session/load applied the mode a session was saved with,
# unconditionally: a `jichi --acp --plan` process that loaded a session saved in
# auto ran in auto, so the model wrote without a permission request, and every
# session it created afterwards was saved in auto too (measured with a real model,
# DEFERRED def-285). The TUI's and the headless resume kept an explicit --plan or
# --auto (mode_pinned), but without a flag they widened chat to auto in the same
# way. One rule now, the one `jichi control mode` already follows (M304): a saved
# mode may narrow the process's mode, never widen it, and an explicit --plan or
# --auto keeps the process's mode either way -- with a note, never silently.
#
# HOW THIS SEES IT. A session saved in auto is seeded before each case. The mock
# model's write_file is the probe: it writes only where the process really runs
# in auto. Checks 4 and 7 are the controls -- the same call, in a process started
# with --auto, does write -- so the refusals cannot pass on a binary that writes
# nothing. The universe of places a saved mode is applied (ACP's load, the TUI's
# two resumes, the headless resume) is one function, jc_perm_saved_mode, whose
# 18 combinations tests/test_perm.c checks; this driver drives the two
# front-ends a smoke run can reach without a terminal.
. "$(dirname "$0")/_smoke.sh"

t_plan 11
smoke_home
tmp=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
SID="5aed5aed-0d5e-4a11-9e45-000000000845"
sdir="$HOME/.jichi.d/sessions"

seed() {   # a session saved in auto, fresh for every case
    mkdir -p "$sdir"
    cat > "$sdir/$SID.json" <<EOF
{"sessionId":"$SID","title":"saved in auto","workspaceDirectory":".","mode":"auto",
 "history":[{"role":"user","content":"hello"},{"role":"assistant","content":"hi"}]}
EOF
}

mk_config() {   # mk_config <port>
    cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$1/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"toolProfile":"full",
 "maxRetries":0}
EOF
}

# wait for a line in the growing $1 whose .id == $2 and has result/error,
# within $3 seconds; print it on success. (From acp_cancel.sh.)
acp_wait_id() {
    _end=$(( $(date +%s) + $3 ))
    while [ "$(date +%s)" -le "$_end" ]; do
        while IFS= read -r _l; do
            [ "$(printf '%s' "$_l" | "$JQ" .id 2>/dev/null)" = "$2" ] || continue
            if printf '%s' "$_l" | "$JQ" -q .result 2>/dev/null \
               || printf '%s' "$_l" | "$JQ" -q .error 2>/dev/null; then
                printf '%s\n' "$_l"
                return 0
            fi
        done < "$1"
        sleep 1
    done
    return 1
}

# acp_case <mode-flag>: load the saved session, prompt (write w1.txt), then
# session/new and prompt again (write w2.txt). Leaves $CASE_WS and $CASE_ERR.
acp_case() {
    seed
    CASE_WS=$(smoke_tmp)
    _d=$(smoke_tmp)
    cat > "$_d/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"w1.txt","content":"x"}
rule
  count 3
  tool write_file {"path":"w2.txt","content":"x"}
rule
  text done
EOF
    mm_start "$_d/replies.mm" "$_d/cap"
    mk_config "$MM_PORT"
    mkfifo "$_d/in.fifo"
    : > "$_d/out.jsonl"
    CASE_ERR="$_d/err"
    (cd "$CASE_WS" && exec "$BIN" --config "$tmp/config.json" --acp "$1" \
        < "$_d/in.fifo" > "$_d/out.jsonl" 2> "$CASE_ERR") &
    _pid=$!
    exec 3<>"$_d/in.fifo"
    # _acp_stop -- tear the case down (fd 3, the ACP process, the mock). The
    # driver's waits would otherwise keep feeding a dead fifo, and the mock
    # would ride out its 120 s deadline.
    _acp_stop() {
        exec 3>&- 2>/dev/null
        kill "$_pid" 2>/dev/null
        wait "$_pid" 2>/dev/null
        mm_stop
    }
    printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":1,"clientCapabilities":{}}}' >&3
    if ! acp_wait_id "$_d/out.jsonl" 1 15 > /dev/null; then
        t_fail "the ACP process died before initialize completed:
 $(tail -c 200 "$CASE_ERR")"
        _acp_stop
        return
    fi
    printf '%s\n' '{"jsonrpc":"2.0","id":2,"method":"session/load","params":{"sessionId":"'"$SID"'","cwd":"'"$CASE_WS"'","mcpServers":[]}}' >&3
    if ! acp_wait_id "$_d/out.jsonl" 2 15 > /dev/null; then
        t_fail "the ACP process died before the session load completed:
 $(tail -c 200 "$CASE_ERR")"
        _acp_stop
        return
    fi
    printf '%s\n' '{"jsonrpc":"2.0","id":3,"method":"session/prompt","params":{"sessionId":"'"$SID"'","prompt":[{"type":"text","text":"write w1"}]}}' >&3
    _w3=$(acp_wait_id "$_d/out.jsonl" 3 30)
    printf '%s\n' '{"jsonrpc":"2.0","id":4,"method":"session/new","params":{"cwd":"'"$CASE_WS"'","mcpServers":[]}}' >&3
    _sid2=$(acp_wait_id "$_d/out.jsonl" 4 15 | "$JQ" .result.sessionId 2>/dev/null)
    printf '%s\n' '{"jsonrpc":"2.0","id":5,"method":"session/prompt","params":{"sessionId":"'"$_sid2"'","prompt":[{"type":"text","text":"write w2"}]}}' >&3
    _w5=$(acp_wait_id "$_d/out.jsonl" 5 30)
    # A prompt that never got its result means the ACP process DIED mid-turn.
    # On the unfixed M954 build this is exactly what happened (the session
    # load left relay_id as stack poison; the first save after a prompt ran
    # strlen on it and the process segfaulted), but the waits here just timed
    # out and the file-based checks below still passed -- a dead process
    # writes nothing, so "w1.txt absent" was true for the wrong reason. The
    # two round-trips completing IS the liveness check those seven cannot see.
    if [ -n "$_w3" ]; then
        t_ok "ACP prompt 1 (the loaded session) got its result"
    else
        t_fail "ACP prompt 1 never completed -- the process died mid-turn:
 $(tail -c 200 "$CASE_ERR")"
    fi
    if [ -n "$_w5" ]; then
        t_ok "ACP prompt 2 (the new session) got its result"
    else
        t_fail "ACP prompt 2 never completed -- the process died mid-turn:
 $(tail -c 200 "$CASE_ERR")"
    fi
    _acp_stop
}

# headless_case <extra-flag-or-empty>: resume the saved session headless; the
# mock asks for write_file h.txt. Leaves $CASE_WS and $CASE_ERR.
headless_case() {
    seed
    CASE_WS=$(smoke_tmp)
    _d=$(smoke_tmp)
    cat > "$_d/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"h.txt","content":"x"}
rule
  text done
EOF
    mm_start "$_d/replies.mm" "$_d/cap"
    mk_config "$MM_PORT"
    CASE_ERR="$_d/err"
    # shellcheck disable=SC2086 -- $1 is one flag or nothing
    (cd "$CASE_WS" && with_deadline 60 "$BIN" --config "$tmp/config.json" $1 \
        -p "write h" --session "$SID" < /dev/null) > "$_d/out" 2> "$CASE_ERR"
    mm_stop
}

# --- 1-3: ACP, a --plan process loads a session saved in auto -------------------
acp_case --plan
if [ -f "$CASE_WS/w1.txt" ]; then
    t_fail "a --plan process that loaded a session saved in auto WROTE w1.txt: the
 saved mode widened the process to auto (D5)"
else
    t_ok "--acp --plan, after loading a session saved in auto: write_file refused"
fi
if [ -f "$CASE_WS/w2.txt" ]; then
    t_fail "a session/new after that load WROTE w2.txt: the widened mode spread to
 the next session"
else
    t_ok "a session/new after the load stays in plan: write_file refused"
fi
if grep -q 'never widens' "$CASE_ERR"; then
    t_ok "the load says on stderr that the saved mode was not applied"
else
    t_fail "no note on stderr that the saved mode was kept out -- silent:
 $(tail -c 200 "$CASE_ERR")"
fi

# --- 4: the control -- the same load under --auto writes --------------------------
acp_case --auto
if [ -f "$CASE_WS/w1.txt" ]; then
    t_ok "control: --acp --auto loading the same session writes w1.txt"
else
    t_fail "control failed: --acp --auto did NOT write, so checks 1-2 may pass
 because nothing writes at all: $(tail -c 200 "$CASE_ERR")"
fi

# --- 5-7: the headless resume ------------------------------------------------------
headless_case ""
if [ -f "$CASE_WS/h.txt" ]; then
    t_fail "a headless resume with no mode flag WROTE h.txt: the session's saved
 auto widened the run (chat by default)"
else
    t_ok "a headless resume with no flag stays in chat: write_file not run"
fi
if grep -q 'never widens' "$CASE_ERR"; then
    t_ok "the headless resume says the saved mode was not applied"
else
    t_fail "no note on stderr from the headless resume: $(tail -c 200 "$CASE_ERR")"
fi
headless_case --auto
if [ -f "$CASE_WS/h.txt" ]; then
    t_ok "control: a headless resume with --auto writes h.txt"
else
    t_fail "control failed: with --auto the resume did NOT write, so check 5 may
 pass because nothing writes: $(tail -c 200 "$CASE_ERR")"
fi

t_done
