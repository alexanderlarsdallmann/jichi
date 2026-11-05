#!/bin/sh
# smoke: D-21 (M993) setup --onboard/--advisor sent the user's key to the
# untrusted project's endpoint and wrote drafts through links/over files.
#
# THE DEFECT. The default project config path (local/config.json or
# .jichi/config.json) was passed to setup_advisor/setup_onboard as an
# "explicit" path to jc_config_load, which then bypassed the M851 project
# trust gate entirely. The user's API key was carried to the project's
# endpoint, and draft writes (setup_draft_write) followed symlinks and
# overwrote existing files without checking.
#
# THE FIX. setup_gate_project_trusted now treats the default project config
# path as IMPLICIT, so it checks jc_cfgtrust_is_trusted before applying the
# config. A user's explicit --config is still allowed without the gate.
# setup_draft_write uses lstat to refuse symlinks and existing regular files.
# setup_draft_write also checks that the PARENT directory of the write path
# resolves under the workspace root, preventing a symlinked .jichi directory
# from carrying writes outside the workspace (review-15 P-1).
#
# CHECKED per leg:
#   A: untrusted project config does NOT receive the model call (advisor)
#   B: explicit --config bypasses the gate (control)
#   C: symlinked draft is refused
#   D: existing draft is not overwritten
#   E: after trust, untrusted project config works (consent control)
#   F: symlinked PARENT directory is rejected (review-15 P-1 parent symlink)
#
# NOT CHECKED: the Authorization header value itself (legs assert by endpoint
# + chat count). The key value was captured in the lead's first-hand scratch
# repro, quoted in the LOG.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws_a="$tmp/ws_a"
ws_b="$tmp/ws_b"
ws_c="$tmp/ws_c"
ws_d="$tmp/ws_d"
ws_e="$tmp/ws_e"
ws_f="$tmp/ws_f"

# Create the shared mock file: one catch-all rule answering reach, list, and
# chat POST (the mock reads the first rule that matches; catch-all must be last)
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  status 200
  body {"id":"1","object":"chat.completion","model":"m","choices":[{"index":0,"message":{"role":"assistant","content":"ADVICE_42"},"finish_reason":"stop"}],"usage":{"prompt_tokens":3,"completion_tokens":3,"total_tokens":6}}
EOF

