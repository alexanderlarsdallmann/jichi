#!/bin/sh
# smoke: lifecycle hooks (M25). PreToolUse on write_file exits 2 -> the
# write is BLOCKED (the file must not appear) yet the turn completes (the
# model receives the block as a tool error and finishes); the Stop hook
# writes a sentinel that must exist after the run.
# (Port of tests/e2e/hooks.py, M211.)
. "$(dirname "$0")/_smoke.sh"

t_plan 15
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
outdir=$(smoke_tmp)
target="$ws/should_not_exist.txt"
sentinel="$outdir/stop_ran"

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool write_file {"path":"should_not_exist.txt","content":"data"}
rule
  text HOOK_DONE
EOF

mm_start "$tmp/replies.mm" "$tmp"

cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x"}],
 "snapshots":false,"repoMap":false,"maxRetries":0,
 "hooksEnabled":true,
 "hooks":{
   "PreToolUse":[{"matcher":"write_file","commands":[{"shell":"exit 2"}]}],
   "Stop":[{"commands":[{"shell":"echo ran > '$sentinel'"}]}]}}
EOF

out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto -p "write the file" < /dev/null); rc=$?
mm_stop

if [ ! -e "$target" ]; then
    t_ok "PreToolUse exit 2 blocked the write"
else
    t_fail "the blocked write_file still wrote the file"
fi
case "$out" in
    *HOOK_DONE*) t_ok "the turn completed after the blocked tool" ;;
    *) t_fail "turn incomplete (rc=$rc): $(printf '%s' "$out" | head_bytes 120)" ;;
esac
if [ -f "$sentinel" ]; then
    t_ok "the Stop hook fired"
else
    t_fail "no Stop-hook sentinel"
fi

# --- M584: a hook that DID NOT RUN must be distinguishable from one that ran ---
# THE DEFECT. A `shell:` hook whose script is missing runs `sh -c`, which starts
# fine and exits 127. jichi's interpret() sent every nonzero code that was not a
# block into "advisory only": one WARN line, no telemetry. `-q` -- which every
# headless and autonomous run uses -- suppressed even the warning. Found on a real
# project whose config named `.jichi/hooks/zig-fmt-check.sh` after the directory
# had been removed: every write fired a dead formatter gate for the project's
# entire recorded history, and the only trace was 15 telemetry events that no
# command displayed.
#
# The three CONTROLS below are the point of the arm: a clean exit, a deliberate
# exit-2 block, and a nonzero exit carrying the JSON contract must each stay
# SILENT. This is a failure log, not a trace -- and the JSON case is the one that
# cannot be judged from the exit code, because that path is chosen by the OUTPUT.
h_probe() {   # $1 = hook shell command; echoes the outcome, or "" for no event
    hp=$(smoke_tmp)
    cat > "$hp/replies.mm" <<'MMEOF'
wire openai
rule
  count 1
  tool write_file {"path":"probe.txt","content":"x"}
rule
  text PROBE_DONE
MMEOF
    mm_start "$hp/replies.mm" "$hp"
    cat > "$hp/config.json" <<CFGEOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x"}],
 "snapshots":false,"repoMap":false,"maxRetries":0,"hooksEnabled":true,
 "hooks":{"PostToolUse":[{"matcher":"write_file",
   "commands":[{"shell":"$1","timeout":5}]}]}}
CFGEOF
    mkdir -p "$hp/ws"
    (cd "$hp/ws" && with_deadline 60 "$BIN" --config "$hp/config.json" -q \
        --no-session --auto --log "$hp/t.jsonl" --log-level metrics \
        -p "write it" < /dev/null) > "$hp/out" 2>&1
    mm_stop
    # h_probe is always called inside $(...), i.e. a SUBSHELL: a variable
    # assigned here cannot reach the caller. The log path goes through a file.
    printf '%s\n' "$hp/t.jsonl" > "$tmp/last_log"
    grep '"event":"hook"' "$hp/t.jsonl" 2>/dev/null \
        | sed -n 's/.*"outcome":"\([a-z_]*\)".*/\1/p' | sed -n 1p
}

miss=$(h_probe "/nonexistent/definitely-not-here.sh")
if [ "$miss" = "not_runnable" ]; then
    t_ok "a hook whose command does not exist is recorded as not_runnable"
else
    t_fail "a missing hook command recorded '${miss:-nothing at all}'. Exit 126/127
   is not advice: it means THE CHECK DID NOT RUN, and a project with a hook
   configured believes it is guarded. This is the case that actually happens in
   the field -- a moved or deleted script -- and it reached no sink at all."
fi

nz=$(h_probe "exit 3")
if [ "$nz" = "nonzero_exit" ]; then
    t_ok "a hook that ran and exited nonzero is recorded as nonzero_exit"
