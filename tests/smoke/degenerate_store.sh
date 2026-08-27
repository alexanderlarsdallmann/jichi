#!/bin/sh
# smoke: hostile/degenerate state in the session store (M198 #1) -- `ls
# --all` against each kind of garbage must survive in BOUNDED TIME with a
# sane exit code, and the healthy session must stay listed. The
# load-bearing case is the FIFO: fopen/fread on a FIFO with no writer used
# to hang /sessions and `ls` forever, so a timeout here is a FAIL, not a
# slow pass. The id/filename-mismatch case must be listed AND loadable
# under its FILENAME stem (the M198 identity rule); Continue's array-shaped
# sessions.json must not surface as a phantom row (M206).
# (Port of tests/e2e/degenerate_store.py, M210.)
. "$(dirname "$0")/_smoke.sh"

t_plan 14

HEALTHY="aaaaaaaa-0000-4000-8000-000000000001"
MISMATCH="bbbbbbbb-0000-4000-8000-000000000002"

session_json() { # session_json SID WS TITLE
    printf '{"sessionId":"%s","title":"%s","workspaceDirectory":"%s",' \
        "$1" "$3" "$2"
    printf '"mode":"chat","history":[{"role":"user","content":"q"}]}'
}

# run one case: prepare a fresh store, apply $2 (a shell function that
# plants the garbage in $store), run ls --all, do the common asserts.
LS_OUT=""
run_case() { # run_case NAME PLANT_FN
    _case="$1"
    ws=$(smoke_tmp)
    HOME=$(smoke_tmp)
    export HOME
    store="$HOME/.jichi.d/sessions"
    mkdir -p "$store"
    session_json "$HEALTHY" "$ws" "synth" > "$store/$HEALTHY.json"
    "$2" "$store"
    (cd "$ws" && with_deadline 15 "$BIN" ls --all < /dev/null \
        > "$store/.out" 2>&1); rc=$?
    LS_OUT="$store/.out"
    chmod -R u+rw "$store" 2>/dev/null
    if [ $rc -ge 124 ]; then
        t_fail "$_case: HUNG (rc=$rc) -- blocked on a store entry"
        return 1
    fi
    if [ $rc -ne 0 ]; then
        t_fail "$_case: rc=$rc (expected 0)"
        return 1
    fi
    if ! grep -q "aaaaaaaa" "$LS_OUT"; then
        t_fail "$_case: the healthy session vanished from the listing"
        return 1
    fi
    return 0
}

plant_fifo()      { mkfifo "$1/cccccccc-0000-4000-8000-000000000003.json"; }
plant_dir()       { mkdir "$1/dddddddd-0000-4000-8000-000000000004.json"; }
plant_zero()      { : > "$1/eeeeeeee-0000-4000-8000-000000000005.json"; }
plant_truncated() {
    printf '{"sessionId":"ffffffff-0000-4000-8000-00000' \
        > "$1/ffffffff-0000-4000-8000-000000000006.json"
}
plant_notjson()   {
    printf 'this is not json at all\n' \
        > "$1/gggggggg-0000-4000-8000-000000000007.json"
}
plant_unreadable() {
    session_json "hhhhhhhh-0000-4000-8000-000000000008" /tmp/x u \
        > "$1/hhhhhhhh-0000-4000-8000-000000000008.json"
    chmod 000 "$1/hhhhhhhh-0000-4000-8000-000000000008.json"
}
plant_tmplitter() { printf 'garbage' > "$1/$HEALTHY.json.tmp12345"; }
plant_mismatch()  {
    session_json "TOTALLY-DIFFERENT-ID" /tmp/x mismatched \
        > "$1/$MISMATCH.json"
}
plant_index()     {
    printf '[{"sessionId":"x","title":"t","messageCount":1}]' \
        > "$1/sessions.json"
}
plant_symloop()   {
    ln -s "$1/iiiiiiii-0000-4000-8000-000000000009.json" \
          "$1/iiiiiiii-0000-4000-8000-000000000009.json"
}
plant_dangling()  {
    ln -s /nonexistent/target \
          "$1/jjjjjjjj-0000-4000-8000-000000000010.json"
}

run_case "fifo"                 plant_fifo       && t_ok "fifo survived"
run_case "directory-named-json" plant_dir        && t_ok "directory survived"
run_case "zero-byte"            plant_zero       && t_ok "zero-byte survived"
run_case "truncated-json"       plant_truncated  && t_ok "truncated survived"
run_case "not-json"             plant_notjson    && t_ok "not-json survived"
run_case "unreadable-mode-000"  plant_unreadable && t_ok "mode-000 survived"
run_case "tmp-litter"           plant_tmplitter  && t_ok "tmp-litter ignored"
run_case "symlink-loop"         plant_symloop    && t_ok "symlink loop survived"
run_case "dangling-symlink"     plant_dangling   && t_ok "dangling link survived"

# id/filename mismatch: survives, is LISTED under its filename stem, and
# is LOADABLE by it (export succeeds and carries the title)
if run_case "id-filename-mismatch" plant_mismatch; then
    t_ok "mismatched store survived"
    if grep -q "bbbbbbbb" "$LS_OUT"; then
        t_ok "mismatched session listed under its filename stem"
    else
        t_fail "mismatched session not listed"
    fi
    with_deadline 15 "$BIN" export "$MISMATCH" < /dev/null \
        > "$LS_OUT.exp" 2>&1; rc=$?
    if [ $rc -eq 0 ] && grep -q "mismatched" "$LS_OUT.exp"; then
        t_ok "mismatched session loadable by its filename stem"
    else
        t_fail "listed but NOT loadable (rc=$rc)"
    fi
else
    t_fail "-"
    t_fail "-"
fi

# Continue's sessions.json (a JSON array) must not surface as a row (M206)
if run_case "continue-index" plant_index; then
    t_ok "continue-index store survived"
    if grep -q "^ *sessions" "$LS_OUT"; then
        t_fail "sessions.json listed as a phantom session row"
    else
        t_ok "array-shaped sessions.json skipped, not a phantom row (M206)"
    fi
else
    t_fail "-"
fi

t_done
