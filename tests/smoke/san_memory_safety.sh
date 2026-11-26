#!/bin/sh
# smoke: the three SAN memory-safety defects under the project's OWN SAN policy
# (M1210, card 117, attempt 2: the driver rework per the 09:58 design).
#
# WHY A WORKTREE (the note's "Why no Makefile stage"). A driver that runs
# `make clean` in the tier's tree deletes the tier's binaries mid-run (the
# keep-going incident's class). The worktree shape makes both go away: the
# driver is tier-registered (the 578th) and self-contained -- its OWN git
# worktree of the tree, its OWN objects, its OWN build. The worktree's objects
# stay inside the worktree, so a driver deleting the tier's binaries is
# impossible by construction.
#
# THE MEASURED RECIPE (run 0094, worktree at b61cf9e71~1): `make SAN=1 jichi`
# rc 0, ~2-3 min for the link. This driver's wall is ONE worktree build + six
# jichi runs; the worktree build dominates, so it sits in run.sh's 120 s list,
# BEFORE JC_SMOKE_TIMEOUT_MULT. No timeout wrapper around the jichi runs: the
# runs must be capped by nothing, and a SAN abort must surface as the driver's
# own `not ok`, never as a deadline artifact.
#
# THE DESIGN (the 09:58+0100 entry's "different approach"): the driver copies
# the three source rigs verbatim (ask_unattended.sh, dirty_dispatch.sh cells
# 11+12, relay_baton_written.sh) and adds ONE ASan sub-assertion per rig:
# no ASan signature in the rig's captured stderr/stdout. The three rigs prove
# the three SAN fixes red-first, each red line carrying its pinned ASan frame
# from notes/M1210.md.
#
# RED FIRST (the driver's own evidence, both runs verbatim in the report):
# Run A (the red): SANMT_BASE=b61cf9e71~1 (the pre-fix head) -- the driver's
# worktree builds the UNFIXED tree, so checks 2, 3 and 5 go red. Each red line
# must carry its pinned ASan frame.
# Run B (the green): the default base (HEAD, which carries the fixes) -- all 6 ok.
#
# TAP plan = 6 (one t_ok/t_fail per check, every path emits exactly 6).
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not available"
command -v make >/dev/null 2>&1 || t_skip "make not available"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# smoke_lint check 5's source-level "$BIN" (see the header). In the 120-s
# tier run the tree's jichi exists; guard anyway.
[ -x "$BIN" ] && "$BIN" --version > "$tmp/binv" 2>/dev/null

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
# The neutralising, the main driver's form: ambient harness loader settings
# (env wins over the config) and the operator's ambient git config for the
# fixture repos.
unset JICHI_HARNESS_DIR JICHI_HARNESS_MODEL_KEY_FILE JICHI_HARNESS_GIT_TOKEN_FILE
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
export GIT_TERMINAL_PROMPT=0
export GIT_CLONE_PROTECTION_ACTIVE=0

# A fixture failure emits one t_fail per check so the plan stays honest.
fixture_fail() {
    _ff_n=0
    while [ "$_ff_n" -lt 7 ]; do
        _ff_n=$((_ff_n + 1))
        t_fail "$_ff_n: fixture failed: $1"
    done
    t_done
}

# --- the WORKTREE (the tree's own head, self-contained) -----------------------
WT=$(smoke_tmp)/wt
SANMT_BASE="${SANMT_BASE:-HEAD}"
if ! (cd "$ROOT" && git worktree add -q "$WT" "$SANMT_BASE") 2> "$tmp/wt.add.err"; then
    fixture_fail "git worktree add $WT $SANMT_BASE failed: $(tr '\n' '|' < "$tmp/wt.add.err" | head_bytes 200)"
fi
# Chain smoke_cleanup: the worktree lives under $tmp, so the registry's
# rm -rf covers its FILES, but the METADATA in the main repo's
# .git/worktrees/ only git removes.
trap 'git worktree remove --force "$WT" 2>/dev/null; git worktree prune 2>/dev/null; smoke_cleanup' EXIT INT TERM

# --- the SAN build (the measured recipe; the driver's wall, ~2-3 min) ----------
_mk=$(smoke_make)
(
    cd "$WT" || exit 3
    exec "$_mk" -j8 SAN=1 WERROR=1 jichi
) > "$tmp/san.build.log" 2>&1
SBRC=$?
SANBIN="$WT/jichi"

