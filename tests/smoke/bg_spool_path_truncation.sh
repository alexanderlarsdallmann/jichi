#!/bin/sh
# smoke: background spool PATH TRUNCATION (SV:0540, M1146; supersedes the
# "load" diagnosis of c1b073b5/ab5b9dcc) and its REFUSAL half (SV:0738,
# M1146-b). A four-step mock turn like bg_spool_cap, but with a HOME deep
# enough that the spool file path
#     $HOME/.jichi.d/bgspool/bg-<id>-<pid>.out
# overflows the spool buffer once pids are 7 digits.
#
# Leg A (M1146, checks 1-5): an 111-byte HOME makes the path 145 bytes, past
# the OLD 128-byte buffer. Before the fix, jc_bg_start built the name with an
# UNCHECKED jc_snprintf, so the path was silently cut at 127 and the spool
# file got a truncated name the probe's bg-*.out glob never sees (max size
# 0). This leg proves the spool is FOUND and capped under the long path: RED
# on the unfixed code, GREEN after.
#
# Leg B (SV:0738, checks 6-9): a 490-byte HOME makes the path 524 bytes --
# past the RESIZED 512-byte spool buffer, yet short enough that spool_dir
# (507 bytes) still fits the 512-byte spool_dir[] and is recorded by
# jc_bg_mgr_init, so the REFUSAL fires in jc_bg_start (the M1146
# truncation-check on p->spool), not in init's empty-dir path. There jichi
# must REFUSE: the tool result the model reads carries the M1146 refusal
# ("spool path too long for the 512-byte buffer: ..."), NO spool file appears
# under $HOME/.jichi.d/bgspool, and the background command never runs (no
# sentinel). SV:0738: the depth is built from MULTIPLE directory components
# each < 255 bytes (NAME_MAX); a single ~480-char component makes mkdir fail
# with 'File name too long'. The precondition (check 6) fails loudly if any
# half of that is wrong, so the leg cannot go green for the wrong reason.
#
# NOTE on the assertion token: the M1146 refusal message embeds the full
# ~507-byte spool_dir into the 512-byte s_spool_err, so its "NOT started"
# tail is truncated away before the model reads it (measured; the model sees
# "error: spool path too long for the 512-byte buffer: <path>" cut at 511).
# Leg B therefore asserts the surviving refusal marker, "spool path too
# long", which is unique to the jc_bg_start refusal (distinct from init's
# generic "failed to start" and from a successful "Started background
# process"). That is the same refusal the task asks for, identified by the
# string that actually reaches the model.

. "$(dirname "$0")/_smoke.sh"

t_plan 9