else
    t_fail "a hook exiting 3 recorded '${nz:-nothing}'; want nonzero_exit --
   distinct from not_runnable, because this hook DID run."
fi

ok0=$(h_probe "true")
if [ -z "$ok0" ]; then
    t_ok "control: a clean hook writes no event (a failure log, not a trace)"
else
    t_fail "a hook exiting 0 emitted '$ok0'. A healthy hook is not worth a line
   per tool call -- that was the M326v decision and it still holds."
fi

blk=$(h_probe "exit 2")
if [ -z "$blk" ]; then
    t_ok "control: a deliberate exit-2 block writes no event"
else
    t_fail "an exit-2 block emitted '$blk'. Blocking is the documented contract,
   not a failure."
fi

js=$(h_probe "echo '{\"decision\":\"block\",\"reason\":\"no\"}'; exit 3")
if [ -z "$js" ]; then
    t_ok "control: a nonzero exit carrying the JSON contract writes no event"
else
    t_fail "a JSON-contract hook exiting 3 emitted '$js'. It said what it meant;
   only the exit code looks like a failure, and the JSON path is chosen by the
   OUTPUT -- which is why interpret() reports whether the hook spoke
   deliberately instead of the caller guessing from the code."
fi

# --- 9: and the event REACHES A READER (seams D6) -----------------------------
# Emitting is half. Eight telemetry event types were written on every run and
# displayed by no command -- which is why `hook` sat unread from M326v until now,
# and why one project's dead formatter gate stayed invisible through 15 recorded
# failures. This check walks the whole path: provoke the failure, then read the
# log back with the shipped reader and require it to SAY so.
miss=$(h_probe "/nonexistent/definitely-not-here.sh")
rep=$("$BIN" telemetry "$(cat "$tmp/last_log")" 2>&1)
case "$rep" in
    *"NOT RUNNABLE"*)
        t_ok "the shipped telemetry reader reports the hook that did not run" ;;
    *)
        t_fail "\`jichi telemetry\` did not report the not_runnable hook, so the
   event is on disk and still invisible -- exactly the state M584 exists to end.
   Reader output was:
$(printf '%s' "$rep" | head_bytes 400)" ;;
esac

# --- M1113-b: duplicate keys in a model's tool args must not bypass a hook ---
# THE DEFECT (SV:2259). jichi's json parser keeps BOTH members of a duplicate
# key; a PreToolUse hook gets its event via jc_hooks.c build_stdin, which
# re-prints the PARSED tool args under "tool_input" -- a re-serialization that
# keeps every duplicate member. So the hook and jichi's own write each resolve
# the SAME key from the SAME re-printed object, but pre-M1113 they resolved it
# DIFFERENTLY: the hook (any real parser: python/Go/jq) takes LAST-WINS, while
# jichi's cJSON_GetObjectItemCaseSensitive returned the FIRST member. A
# duplicate-keyed write_file args {"path":"BLOCKME.txt",...,"path":"ok.txt"}
# let the hook judge ok.txt (allowed) while jichi wrote BLOCKME.txt (the first)
# -- a fence bypass: the guard looked at one path and jichi wrote another.
# M1113's last-wins accessor makes hook stdin and jichi's own tool reads
# resolve the SAME (last) member, so the guard and the write agree.
#
# The hook below is the WITNESS: a POSIX-sh guard that reads the event JSON on
# stdin, extracts the LAST "path" member it sees (last-wins, like any real
# parser -- the smoke tier is python3-free, so sed takes the last match), logs
# the path it judged, and exits 2 (block) only if THAT path contains BLOCKME.
# Each leg asserts what the hook SAW (its log line), not just the write -- the
# leg is only meaningful if the hook's verdict and the written file agree.
mmk_leg() {  # mmk_leg <label> <tool-json> -> sets MMK_WS, MMK_LOG
    # One fresh workspace + mock turn. The hook is a POSIX-sh guard that logs
    # the LAST "path" it sees (last-wins, like a real parser) to MMK_LOG and
    # exits 2 iff THAT path holds BLOCKME. The log path is baked into the
    # shell command so it reaches the hook without exporting.
    mlh=$(smoke_tmp)
    MMK_LOG="$mlh/saw.log"
    rm -f "$MMK_LOG"
    mlhook="$mlh/guard.sh"
    cat > "$mlhook" <<HOOK
#!/bin/sh
p=\$(sed -n 's/.*"path"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\\1/p' | sed -n '\$p')
printf '%s\\n' "\$p" >> "$MMK_LOG"
case "\$p" in
    *BLOCKME*) exit 2 ;;
    *) exit 0 ;;
esac
HOOK
    MMK_WS=$(smoke_tmp); mkdir -p "$MMK_WS"
    mla=$(smoke_tmp); mkdir -p "$mla/cap"
    cat > "$mla/r.mm" <<MMEOF
wire openai
rule
  count 1
  tool write_file $2