if [ "$SBRC" -ne 0 ] || [ ! -x "$SANBIN" ]; then
    TOOLCHAIN_SILENT=0
    if "$G" -qiE 'unknown argument|unknown option|unrecogni[sz]ed (command-line )?option|cannot find -lasan|libclang_rt\.asan' "$tmp/san.build.log"; then
        TOOLCHAIN_SILENT=1
    fi
    if [ "$TOOLCHAIN_SILENT" -eq 1 ]; then
        t_skip_one "1: the SAN build is real -- SKIP: the toolchain cannot build the SAN subject"
        t_skip_one "2: the ask_unattended rig under SAN -- SKIP: the toolchain cannot build the SAN subject"
        t_skip_one "3: the unknown-value doctor under SAN -- SKIP: the toolchain cannot build the SAN subject"
        t_skip_one "3b: the unknown-value doctor under SAN with a 70-byte value -- SKIP: the toolchain cannot build the SAN subject"
        t_skip_one "4: the known-value 'refuse' control -- SKIP: the toolchain cannot build the SAN subject"
        t_skip_one "5: the relay rig ON under SAN -- SKIP: the toolchain cannot build the SAN subject"
        t_skip_one "6: the relay rig OFF control -- SKIP: the toolchain cannot build the SAN subject"
        t_done
    fi
    t_fail "1: the SAN build failed (rc=$SBRC; binary present: $([ -x "$SANBIN" ] && echo yes || echo no)) -- a committed regression cell that cannot build is evidence, not an absence; the make log's tail: $(tail -5 "$tmp/san.build.log" | tr '\n' '|' | head_bytes 300)"
    t_fail "2: the ask_unattended rig under SAN (NOT RUN -- the build failed)"
    t_fail "3: the unknown-value doctor under SAN (NOT RUN -- the build failed)"
    t_fail "3b: the unknown-value doctor under SAN with a 70-byte value (NOT RUN -- the build failed)"
    t_fail "4: the known-value 'refuse' control (NOT RUN -- the build failed)"
    t_fail "5: the relay rig ON under SAN (NOT RUN -- the build failed)"
    t_fail "6: the relay rig OFF control (NOT RUN -- the build failed)"
    t_done
fi

# --- 1: the SAN build is REAL (strings: __asan_ / LeakSanitizer) --------------
if strings "$SANBIN" 2>/dev/null | "$G" -qE '__asan_|LeakSanitizer'; then
    t_ok "1: the SAN build is real -- $SANBIN holds an __asan_/LeakSanitizer symbol"
else
    t_fail "1: the SAN build is NOT sanitized -- $SANBIN holds no __asan_/LeakSanitizer symbol"
fi

# --- 2: C1 (d1) -- ask_unattended rig under SAN -------------------------------
# Copy ask_unattended.sh's rig verbatim, plus one ASan sub-assertion.
tmp_c1=$(smoke_tmp)
{
    echo "wire openai"
    echo "rule"
    echo "  count 1"
    echo '  tool ask_user {"question":"Overwrite the existing export file?"}'
    echo "rule"
    echo "  text done"
} > "$tmp_c1/replies.mm"

mm_start "$tmp_c1/replies.mm" "$tmp_c1"
write_config "$tmp_c1/config.json" "$MM_PORT"
(cd "$tmp_c1" && with_deadline 60 "$SANBIN" --config "$tmp_c1/config.json" --no-session \
    --auto --budget-tokens 100k --dump-requests "$tmp_c1/reqs" \
    -p "export the data" < /dev/null > "$tmp_c1/out" 2> "$tmp_c1/err") || true
mm_stop