# --- build a deep HOME -------------------------------------------------------
# smoke_home would put HOME directly under $SMOKE_TMPROOT (a `jichi_smoke.XXXXXX`
# name). With a short TMPDIR that leaves the spool file well under 128 bytes, so
# the bug never fires. Instead we nest long pad components under a registered
# temp base and put HOME inside them, so the spool path is guaranteed long.
#
# _target_home N -- build a HOME whose full path is exactly N bytes (N >= 30):
# mktemp a registered temp base, pad to reach N with a chain of components
# each <= 100 chars, then mktemp the 18-char `jichi_smoke.XXXXXX` HOME name in
# the deepest one. Sets + exports HOME and neutralizes the ambient project
# config the way smoke_home does (M376).
#
# SV:0738: the pad is MULTIPLE components of at most 100 chars each (well
# under 255 = NAME_MAX). A single ~480-char component makes mkdir fail with
# 'File name too long', and the leg would die -- or run shallow -- for the
# wrong reason. Each component here is <= 100.
_target_home() {
    _th_n=$1
    _th_base=$(mktemp -d "$SMOKE_TMPROOT/jichi_smoke.XXXXXX") || {
        echo "not ok - mktemp failed" >&2; exit 1
    }
    printf '%s\n' "$_th_base" >> "$SMOKE_TMPREG"
    # HOME = base + <pad path> + 1 + 18. So <pad path> must be _th_n - base - 19.
    # <pad path> is "/c1/c2/..." : total length = sum(len(c_i)) + (#components).
    _th_padlen=$((_th_n - ${#_th_base} - 19))
    [ "$_th_padlen" -lt 0 ] && _th_padlen=0
    _th_deep="$_th_base"
    _th_left=$_th_padlen
    while [ "$_th_left" -gt 0 ]; do
        if [ "$_th_left" -gt 101 ]; then
            _th_c=100
        else
            _th_c=$((_th_left - 1))
        fi
        _th_comp=$(printf 'd%.0s' $(seq 1 "$_th_c"))
        _th_deep="$_th_deep/$_th_comp"
        _th_left=$((_th_left - _th_c - 1))
    done
    mkdir -p "$_th_deep" || {
        echo "not ok - mkdir deep dir failed" >&2; exit 1
    }
    _th_home=$(mktemp -d "$_th_deep/jichi_smoke.XXXXXX") || {
        echo "not ok - mktemp HOME failed" >&2; exit 1
    }
    HOME=$_th_home
    export HOME
    printf '%s' '{}' > "$HOME/smoke-null-config.json"
    JC_CONFIG="$HOME/smoke-null-config.json"
    export JC_CONFIG
}

# _max_comp PATH -- print the length (bytes) of PATH's longest directory
# component. The Leg B precondition uses it to prove every component is
# < 255 (NAME_MAX), so the depth is built from legal names the kernel will
# create -- a single over-long component would have made mkdir fail.
_max_comp() {
    _mc_path=$1; _mc_max=0
    while [ -n "$_mc_path" ]; do
        _mc_head=${_mc_path%%/*}
        case "$_mc_path" in
            */*) _mc_rest=${_mc_path#*/} ;;
            *)   _mc_rest="" ;;
        esac
        [ "${#_mc_head}" -gt "$_mc_max" ] && _mc_max=${#_mc_head}
        _mc_path=$_mc_rest
    done
    printf '%s' "$_mc_max"
}

# --- the four-step mock turn (same writer as bg_spool_cap) --------------------
# _mock_turn WS SENTINEL CAP -- run the start/read/read/answer turn with its
# mockmodel capture dir CAP (a fresh one per leg, so each leg's req.* holds
# that leg's wire traffic only). Prints the agent's stdout to the caller
# (who captures it); the capture stays on disk for the assertions.
_mock_turn() {
    _mt_ws=$1; _mt_sent=$2; _mt_cap=$3
    rm -rf "$_mt_cap"; mkdir -p "$_mt_cap"
    cat > "$_mt_cap/replies.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"echo started > '$_mt_sent'; for i in \$(seq 1 2048); do yes MARK | sed 205q; sleep 0.001; done; sleep 10","run_in_background":true}
rule
  count 2
  delay 800
  tool read_background_output {"id":1}
rule
  count 3
  delay 200
  tool read_background_output {"id":1}
rule
  text BG_DONE
EOF
    mm_start "$_mt_cap/replies.mm" "$_mt_cap"
    write_config "$_mt_cap/config.json" "$MM_PORT"
    ( cd "$_mt_ws" && with_deadline 60 "$BIN" --config "$_mt_cap/config.json" \
          -q --no-session --auto -p "run it in the background" < /dev/null )
    _mt_rc=$?
    mm_stop
    return $_mt_rc
}

# ===========================================================================
# LEG A (M1146): 111-byte HOME -> spool file 145 bytes, past the OLD
# 128-byte buffer. The spool must be FOUND and capped.
# ===========================================================================
_target_home 111
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
sentinel="$ws/bg_ran"

# Precondition (the "re-check the depth" guard): the spool path jichi will build
# MUST be long enough to overflow the 128-byte buffer. spool_dir is
# $HOME/.jichi.d/bgspool; the file appends /bg-<id>-<pid>.out (~16 chars for a
# 7-digit pid). If this is short, the driver would go green for the wrong reason.
spool_dir="$HOME/.jichi.d/bgspool"
sfile_len=$(( ${#spool_dir} + 1 + 16 ))
if [ "$sfile_len" -ge 128 ]; then
    t_ok "A long path precondition: spool file path is ${sfile_len} bytes (>= 128, the old buffer overflows)"
else
    t_fail "A long path precondition NOT met: spool file path is only ${sfile_len} bytes -- HOME too shallow to trigger the truncation, re-check the depth"
fi

# --- start the probe (verbatim shape from bg_spool_cap) ----------------------
# Stats the spool file every 0.2s during the run, records the max size seen.
max_size_file="$ws/max_spool_size"
echo "0" > "$max_size_file"

(
    for i in $(seq 1 30); do
        if [ -d "$spool_dir" ]; then
            spool_file=$(ls -t "$spool_dir"/bg-*.out 2>/dev/null | head -1)
            if [ -n "$spool_file" ] && [ -f "$spool_file" ]; then
                spool_size=$(wc -c < "$spool_file" 2>/dev/null | tr -d ' ' || echo 0)
                current_max=$(cat "$max_size_file" 2>/dev/null || echo 0)
                if [ "$spool_size" -gt "$current_max" ]; then
                    echo "$spool_size" > "$max_size_file"
                fi
            fi
        fi
        sleep 0.2
    done
) &
    probe_pid=$!

t0=$(date +%s)
out=$(_mock_turn "$ws" "$sentinel" "$tmp/cap")
rc=$?
t1=$(date +%s)
printf '%s' "$out" > "$ws/agent.log"

kill $probe_pid 2>/dev/null
wait $probe_pid 2>/dev/null

case "$out" in
    *BG_DONE*) t_ok "A the start/read/read/answer sequence completed" ;;
    *) t_fail "A turn incomplete (rc=$rc): $(printf '%s' "$out" | head_bytes 120)" ;;
esac

if [ $((t1 - t0)) -lt 50 ]; then
    t_ok "A the background child did not hold the agent ($((t1 - t0))s)"
else
    t_fail "A run took $((t1 - t0))s -- the background child was not reaped"
fi

i=0
while [ ! -f "$sentinel" ] && [ $i -lt 5 ]; do
    i=$((i + 1)); sleep 1
done
if [ -f "$sentinel" ]; then
    t_ok "A the background command really ran (sentinel written)"
else
    t_fail "A no sentinel -- the detached command never ran"
fi

# The spool must be FOUND (max > 0: the bg-*.out glob matched a real, full-name
# spool) and CAPPED (<= JC_BG_SPOOL_MAX). Pre-fix the name is cut at 127, so no
# .out file exists and max_seen is 0.
max_seen=$(cat "$max_size_file" 2>/dev/null || echo 0)
if [ "$max_seen" -gt 0 ] && [ "$max_seen" -le 524288 ]; then
    t_ok "A spool found and capped under the long path: max size $max_seen <= JC_BG_SPOOL_MAX (524288)"
else
    t_fail "A spool name truncated / spool file not found under a long path: max size seen $max_seen (the 128-byte buffer cut the name so the probe's bg-*.out glob matched nothing)"
fi

# ===========================================================================
# LEG B (SV:0738, the refusal): 490-byte HOME -> spool file 524 bytes, past
# the RESIZED 512-byte spool buffer (but spool_dir 507 still fits 512, so the
# refusal fires in jc_bg_start). jichi must REFUSE, with the model reading why.
# ===========================================================================
_target_home 490
ws2=$(smoke_tmp)
sentinel2="$ws2/bg_ran"
spool_dir2="$HOME/.jichi.d/bgspool"
sfile_len2=$(( ${#spool_dir2} + 1 + 16 ))
max_comp2=$(_max_comp "$spool_dir2")
# Three things must hold or the leg fires for the wrong reason:
#  (i)   the spool FILE path overflows the 512-byte spool buffer (> 511);
#  (ii)  spool_dir still fits the 512-byte spool_dir[] (< 512), so init
#        records it and the REFUSAL is jc_bg_start's truncation-check (the
#        branch the perturbation disables), not init's empty-dir "no dir"
#        path (a deeper HOME would refuse in init with a different message);
#  (iii) every directory component is < 255 (NAME_MAX) -- the multi-component
#        depth is legal, so mkdir created a real path (SV:0738).
if [ "$sfile_len2" -gt 511 ] && [ "${#spool_dir2}" -lt 512 ] && [ "$max_comp2" -lt 255 ]; then
    t_ok "B refusal precondition: spool file path ${sfile_len2} bytes (> 511, overflows the spool buffer), spool_dir ${#spool_dir2} bytes (< 512, so jc_bg_start -- not init -- refuses), longest component ${max_comp2} (< 255)"
else
    t_fail "B refusal precondition NOT met: spool file ${sfile_len2} bytes (need > 511), spool_dir ${#spool_dir2} bytes (need < 512), longest component ${max_comp2} bytes (need < 255) -- HOME depth wrong, re-check the target"
fi

t20=$(date +%s)
out2=$(_mock_turn "$ws2" "$sentinel2" "$tmp/cap2")
rc2=$?
t21=$(date +%s)
printf '%s' "$out2" > "$ws2/agent.log"

# Guard: the leg actually ran a full turn (so the refusal checks below decide
# on a real refusal, not on a turn that hung/crashed).
case "$out2" in
    *BG_DONE*) t_ok "B the start/refuse/read/answer sequence completed in $((t21 - t20))s" ;;
    *) t_fail "B turn incomplete (rc=$rc2): $(printf '%s' "$out2" | head_bytes 120)" ;;
esac

# Check 8 (the core refusal assertion): the tool result the MODEL reads
# (req.2 of the capture carries run_terminal_command's result) must be the
# M1146 refusal, "spool path too long for the 512-byte buffer: ...". With the
# refusal branch disabled (the perturbation) this becomes "Started background
# process 1" and the check goes red. (The message's "NOT started" tail is
# truncated away by the 512-byte s_spool_err at this depth -- see the note at
# the top -- so we assert the surviving, refusal-unique marker.)
if [ -f "$tmp/cap2/req.2" ] && cat "$tmp/cap2"/req.* 2>/dev/null | grep -q 'spool path too long'; then
    t_ok "B refusal: the tool result the model reads carries 'spool path too long for the 512-byte buffer'"
else
    t_fail "B no refusal: the model's tool result never says 'spool path too long' for a ${sfile_len2}-byte spool path -- the spool name was silently cut and the command (re)started: $(cat "$tmp/cap2"/req.* 2>/dev/null | head_bytes 160)"
fi

# Check 9: NO spool file may appear under $HOME/.jichi.d/bgspool (the bg-*.out
# glob matches nothing -- a refused start opened no spool), and the background
# command must never have run (no sentinel). A disabled refusal would create a
# (truncated-name) spool and write the sentinel.
nfiles2=$(ls "$spool_dir2"/bg-*.out 2>/dev/null | wc -l | tr -d ' ')
if [ "$nfiles2" -eq 0 ] && [ ! -f "$sentinel2" ]; then
    t_ok "B no spool file, no sentinel: the refused command never ran (bg-*.out count 0, sentinel absent)"
else
    t_fail "B the refused command ran anyway: bg-*.out files=$nfiles2, sentinel $([ -f "$sentinel2" ] && echo present || echo absent) -- the refusal did not hold"
fi

t_done