rule
  count 2
  text MMK$1DONE
MMEOF
    mm_start_unbounded "$mla/r.mm" "$mla/cap"
    cat > "$mla/cfg.json" <<CFGEOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0,"hooksEnabled":true,
 "hooks":{"PreToolUse":[{"matcher":"write_file","commands":[{"shell":"sh '$mlhook'","timeout":10}]}]}}
CFGEOF
    ( cd "$MMK_WS" && with_deadline 60 "$BIN" --config "$mla/cfg.json" -q \
        --no-session --auto -p "write it" < /dev/null ) > "$mla/out" 2>&1
    mm_stop
    MMK_OUT="$mla/out"
    MMK_CAP="$mla/cap"
}

# Leg A (the bypass): {"path":"BLOCKME.txt",...,"path":"ok.txt"} -- the hook sees
# ok.txt (LAST) and must ALLOW. POST-M1113 the write resolves ok.txt (last), so
# ok.txt is written and BLOCKME.txt is NOT: the guard's verdict and the file
# agree. Pre-M1113 the write took BLOCKME.txt (first) -- the bypass.
mmk_leg A '{"path":"BLOCKME.txt","content":"SVX","path":"ok.txt"}'
mmk_a_saw=$(cat "$MMK_LOG" 2>/dev/null)
if [ "$mmk_a_saw" = "ok.txt" ]; then
    t_ok "10 leg A: the hook saw ok.txt (the LAST path) and allowed"
else
    t_fail "10 leg A: hook saw '${mmk_a_saw:-<none>}' not ok.txt (the last-wins member)"
fi
{ [ -f "$MMK_WS/ok.txt" ] && [ ! -e "$MMK_WS/BLOCKME.txt" ]; } \
    && t_ok "11 leg A: write agreed with the hook (ok.txt written, BLOCKME.txt absent) -- no bypass" \
    || t_fail "11 leg A: write disagrees with the hook (ok=$([ -f "$MMK_WS/ok.txt" ] && echo y || echo n) blockme=$([ -e "$MMK_WS/BLOCKME.txt" ] && echo y || echo n)) -- a duplicate-keyed arg bypassed the fence"

# Leg B (reverse order): {"path":"ok.txt",...,"path":"BLOCKME.txt"} -- the hook
# sees BLOCKME.txt (LAST) and blocks (exit 2). Nothing may be written, and the
# block must reach the model as the tool message.
mmk_leg B '{"path":"ok.txt","content":"SVX","path":"BLOCKME.txt"}'
# The block reached the model as the tool message: it is the content of the
# round-2 request the mockmodel captures (cap/req.*), not jichi's stdout.
mmk_b_saw=$(cat "$MMK_LOG" 2>/dev/null)
mmk_b_blocked=$(grep -h "PreToolUse hook" "$MMK_CAP"/req.* 2>/dev/null | grep -c . || true)
if [ "$mmk_b_saw" = "BLOCKME.txt" ]; then
    t_ok "12 leg B: the hook saw BLOCKME.txt (the LAST path) and blocked"
else
    t_fail "12 leg B: hook saw '${mmk_b_saw:-<none>}' not BLOCKME.txt (the last-wins member)"
fi
{ [ ! -e "$MMK_WS/ok.txt" ] && [ ! -e "$MMK_WS/BLOCKME.txt" ] \
    && [ "${mmk_b_blocked:-0}" -ge 1 ]; } \
    && t_ok "13 leg B: nothing written and the block reached the model" \
    || t_fail "13 leg B: files ok=$([ -e "$MMK_WS/ok.txt" ] && echo y || echo n) blockme=$([ -e "$MMK_WS/BLOCKME.txt" ] && echo y || echo n) blocked_msg=$mmk_b_blocked"

# Leg C (control): a single {"path":"BLOCKME.txt"} -- blocked, nothing written.
mmk_leg C '{"path":"BLOCKME.txt","content":"SVX"}'
mmk_c_saw=$(cat "$MMK_LOG" 2>/dev/null)
mmk_c_blocked=$(grep -h "PreToolUse hook" "$MMK_CAP"/req.* 2>/dev/null | grep -c . || true)
if [ "$mmk_c_saw" = "BLOCKME.txt" ]; then
    t_ok "14 leg C: control -- the hook saw BLOCKME.txt and blocked"
else
    t_fail "14 leg C: control -- hook saw '${mmk_c_saw:-<none>}' not BLOCKME.txt"
fi
{ [ ! -e "$MMK_WS/BLOCKME.txt" ] && [ "${mmk_c_blocked:-0}" -ge 1 ]; } \
    && t_ok "15 leg C: control -- nothing written, block reached the model" \
    || t_fail "15 leg C: control -- blockme=$([ -e "$MMK_WS/BLOCKME.txt" ] && echo y || echo n) blocked_msg=$mmk_c_blocked"

t_done