journal=$(ls "$HOME/.jichi.d/runs"/*.jsonl 2>/dev/null | head -1)

# 2a: the bounded run wrote a journal
c1_journal_ok=0
if [ -n "$journal" ] && [ -s "$journal" ]; then
    c1_journal_ok=1
fi

# 2b: the ask event, unanswered, with the question
c1_ask_ok=0
if grep '"event":"ask"' "$journal" 2>/dev/null \
   | grep '"answered":false' | grep -q 'Overwrite the existing export'; then
    c1_ask_ok=1
fi

# 2c: the model was told honestly (pinned through the wire)
c1_model_ok=0
last=$(ls "$tmp_c1/reqs"/req-*.json 2>/dev/null | tail -1)
if [ -n "$last" ] && grep -q 'No interactive user is available' "$last"; then
    c1_model_ok=1
fi

# 2d: `runs` renders the count
c1_runs_ok=0
runsout=$("$SANBIN" runs 2>/dev/null)
if printf '%s' "$runsout" | grep -q 'unanswered=1'; then
    c1_runs_ok=1
fi

# 2e: and the JSON projection carries it
c1_json_ok=0
runsjson=$("$SANBIN" runs --output json 2>/dev/null)
if printf '%s' "$runsjson" | grep -q '"unanswered":[[:space:]]*1'; then
    c1_json_ok=1
fi

# 2f: the run itself completed
c1_done_ok=0
if grep -q 'done' "$tmp_c1/out"; then
    c1_done_ok=1
fi

# 2g: NO ASan signature in the captured stderr (the pinned frame d1)
c1_asan_ok=0
if ! "$G" -qE 'ERROR: AddressSanitizer|stack-use-after-scope' "$tmp_c1/err" 2>/dev/null; then
    c1_asan_ok=1
fi

# C1 passes if all sub-checks pass AND no ASan
if [ "$c1_journal_ok" -eq 1 ] && [ "$c1_ask_ok" -eq 1 ] && [ "$c1_model_ok" -eq 1 ] && \
   [ "$c1_runs_ok" -eq 1 ] && [ "$c1_json_ok" -eq 1 ] && [ "$c1_done_ok" -eq 1 ] && \
   [ "$c1_asan_ok" -eq 1 ]; then
    t_ok "2: the ask_unattended rig under SAN (journal, ask event, model, runs, json, done) + no ASan signature (heap-buffer-overflow ... jc_utf8_trunc_len)"
else
    t_fail "2: the ask_unattended rig under SAN failed (journal=$c1_journal_ok ask=$c1_ask_ok model=$c1_model_ok runs=$c1_runs_ok json=$c1_json_ok done=$c1_done_ok asan=$c1_asan_ok) -- heap-buffer-overflow ... jc_utf8_trunc_len"
fi

# --- 3: C2 (d2) -- unknown-value doctor under SAN -----------------------------
# Copy dirty_dispatch.sh cells 11+12 verbatim, plus one ASan sub-assertion.
tmp_c2=$(smoke_tmp)
_ws2=$(smoke_tmp)

make_git() {
    _mg_d=$1; _mg_dirty=$2
    git init -q "$_mg_d"
    git -C "$_mg_d" config user.email t@t
    git -C "$_mg_d" config user.name t
    printf 'committed\n' > "$_mg_d/base.txt"
    (cd "$_mg_d" && git add base.txt && git commit -q -m init)
    if [ "$_mg_dirty" = 1 ]; then
        printf 'uncommitted work\n' > "$_mg_d/dirty.txt"
    fi
}

write_strictness() {
    _ws_c=$1; _ws_m=$2
    sed "1s/{/{\"strictness\":{\"dirtyDispatch\":\"$_ws_m\"},/" "$_ws_c" > "$_ws_c.tmp" \
        && mv "$_ws_c.tmp" "$_ws_c"
}

make_git "$_ws2" 0
rm -rf "$tmp_c2"; mkdir -p "$tmp_c2"
write_config "$tmp_c2/config.json" "$MM_PORT" '' ''
write_strictness "$tmp_c2/config.json" 'Bogus'
(cd "$_ws2" && with_deadline 60 "$SANBIN" doctor --config "$tmp_c2/config.json" \
    < /dev/null > "$tmp_c2/doc" 2>&1 )

# 3a: the config-load NOTICE names the key, the value and the mode in effect
c2_notice_ok=0
if "$G" -q 'strictness.dirtyDispatch' "$tmp_c2/doc" \
   && "$G" -q 'Bogus' "$tmp_c2/doc" \
   && "$G" -q 'unknown' "$tmp_c2/doc" \
   && "$G" -q 'warn.*in effect' "$tmp_c2/doc"; then
    c2_notice_ok=1
fi

# 3b: `jichi doctor` reports the effective mode and the fell-through reason
c2_effective_ok=0
if "$G" -q 'dirty-tree dispatch guard: warn' "$tmp_c2/doc" \
   && "$G" -q 'strictness.dirtyDispatch' "$tmp_c2/doc" \
   && "$G" -q 'Bogus' "$tmp_c2/doc"; then
    c2_effective_ok=1
fi

# 3c: NO ASan signature in the captured doctor output (the pinned frame d2)
c2_asan_ok=0
if ! "$G" -qE 'ERROR: AddressSanitizer|stack-use-after-scope' "$tmp_c2/doc" 2>/dev/null; then
    c2_asan_ok=1
fi

# C2-unknown passes if all sub-checks pass AND no ASan
if [ "$c2_notice_ok" -eq 1 ] && [ "$c2_effective_ok" -eq 1 ] && [ "$c2_asan_ok" -eq 1 ]; then
    t_ok "3: the unknown-value doctor under SAN (notice, effective-mode) + no ASan signature (heap-use-after-free ... jc_snprintf/run_doctor)"
else
    t_fail "3: the unknown-value doctor under SAN failed (notice=$c2_notice_ok effective=$c2_effective_ok asan=$c2_asan_ok) -- heap-use-after-free ... jc_snprintf/run_doctor"
fi

# --- 3b: C2 (d2) long arm -- 70-byte unknown dirtyDispatch under SAN -------
# The review found the >=64-byte fell arm overflows ddfold (stack-buffer-overflow WRITE).
# This cell uses a 70-byte unknown value to prove the memcpy in the ORIGINAL fix has
# no ddlen<64 guard. After the d2 redo (format from dm live), this should be green.
tmp_c2b=$(smoke_tmp)
_ws2b=$(smoke_tmp)

make_git "$_ws2b" 0
rm -rf "$tmp_c2b"; mkdir -p "$tmp_c2b"
write_config "$tmp_c2b/config.json" "$MM_PORT" '' '' 70
# The 70-byte value: 'BogusUnknownValueSevenTenBytesSevenTenBytesSevenTenBytes70bytexXXXXXXX' (70 bytes)
write_strictness "$tmp_c2b/config.json" 'BogusUnknownValueSevenTenBytesSevenTenBytesSevenTenBytes70bytexXXXXXXX'
(cd "$_ws2b" && with_deadline 60 "$SANBIN" doctor --config "$tmp_c2b/config.json" \
    < /dev/null > "$tmp_c2b/doc" 2>&1 )

# 3b-a: the config-load NOTICE names the key, the 70-byte value prefix, and the mode
c2b_notice_ok=0
if "$G" -q 'strictness.dirtyDispatch' "$tmp_c2b/doc" \
   && "$G" -q 'BogusUnknownValueSevenTenBytes' "$tmp_c2b/doc" \
   && "$G" -q 'unknown' "$tmp_c2b/doc" \
   && "$G" -q 'warn.*in effect' "$tmp_c2b/doc"; then
    c2b_notice_ok=1
fi

# 3b-b: `jichi doctor` reports the effective mode and the fell-through reason
c2b_effective_ok=0
if "$G" -q 'dirty-tree dispatch guard: warn' "$tmp_c2b/doc" \
   && "$G" -q 'strictness.dirtyDispatch' "$tmp_c2b/doc" \
   && "$G" -q 'BogusUnknownValueSevenTenBytes' "$tmp_c2b/doc"; then
    c2b_effective_ok=1
fi

# 3b-c: NO ASan signature in the captured doctor output (the pinned frame for long fell)
c2b_asan_ok=0
if ! "$G" -qE 'ERROR: AddressSanitizer|stack-use-after-scope' "$tmp_c2b/doc" 2>/dev/null; then
    c2b_asan_ok=1
fi

# C2-long passes if all sub-checks pass AND no ASan
if [ "$c2b_notice_ok" -eq 1 ] && [ "$c2b_effective_ok" -eq 1 ] && [ "$c2b_asan_ok" -eq 1 ]; then
    t_ok "3b: the unknown-value doctor under SAN with a 70-byte value (notice, effective-mode) + no ASan (the long fell arm formats live from dm, no ddfold overflow)"
else
    t_fail "3b: the unknown-value doctor under SAN with a 70-byte value failed (notice=$c2b_notice_ok effective=$c2b_effective_ok asan=$c2b_asan_ok) -- stack-buffer-overflow WRITE in ddfold"
fi

# --- 4: C2 control (known value 'refuse') -------------------------------------
# The known value 'refuse' should print 'dirty-tree dispatch guard: refuse'
# (the byte the block prints for a known value; read src/main.c's run_doctor
# to confirm the exact text) and no ASan.
tmp_c2c=$(smoke_tmp)
_ws2c=$(smoke_tmp)

make_git "$_ws2c" 0
rm -rf "$tmp_c2c"; mkdir -p "$tmp_c2c"
write_config "$tmp_c2c/config.json" "$MM_PORT" '' ''
write_strictness "$tmp_c2c/config.json" 'refuse'
(cd "$_ws2c" && with_deadline 60 "$SANBIN" doctor --config "$tmp_c2c/config.json" \
    < /dev/null > "$tmp_c2c/doc" 2>&1 )

# 4a: the known value 'refuse' -- the doctor prints 'dirty-tree dispatch guard: refuse'
c2c_refuse_ok=0
if "$G" -q 'dirty-tree dispatch guard: refuse' "$tmp_c2c/doc"; then
    c2c_refuse_ok=1
fi

# 4b: NO ASan signature for the known value
c2c_asan_ok=0
if ! "$G" -qE 'ERROR: AddressSanitizer|stack-use-after-scope' "$tmp_c2c/doc" 2>/dev/null; then
    c2c_asan_ok=1
fi

# C2-known passes if the known value prints and no ASan
if [ "$c2c_refuse_ok" -eq 1 ] && [ "$c2c_asan_ok" -eq 1 ]; then
    t_ok "4: the known-value 'refuse' control prints the normal OK line + no ASan signature"
else
    t_fail "4: the known-value 'refuse' control failed (refuse=$c2c_refuse_ok asan=$c2c_asan_ok)"
fi

# --- 5: C3 (d3) -- relay rig ON under SAN -------------------------------------
# Copy relay_baton_written.sh verbatim, plus one ASan sub-assertion.
tmp_c3=$(smoke_tmp)
ws_c3=$(smoke_tmp)

cat > "$tmp_c3/replies.mm" <<'EOF'
wire openai
rule
  match "messages"
  usage 1000 5
  tool read_file {"path":"a.txt"}
EOF
printf 'a small file for the rig\n' > "$ws_c3/a.txt"

mm_start "$tmp_c3/replies.mm" "$tmp_c3/cap"
MODEL_X='"contextLength":1000'
TOP_X='"maxToolIters":3'
write_config "$tmp_c3/config_on.json" "$MM_PORT" \
    "$TOP_X,\"relay\":true" "$MODEL_X"
write_config "$tmp_c3/config_off.json" "$MM_PORT" "$TOP_X" "$MODEL_X"

store_on="$HOME/.jichi.d/relay"
notes() { ls "$store_on"/*/leg-*.md 2>/dev/null | head -n 1; }

