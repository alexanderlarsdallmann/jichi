#!/bin/sh
# smoke: an armed autonomy envelope does not switch an ACP session to auto
# (M850, the branch review's ACP-2 -- the ACP half of CFG-1).
#
# THE DEFECT. main.c promoted an armed envelope to AUTO "in headless mode", and
# headless was `print_prompt || !isatty(stdin)`. ACP's stdin is always a pipe, so
# an editor session counted as headless: `--edit-scope`, `--max-tool-calls`, a
# config `verify`, or a project `.jichi/config.json` with `editScope` armed the
# envelope and pinned AUTO -- zero permission requests, while the editor showed
# the session asking as usual. A cloned repository could carry that config.
#
# THE GAP THIS ALSO CLOSES. No test in any tier asserted that ACP asks at all:
# `tests/e2e/acp_terminal.py` answers a permission request only "defensively"
# under --auto. So this driver plays the editor: it waits for the server's
# `session/request_permission`, answers it, and reads the disk.
#
#   1  baseline: a write in an ACP chat session sends session/request_permission
#   2  baseline: a REJECTED write does not land
#   3  --edit-scope (arms the envelope) still asks -- ACP-2
#   4  --max-tool-calls (arms it too) still asks
#   5  a project .jichi/config.json with editScope (no --config) still asks
#   6  armed, a rejected write does not land
#   7  armed, an approved write lands -- the gate is real, not a refusal of all
#   8  control: an explicit --auto with an armed envelope asks nothing and writes
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
JQ="$SMOKE_TOOLS/jsonq"

cat > "$HOME/mm.script" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"a.txt","content":"written"}
rule
  text done
EOF

# acp_case <answer:reject_once|allow_once> <use --config:yes|no> <flags...>
#   -> ASKED (1 if the server sent session/request_permission), WROTE (yes|no),
#      ERR (the server's stderr)
acp_case() {
    _answer=$1; _usecfg=$2; shift 2
    _t=$(smoke_tmp)
    _ws=$(smoke_tmp)
    _fifo="$_t/in.fifo"; _out="$_t/out.jsonl"; ERR="$_t/err"
    mkfifo "$_fifo"; : > "$_out"
    mm_start "$HOME/mm.script" "$_t/cap"
    _cfgjson="{\"lowResource\":false,\"models\":[{\"name\":\"m\",\"provider\":\"openai\",\"model\":\"mock\",
 \"apiBase\":\"http://127.0.0.1:$MM_PORT/v1\",\"apiKey\":\"x\",\"roles\":[\"chat\"]}],
 \"snapshots\":false,\"repoMap\":false,\"references\":false,\"toolProfile\":\"full\",
 \"maxRetries\":0}"
    if [ "$_usecfg" = yes ]; then
        printf '%s\n' "$_cfgjson" > "$_t/config.json"
        set -- --config "$_t/config.json" "$@"
    else
        # the implicit merge: the user's ~/.jichi, plus the project's own file
        printf '%s\n' "$_cfgjson" > "$HOME/.jichi"
        mkdir -p "$_ws/.jichi"
        printf '{"editScope":["*.txt"]}\n' > "$_ws/.jichi/config.json"
    fi
    # smoke_home exports JC_CONFIG (an empty config, M376), and JC_CONFIG turns
    # the implicit merge OFF -- so the project-config case must drop it, or the
    # project file is never read and "no permission request" proves nothing.
    if [ "$_usecfg" = yes ]; then
        (cd "$_ws" && exec "$BIN" "$@" --acp < "$_fifo" > "$_out" 2> "$ERR") &
    else
        (cd "$_ws" && unset JC_CONFIG && exec "$BIN" "$@" --acp \
            < "$_fifo" > "$_out" 2> "$ERR") &
    fi
    _pid=$!
    exec 3<>"$_fifo"
    printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":1,"clientCapabilities":{}}}' >&3
    printf '%s\n' '{"jsonrpc":"2.0","id":2,"method":"session/new","params":{"cwd":"'"$_ws"'","mcpServers":[]}}' >&3
    _end=$(( $(date +%s) + 15 )); _sid=""
    while [ -z "$_sid" ] && [ "$(date +%s)" -le "$_end" ]; do
        _sid=$(while IFS= read -r _l; do
                   [ "$(printf '%s' "$_l" | "$JQ" .id 2>/dev/null)" = 2 ] &&
                   printf '%s' "$_l" | "$JQ" .result.sessionId 2>/dev/null && break
               done < "$_out")
        [ -n "$_sid" ] || sleep 1
    done
    printf '%s\n' '{"jsonrpc":"2.0","id":3,"method":"session/prompt","params":{"sessionId":"'"$_sid"'","prompt":[{"type":"text","text":"write it"}]}}' >&3
    ASKED=0; _done=0
    _end=$(( $(date +%s) + 30 ))
    while [ "$_done" = 0 ] && [ "$(date +%s)" -le "$_end" ]; do
        while IFS= read -r _l; do
            _m=$(printf '%s' "$_l" | "$JQ" .method 2>/dev/null)
            _i=$(printf '%s' "$_l" | "$JQ" .id 2>/dev/null)
            if [ "$_m" = "session/request_permission" ] && [ "$ASKED" = 0 ]; then
                ASKED=1
                printf '%s\n' '{"jsonrpc":"2.0","id":'"$_i"',"result":{"outcome":{"outcome":"selected","optionId":"'"$_answer"'"}}}' >&3
            elif [ "$_i" = 3 ] && [ -z "$_m" ]; then
                _done=1
            fi
        done < "$_out"
        [ "$_done" = 1 ] || sleep 1
    done
    exec 3>&-
    kill "$_pid" 2>/dev/null; wait "$_pid" 2>/dev/null
    mm_stop
    WROTE=$([ -f "$_ws/a.txt" ] && echo yes || echo no)
    # Vacuity guard: did the session reach the model at all? Without a call
    # there is no tool call, so "no permission request" would mean nothing.
    CALLED=$([ -f "$_t/cap/req.1" ] && echo yes || echo no)
    rm -f "$HOME/.jichi"
}

