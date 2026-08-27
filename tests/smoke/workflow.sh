#!/bin/sh
# smoke: deterministic workflow (M101) -- the spec parser + dispatch. A spec
# with no usable stages errors before any model call, so this stays offline.
# (Port of tests/e2e/workflow.py, M210.)
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)
write_config "$tmp/config.json" 9

printf '{ "name": "x", "stages": [] }\n' > "$ws/empty.json"
(cd "$ws" && "$BIN" --config "$tmp/config.json" workflow empty.json \
    < /dev/null > /dev/null 2>"$tmp/err"); rc=$?
if [ $rc -ne 0 ] && grep -q "no usable stages" "$tmp/err"; then
    t_ok "empty-stages spec errors before any model call"
else
    t_fail "rc=$rc err=$(head_bytes 150 "$tmp/err")"
fi

printf 'not json at all\n' > "$ws/bad.json"
(cd "$ws" && "$BIN" --config "$tmp/config.json" workflow bad.json \
    < /dev/null > /dev/null 2>&1); rc=$?
if [ $rc -ne 0 ]; then
    t_ok "invalid-JSON spec errors"
else
    t_fail "invalid JSON accepted (rc=0)"
fi

(cd "$ws" && "$BIN" --config "$tmp/config.json" workflow \
    < /dev/null > /dev/null 2>"$tmp/err2"); rc=$?
if [ $rc -eq 2 ] && grep -q "usage" "$tmp/err2"; then
    t_ok "no spec -> usage error (exit 2)"
else
    t_fail "rc=$rc (want 2) err=$(head_bytes 150 "$tmp/err2")"
fi

t_done