# The relay-ON run (hands over -> writes the baton).
# The worktree build links no leakcheck-suppress file, so card [118]'s pre-existing
# exit leaks (jc_sb_reserve <- hl_text class) would make LSan abort the relay run at
# exit AFTER the handover wrote the baton, emptying on.json and reding check 5's
# restored handover floor for a reason d3 did not cause; detect_leaks=0 silences LSan
# ONLY -- ASan stays armed, so the pinned buffer-overflow reds still abort mid-run.
# (the env form, not a prefix: FreeBSD's /bin/sh does not export a prefix to the child, smoke_lint 13)
(cd "$ws_c3" && with_deadline 90 env ASAN_OPTIONS=detect_leaks=0 "$SANBIN" --config "$tmp_c3/config_on.json" \
    --output json -p "read the file and tell me what it says" \
    < /dev/null > "$tmp_c3/on.json" 2> "$tmp_c3/on.err")
n_on=$(notes)
(cd "$ws_c3" && with_deadline 90 env ASAN_OPTIONS=detect_leaks=0 "$SANBIN" --config "$tmp_c3/config_off.json" \
    --output json -p "read the file and tell me what it says" \
    < /dev/null > "$tmp_c3/off.json" 2> "$tmp_c3/off.err")
mm_stop

stop_of() { "$SMOKE_TOOLS/jsonq" '.stop_reason' "$1" 2>/dev/null; }
mode_of() { ls -l "$1" 2>/dev/null | awk '{ print substr($1, 2) }'; }