# Global config helper: write $HOME/.jichi with model "g" at <port>
global_config() {
    cat > "$HOME/.jichi" <<EOF
{"lowResource":false,"models":[{"name":"g","provider":"openai","model":"g","apiBase":"http://127.0.0.1:$1/v1","apiKey":"x","roles":["chat"]}],"snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF
}

# Project config helper: write $ws/local/config.json with model "p" at <port>
# $1=port, $2=apiKey (literal) or "" ( apiKeyEnv )
project_config() {
    _pc_port=$1; _pc_ws=$2; _pc_apikey=$3
    mkdir -p "$_pc_ws/local"
    if [ -n "$_pc_apikey" ]; then
        printf '%s\n' '{"lowResource":false,"models":[{"name":"p","provider":"openai","model":"p","apiBase":"http://127.0.0.1:'"$_pc_port"'/v1","apiKey":"'"$_pc_apikey"'","roles":["chat"]}],"snapshots":false,"repoMap":false,"references":false,"maxRetries":0}' > "$_pc_ws/local/config.json"
    else
        printf '%s\n' '{"lowResource":false,"models":[{"name":"p","provider":"openai","model":"p","apiBase":"http://127.0.0.1:'"$_pc_port"'/v1","apiKeyEnv":"D21_KEY","roles":["chat"]}],"snapshots":false,"repoMap":false,"references":false,"maxRetries":0}' > "$_pc_ws/local/config.json"
    fi
}

# chat_count DIR -- count files in DIR matching POST /v1/chat/completions
chat_count() {
    _cc_dir=$1
    _cc_n=0
    if [ -d "$_cc_dir" ]; then
        _cc_n=$(find "$_cc_dir" -type f -name "req.*" 2>/dev/null | while read -r f; do
            grep -l 'POST /v1/chat/completions' "$f" 2>/dev/null
        done | wc -l)
    fi
    printf '%s' "$_cc_n"
}

# LEG A (the gate, advisor): two mocks P1 (global), P2 (project). Global g@P1,
# project p@P2 apiKeyEnv D21_KEY=USERSECRETKEY9876543210, UNTRUSTED.
# (cd $ws && with_deadline 60 $BIN setup --advisor </dev/null)
mkdir -p "$ws_a"
mkdir -p "$tmp/cap_a_p1"
mm_start "$tmp/replies.mm" "$tmp/cap_a_p1"
_p1=$MM_PORT
mkdir -p "$tmp/cap_a_p2"
mm_start "$tmp/replies.mm" "$tmp/cap_a_p2"
_p2=$MM_PORT
global_config "$_p1"
project_config "$_p2" "$ws_a" ""
export D21_KEY=USERSECRETKEY9876543210
(cd "$ws_a" && unset JC_CONFIG && with_deadline 60 "$BIN" setup --advisor </dev/null) > "$tmp/a_out" 2>&1
rc=$?
unset D21_KEY
mm_stop
mm_stop
# FIRST require the model call happened (a non-empty capture, or the POST in it)
# and t_fail otherwise -- a leg that passes on a run that never called the model
# is vacuous
_n1=$(chat_count "$tmp/cap_a_p1")
_n2=$(chat_count "$tmp/cap_a_p2")
if [ "$_n2" -ge 1 ]; then
    t_fail "LEG A: gate NOT applied -- P1 chats: $_n1, P2 chats: $_n2 (want >=1 / 0)"
elif [ "$_n1" -eq 0 ] && [ "$rc" -ge 124 ]; then
    t_fail "LEG A: command timed out"
elif [ "$_n1" -eq 0 ] && [ "$rc" -ne 0 ]; then
    t_fail "LEG A: model call did not happen (vacuous) -- rc=$rc, cap1=$tmp/cap_a_p1, cap2=$tmp/cap_a_p2"
elif [ "$_n1" -eq 0 ]; then
    t_fail "LEG A: model call did not happen (vacuous) -- no POST at P1 (cap1)"
elif ! grep -q 'not trusted' "$tmp/a_out"; then
    t_fail "LEG A: gate NOT applied -- 'not trusted' notice missing from output"
else
    t_ok "LEG A: gate applied -- P1 chats: $_n1 (>=1), P2 chats: $_n2 (0), 'not trusted' notice present"
fi

# LEG B (explicit control): same ws/configs as A, setup --advisor --config local/config.json
# Green: chat POST at P2 >=1, at P1 = 0. Green before AND after the fix (control
# that --config bypasses the gate).
mkdir -p "$ws_b"
mkdir -p "$tmp/cap_b_p1"
mm_start "$tmp/replies.mm" "$tmp/cap_b_p1"
_p1=$MM_PORT
mkdir -p "$tmp/cap_b_p2"
mm_start "$tmp/replies.mm" "$tmp/cap_b_p2"
_p2=$MM_PORT
global_config "$_p1"
project_config "$_p2" "$ws_b" ""
export D21_KEY=USERSECRETKEY9876543210
(cd "$ws_b" && unset JC_CONFIG && with_deadline 60 "$BIN" setup --advisor --config local/config.json </dev/null) >/dev/null 2>&1
rc=$?
unset D21_KEY
mm_stop
mm_stop
_n1=$(chat_count "$tmp/cap_b_p1")
_n2=$(chat_count "$tmp/cap_b_p2")
if [ "$_n2" -eq 0 ] && [ "$rc" -ge 124 ]; then
    t_fail "LEG B: command timed out"
elif [ "$_n2" -eq 0 ]; then
    t_fail "LEG B: model call did not happen at P2 (vacuous) -- P2 POSTs: $_n2"
elif [ "$_n2" -ge 1 ] && [ "$_n1" -eq 0 ]; then
    t_ok "LEG B: explicit --config bypasses gate -- P1 chats: $_n1 (0), P2 chats: $_n2 (>=1)"
else
    t_fail "LEG B: control failed -- P1 chats: $_n1, P2 chats: $_n2 (want 0 / >=1)"
fi

# LEG C (planted symlink, onboard): ONE mock P; project config model p@P with
# a LITERAL apiKey 'x' (so the oneshot runs); global g@P too (harmless).
# Plant .jichi/onboarding.analysis.md -> outside marker file (ln -s, marker OUTSIDE_ANALYSIS_M993E2E).
# (cd $ws && with_deadline 60 $BIN setup --onboard --config local/config.json </dev/null)
mkdir -p "$ws_c"
mkdir -p "$ws_c/.jichi"
global_config 5
project_config 6 "$ws_c" "x"
outside_c="$tmp/outside_analysis_c.md"
printf '%s\n' "OUTSIDE_ANALYSIS_M993E2E" > "$outside_c"
mkdir -p "$tmp/cap_c"
mm_start "$tmp/replies.mm" "$tmp/cap_c"
_p=$MM_PORT
global_config "$_p"
project_config "$_p" "$ws_c" "x"
ln -s "$outside_c" "$ws_c/.jichi/onboarding.analysis.md"
export D21_KEY=USERSECRETKEY9876543210
(cd "$ws_c" && unset JC_CONFIG && with_deadline 60 "$BIN" setup --onboard --config local/config.json </dev/null) >/dev/null 2>&1
rc=$?
unset D21_KEY
mm_stop
# FIRST require the model call: a chat POST in the cap, else t_fail 'LEG C: model call did not happen (vacuous)'
_n=$(chat_count "$tmp/cap_c")
if [ "$_n" -eq 0 ] && [ "$rc" -ge 124 ]; then
    t_fail "LEG C: command timed out"
elif [ "$_n" -eq 0 ]; then
    t_fail "LEG C: model call did not happen (vacuous) -- no POST at cap_c"
elif [ -L "$ws_c/.jichi/onboarding.analysis.md" ] && [ -f "$outside_c" ] && grep -q "OUTSIDE_ANALYSIS_M993E2E" "$outside_c" && ! grep -q "ADVICE_42" "$outside_c"; then
    t_ok "LEG C: symlink write refused -- outside file still contains marker (not ADVICE_42)"
elif [ -f "$ws_c/.jichi/onboarding.analysis.md" ]; then
    # Check if the target was modified
    if grep -q "ADVICE_42" "$outside_c"; then
        t_fail "LEG C: symlink target modified (ADVICE_42 found in outside file)"
    else
        t_ok "LEG C: symlink write refused -- outside file unchanged"
    fi
else
    t_ok "LEG C: symlink write refused -- draft file not created"
fi

# LEG D (no-overwrite, onboard): same setup as C but .jichi/onboarding.analysis.md a REAL file with marker OLD_ANALYSIS_M993E2E.
# Require the model call; then file still contains the marker + 'refused' in output.
mkdir -p "$ws_d"
mkdir -p "$ws_d/.jichi"
global_config 7
project_config 8 "$ws_d" "x"
printf '%s\n' "OLD_ANALYSIS_M993E2E" > "$ws_d/.jichi/onboarding.analysis.md"
mkdir -p "$tmp/cap_d"
mm_start "$tmp/replies.mm" "$tmp/cap_d"
_p=$MM_PORT
global_config "$_p"
project_config "$_p" "$ws_d" "x"
export D21_KEY=USERSECRETKEY9876543210
(cd "$ws_d" && unset JC_CONFIG && with_deadline 60 "$BIN" setup --onboard --config local/config.json </dev/null) > "$tmp/d_out" 2>&1
rc=$?
unset D21_KEY
mm_stop
_n=$(chat_count "$tmp/cap_d")
if [ "$_n" -eq 0 ] && [ "$rc" -ge 124 ]; then
    t_fail "LEG D: command timed out"
elif [ "$_n" -eq 0 ]; then
    t_fail "LEG D: model call did not happen (vacuous) -- no POST at cap_d"
elif grep -q "OLD_ANALYSIS_M993E2E" "$ws_d/.jichi/onboarding.analysis.md" && grep -q "refused" "$tmp/d_out"; then
    t_ok "LEG D: no-overwrite -- file unchanged (marker present) + 'refused' in output"
else
    _marker=$(grep -c "OLD_ANALYSIS_M993E2E" "$ws_d/.jichi/onboarding.analysis.md" 2>/dev/null || echo 0)
    _refused=$(grep -c "refused" "$tmp/d_out" 2>/dev/null || echo 0)
    t_fail "LEG D: overwrite happened or no refused notice -- marker count: $_marker, refused count: $_refused"
fi

# LEG E (trust flow): two mocks P1 (global), P2 (project, apiKeyEnv, UNTRUSTED like A).
# First (cd $ws && with_deadline 60 $BIN trust </dev/null) -- require rc 0 and its output contains 'trusted';
# then setup --advisor. Green: chat POST at P2 >=1, at P1 = 0, NO 'not trusted' notice.
mkdir -p "$ws_e"
mkdir -p "$tmp/cap_e_p1"
mm_start "$tmp/replies.mm" "$tmp/cap_e_p1"
_p1=$MM_PORT
mkdir -p "$tmp/cap_e_p2"
mm_start "$tmp/replies.mm" "$tmp/cap_e_p2"
_p2=$MM_PORT
global_config "$_p1"
project_config "$_p2" "$ws_e" ""
# trust requires the project config with the real port before it runs
export D21_KEY=USERSECRETKEY9876543210
# First trust
(cd "$ws_e" && unset JC_CONFIG && with_deadline 60 "$BIN" trust </dev/null) > "$tmp/e_trust_out" 2>&1
_trust_rc=$?
if [ "$_trust_rc" -ne 0 ]; then
    t_fail "LEG E: trust command failed (rc=$_trust_rc)"
elif ! grep -q 'trusted' "$tmp/e_trust_out"; then
    t_fail "LEG E: trust did not output 'trusted': $(tail -c 200 "$tmp/e_trust_out")"
else
    (cd "$ws_e" && unset JC_CONFIG && with_deadline 60 "$BIN" setup --advisor </dev/null) >/dev/null 2>&1
    _adv_rc=$?
    unset D21_KEY
    mm_stop
    mm_stop
    _n1=$(chat_count "$tmp/cap_e_p1")
    _n2=$(chat_count "$tmp/cap_e_p2")
    if [ "$_adv_rc" -ge 124 ]; then
        t_fail "LEG E: advisor command timed out"
    elif [ "$_n2" -ge 1 ] && [ "$_n1" -eq 0 ]; then
        t_ok "LEG E: trust flow -- P1 chats: $_n1 (0), P2 chats: $_n2 (>=1)"
    else
        t_fail "LEG E: trust flow failed -- P1 chats: $_n1, P2 chats: $_n2 (want 0 / >=1)"
    fi
fi

# LEG F (parent symlink escape, review-15 P-1): same mock setup as LEG D but
# workspace has .jichi symlinked to an outside directory. The advisor should
# refuse to write through the symlinked parent.
mkdir -p "$ws_f"
mkdir -p "$ws_f/.jichi"
global_config 11
project_config 12 "$ws_f" "x"
outside_f="$tmp/outside_parent_f"
mkdir -p "$outside_f"
# Make sure the outside dir is initially empty
[ -z "$(ls -A "$outside_f" 2>/dev/null)" ] || rm -rf "$outside_f"/*
mkdir -p "$tmp/cap_f"
mm_start "$tmp/replies.mm" "$tmp/cap_f"
_p=$MM_PORT
global_config "$_p"
project_config "$_p" "$ws_f" "x"
# Remove the regular .jichi and create a symlink to outside_f
rm -rf "$ws_f/.jichi"
ln -s "$outside_f" "$ws_f/.jichi"
export D21_KEY=USERSECRETKEY9876543210
(cd "$ws_f" && unset JC_CONFIG && with_deadline 60 "$BIN" setup --advisor </dev/null) > "$tmp/f_out" 2>&1
rc=$?
unset D21_KEY
mm_stop
# FIRST require the model call happened (a chat POST in the cap, else t_fail 'LEG F: model call did not happen (vacuous)'
_n=$(chat_count "$tmp/cap_f")
if [ "$_n" -eq 0 ] && [ "$rc" -ge 124 ]; then
    t_fail "LEG F: command timed out"
elif [ "$_n" -eq 0 ]; then
    t_fail "LEG F: model call did not happen (vacuous) -- no POST at cap_f"
elif [ -z "$(ls -A "$outside_f" 2>/dev/null)" ]; then
    # Outside dir is empty - good, but also need 'refused' in output
    if grep -q "refused" "$tmp/f_out"; then
        t_ok "LEG F: parent symlink escape refused -- outside dir empty + 'refused' in output"
    else
        t_fail "LEG F: parent symlink escape -- outside dir empty but no 'refused' in output: $(tail -c 300 "$tmp/f_out")"
    fi
else
    # Outside dir is NOT empty - the write went through (BAD before fix)
    t_fail "LEG F: write landed outside through symlinked parent: $(ls -la "$outside_f" 2>/dev/null)"
fi

# Teardown
rm -rf "$tmp"

t_done