# --- 1-2: the baseline: ACP asks, and a rejection holds ------------------------
acp_case reject_once yes
if [ "$ASKED" = 1 ]; then
    t_ok "baseline: a write in an ACP chat session asks the client"
else
    t_fail "baseline: no session/request_permission for a write in chat mode -- the
 gate under test is not there at all: $(tail -c 200 "$ERR")"
fi
if [ "$WROTE" = no ]; then
    t_ok "baseline: a rejected write does not land"
else
    t_fail "baseline: the client rejected the write and it landed anyway"
fi

# --- 3-5: an armed envelope still asks ------------------------------------------
acp_case reject_once yes --edit-scope '*.txt'
A3=$ASKED; W3=$WROTE
if [ "$A3" = 1 ]; then
    t_ok "--edit-scope arms the envelope, and the ACP session still asks"
else
    t_fail "--edit-scope switched the ACP session to auto: no permission request
 (stderr: $(grep -o '\[envelope\][^\"]*' "$ERR" | head -1))"
fi
acp_case reject_once yes --max-tool-calls 5
if [ "$ASKED" = 1 ]; then
    t_ok "--max-tool-calls arms the envelope, and the ACP session still asks"
else
    t_fail "--max-tool-calls switched the ACP session to auto: no permission request"
fi
acp_case reject_once no
if [ "$ASKED" = 1 ]; then
    t_ok "a project config's editScope does not switch the ACP session to auto"
elif [ "$CALLED" = no ]; then
    t_fail "the project-config case never reached the model -- the implicit merge
 did not load, so this check measured nothing: $(tail -c 200 "$ERR")"
else
    t_fail "a project .jichi/config.json with editScope switched the editor's
 session to auto: the model was called and no permission request came"
fi

# --- 6-7: armed, the answer decides ---------------------------------------------
if [ "$A3" = 1 ] && [ "$W3" = no ]; then
    t_ok "armed, a rejected write does not land"
else
    t_fail "armed, the write landed without an approval (asked=$A3 wrote=$W3)"
fi
acp_case allow_once yes --edit-scope '*.txt'
if [ "$ASKED" = 1 ] && [ "$WROTE" = yes ]; then
    t_ok "armed, an approved write lands -- the gate asks, it does not refuse all"
else
    t_fail "armed, an approved write did not land (asked=$ASKED wrote=$WROTE)"
fi

# --- 8: control: an explicit --auto is the user's choice -------------------------
acp_case reject_once yes --auto --edit-scope '*.txt'
if [ "$ASKED" = 0 ] && [ "$WROTE" = yes ]; then
    t_ok "control: an explicit --auto asks nothing and writes"
else
    t_fail "control failed: --acp --auto (asked=$ASKED wrote=$WROTE)"
fi

t_done