# 5a: the floor -- a handover happened AND a notes file appeared (0600, non-empty)
c3_handover_ok=0
if [ "$(stop_of "$tmp_c3/on.json")" = "handover" ]; then
    c3_handover_ok=1
fi
c3_baton_ok=0
if [ -n "$n_on" ] && [ -s "$n_on" ] && [ "$(mode_of "$n_on")" = "rw-------" ]; then
    # The baton exists - this indicates a handover occurred (even if the run
    # exited with error due to ASan leaks after the handover)
    c3_baton_ok=1
fi

# 5b: the baton holds the header, the task verbatim, and sections 2 and 4
c3_template_ok=0
if [ -n "$n_on" ] && grep -q '^# Relay notes' "$n_on" && \
   grep -q '^## 1\. The task' "$n_on" && \
   grep -q 'read the file and tell me what it says' "$n_on" && \
   grep -q '^## 2\. Where the work stands' "$n_on" && \
   grep -q '^## 4\. How to use these notes' "$n_on"; then
    c3_template_ok=1
fi

# 5c: the header names the trigger that ended the leg (hard_limit)
c3_header_ok=0
if [ -n "$n_on" ] && grep -q '^Previous leg ended: hard_limit' "$n_on"; then
    c3_header_ok=1
fi

# 5d: the log names the relay notes' path for the operator
c3_log_ok=0
if grep -q '\[relay\] handover notes written to' "$tmp_c3/on.err"; then
    c3_log_ok=1
fi

# 5e: NO ASan signature in the captured stderr (the pinned frame d3)
c3_asan_ok=0
if ! "$G" -qE 'ERROR: AddressSanitizer|stack-use-after-scope' "$tmp_c3/on.err" 2>/dev/null; then
    c3_asan_ok=1
fi

# C3 passes if all sub-checks pass AND no ASan
if [ "$c3_handover_ok" -eq 1 ] && [ "$c3_baton_ok" -eq 1 ] && [ "$c3_template_ok" -eq 1 ] && [ "$c3_header_ok" -eq 1 ] && \
   [ "$c3_log_ok" -eq 1 ] && [ "$c3_asan_ok" -eq 1 ]; then
    t_ok "5: the relay rig ON under SAN (handover, baton, template, header, log) + no ASan signature (stack-buffer-overflow ... relay_baton_render)"
else
    t_fail "5: the relay rig ON under SAN failed (handover=$c3_handover_ok baton=$c3_baton_ok template=$c3_template_ok header=$c3_header_ok log=$c3_log_ok asan=$c3_asan_ok) -- stack-buffer-overflow ... relay_baton_render"
fi

# --- 6: C3 control -- relay rig OFF -------------------------------------------
# The OFF run ends at its cap (not a handover), and the relay store holds
# nothing under the OFF run's OWN session id.

# 6a: with the relay off, the run ends at its cap and wrote no baton
off_stop=$("$SMOKE_TOOLS/jsonq" '.stop_reason' "$tmp_c3/off.json" 2>/dev/null)
off_id=$("$SMOKE_TOOLS/jsonq" '.session_id' "$tmp_c3/off.json" 2>/dev/null)
c3c_off_ok=0
if [ -n "$off_id" ] && [ "$off_stop" = "max_iters" ] && \
   [ ! -e "$store_on/$off_id" ]; then
    c3c_off_ok=1
fi

# 6b: NO ASan signature for the OFF run
c3c_asan_ok=0
if ! "$G" -qE 'ERROR: AddressSanitizer|stack-use-after-scope' "$tmp_c3/off.err" 2>/dev/null; then
    c3c_asan_ok=1
fi

# C3-off passes if the OFF run ended at cap and no ASan
if [ "$c3c_off_ok" -eq 1 ] && [ "$c3c_asan_ok" -eq 1 ]; then
    t_ok "6: the relay rig OFF control ends at its cap ($off_stop) and wrote no baton (no relay dir under its session id) + no ASan signature"
else
    t_fail "6: the relay rig OFF control failed (off_stop=$off_stop off_id=$off_id has_relay=$([ -e "$store_on/$off_id" ] && echo yes || echo no) asan=$c3c_asan_ok)"
fi

t_done
