#!/bin/sh
# smoke: `jichi-harness doctor` -- the global `--config`, the offline checks in
# jichi's doctor output shape (the summary line, the exit class, the JSON form),
# the canary-never-leaks rule, and the settings-table lint that keeps
# docs/HARNESS.md from drifting from the JICHI_HARNESS_* names in the loader
# header.
#
# M1052 (H1b). Design note:
#   docs/internal/drive/2026-10-23-harness/notes/M1052.md
# Output-shape precedent: tests/smoke/doctor.sh + src/util/jc_doctor.c (the
# `%d ok, %d warning(s), %d problem(s)` summary line, exit 1 on a problem, and
# the `--output json` form). Ported reference: the drive's own live doctor,
# docs/internal/drive/2026-10-23-harness/harness/doctor-live.sh (the
# `jichi --config <file> doctor` spelling; "never prints the key").
#
# THE RED THIS IS HELD BY. On the tree before the change `doctor` is the H0
# "not built yet" refusal (exit 2) and there is no `--config` option: check 1
# below fails at its first line -- the valid-config doctor exits 2, not 0. That
# is the whole point of the driver. (Checks 2,3,5,6,7,8,9 also fail in the red
# state; check 4 -- the canary is absent -- passes trivially because there is
# no output, and check 10 -- the version parity -- passes because both
# binaries are the same tree.)
#
# WHAT IS CHECKED:
#   1. `--config <valid> doctor` exits 0 on a fully-valid harness config.
#   2. that output carries a jichi-doctor summary line (`N ok, `).
#   3. the model key is reported BY SETTING NAME (JICHI_HARNESS_MODEL_KEY_FILE).
#   4. the canary (a secret file's CONTENTS) never appears in the output -- a
#      path may be printed, the key's value may not.
#   5. a config whose modelKeyFile reference is ABSENT exits 1 (not the
#      exit-2 not-built class): an absent reference is "stopped before its
#      request", a reportable problem.
#   6. that output names JICHI_HARNESS_MODEL_KEY_FILE and carries the 10.4
#      item-3 refusal ("not set" / "nothing to look for").
#   7. `doctor` with NO --config exits 1 (a problem, not the exit-2 class) and
#      names --config.
#   8. `--config <valid> doctor --output json` parses (jsonq) and carries
#      "fail":0.
#   9. the settings-table lint: each JICHI_HARNESS_* name read OUT of
#      include/jc_harness_config.h appears in docs/HARNESS.md. A rename in the
#      header with a forgotten table row goes red.
#  10. jichi-harness and jichi report the SAME version (M495: one tree). smoke_lint
#      check 5 requires this non-lint driver to run the tree's "$BIN" once; the
#      assertion is a harness that drifted from the tree would say a different
#      version.

. "$(dirname "$0")/_smoke.sh"

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HBIN="${JC_SMOKE_HBIN:-$ROOT/jichi-harness}"

# The loader resolves each top-level reference from its JICHI_HARNESS_* env
# setting FIRST (env wins over the config's own value). An ambient env would
# then defeat the "absent reference" cases (checks 5-7), so clear all three.
unset JICHI_HARNESS_DIR JICHI_HARNESS_MODEL_KEY_FILE JICHI_HARNESS_GIT_TOKEN_FILE

t_plan 10
smoke_home
tmp=$(smoke_tmp)

# A fresh random 32-hex-digit canary (never a fixed string): it rides in the
# secret files' CONTENTS and must not reach doctor's output (a path is not a
# secret; the key's value is).
canary=$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')
[ -n "$canary" ] || canary="deadbeefc0ffee$$_deadbeefc0ffee"

# (a) the secret files: the canary is their CONTENTS. doctor checks that the
#     file a reference NAMES exists; it never opens it or prints its contents.
printf '%s\n' "$canary" > "$tmp/model.key"
printf '%s\n' "$canary" > "$tmp/git.token"
# A jichi config for doctor to reference (its own schema; doctor only stats the
# path, never parses it).
printf '{}\n' > "$tmp/jichi.json"

# (b) a VALID harness config pointing at those files. `dir` is required by the
#     loader; the three secret references point at files that exist.
cat > "$tmp/cfg.json" <<EOF
{"dir":"$tmp","modelKeyFile":"$tmp/model.key",
"gitTokenFile":"$tmp/git.token","jichiConfig":"$tmp/jichi.json"}
EOF

# (c) a config whose modelKeyFile reference is ABSENT (for the exit-1 case).
cat > "$tmp/cfg-nomodelkey.json" <<EOF
{"dir":"$tmp","gitTokenFile":"$tmp/git.token",
"jichiConfig":"$tmp/jichi.json"}
EOF

# --- 1: a fully-valid config: doctor exits 0 --------------------------------
"$HBIN" --config "$tmp/cfg.json" doctor < /dev/null > "$tmp/d1" 2> "$tmp/d1.err"
rc=$?
if [ "$rc" -ne 0 ]; then
    t_fail "1: \`--config <valid> doctor\` exited $rc (expected 0) -- on the unfixed tree doctor is the H0 'not built yet' refusal (or --config is an unknown option); output: $(cat "$tmp/d1" "$tmp/d1.err" | head_bytes 160)"
else
    t_ok "1: \`--config <valid> doctor\` exits 0 on a fully-valid config"
fi

# --- 2: the output carries a jichi-doctor summary line ----------------------
if grep -qE '(^|[[:space:]])[0-9]+ ok, ' "$tmp/d1"; then
    t_ok "2: doctor prints a jichi-doctor summary line ('N ok, ')"
else
    t_fail "2: no summary line ('N ok, ') in doctor output: $(head_bytes 160 "$tmp/d1")"
fi

# --- 3: the model key is reported BY SETTING NAME ---------------------------
if grep -qF "JICHI_HARNESS_MODEL_KEY_FILE" "$tmp/d1"; then
    t_ok "3: the model key reference is named by setting (JICHI_HARNESS_MODEL_KEY_FILE)"
else
    t_fail "3: doctor does not name JICHI_HARNESS_MODEL_KEY_FILE (the secret is reported by SETTING NAME): $(head_bytes 200 "$tmp/d1")"
fi

# --- 4: the canary (the key's CONTENTS) never appears -----------------------
if grep -qF "$canary" "$tmp/d1" "$tmp/d1.err" 2>/dev/null; then
    t_fail "4: the canary (a secret file's contents) appeared in doctor's output -- doctor may print the reference's path, never the key's value"
else
    t_ok "4: the canary does not appear anywhere in doctor's output (the key's value is never printed)"
fi

# --- 5: an ABSENT modelKeyFile reference exits 1 ----------------------------
"$HBIN" --config "$tmp/cfg-nomodelkey.json" doctor < /dev/null > "$tmp/d5" 2> "$tmp/d5.err"
rc=$?
if [ "$rc" -ne 1 ]; then
    t_fail "5: \`--config <missing modelKeyFile> doctor\` exited $rc (expected 1, not the exit-2 not-built class): $(cat "$tmp/d5" "$tmp/d5.err" | head_bytes 160)"
else
    t_ok "5: an absent modelKeyFile reference exits 1 (a problem, not a usage error)"
fi

# --- 6: that output names the setting and carries the 10.4 item-3 refusal ----
if grep -qF "JICHI_HARNESS_MODEL_KEY_FILE" "$tmp/d5" && \
   grep -qEi 'not set|nothing to look for' "$tmp/d5"; then
    t_ok "6: the refusal names JICHI_HARNESS_MODEL_KEY_FILE and says it is not set / nothing to look for"
else
    t_fail "6: the missing-modelKeyFile output does not name JICHI_HARNESS_MODEL_KEY_FILE with the 'not set / nothing to look for' refusal (plan 10.4 item 3): $(head_bytes 200 "$tmp/d5")"
fi

# --- 7: doctor with NO --config exits 1 and names --config ------------------
"$HBIN" doctor < /dev/null > "$tmp/d7" 2> "$tmp/d7.err"
rc=$?
if [ "$rc" -ne 1 ] || ! grep -qF -- "--config" "$tmp/d7" "$tmp/d7.err" 2>/dev/null; then
    t_fail "7: \`doctor\` with no --config exited $rc (expected 1, a problem not the exit-2 not-built class) and/or does not name --config: $(cat "$tmp/d7" "$tmp/d7.err" | head_bytes 160)"
else
    t_ok "7: doctor with no --config exits 1 (a problem) and names --config"
fi

# --- 8: --output json parses and carries "fail":0 ----------------------------
"$HBIN" --config "$tmp/cfg.json" doctor --output json < /dev/null > "$tmp/d8.json" 2>/dev/null
if "$SMOKE_TOOLS/jsonq" -q '.' "$tmp/d8.json" 2>/dev/null && \
   grep -q '"fail":0' "$tmp/d8.json"; then
    t_ok "8: \`doctor --output json\` parses (jsonq) and carries \"fail\":0"
else
    t_fail "8: doctor --output json did not parse as a single JSON document or does not carry \"fail\":0: $(head_bytes 160 "$tmp/d8.json")"
fi

# --- 9: the settings-table lint ---------------------------------------------
# Read the JICHI_HARNESS_* names OUT of the loader header (the single source)
# and check each appears in docs/HARNESS.md's settings table. This is the check
# that forces the doc change: a rename in the header with a forgotten row goes
# red here, per D13 item 4 ("the table is generated or linted from the source,
# so it cannot drift").
_hdr="$ROOT/include/jc_harness_config.h"
_names=$(sed -n 's/^#define JC_HARNESS_ENV_[A-Za-z_]*[[:space:]]*"\([A-Za-z_]*\)".*/\1/p' "$_hdr" | sort -u)
_n=$(printf '%s\n' "$_names" | grep -c . )
if [ "$_n" -ne 3 ]; then
    t_fail "9: read $_n JICHI_HARNESS_* names out of include/jc_harness_config.h (expected 3) -- the lint cannot verify the table it cannot enumerate"
else
    _miss=""
    for _nm in $_names; do
        grep -qF "$_nm" "$ROOT/docs/HARNESS.md" || _miss="$_miss $_nm"
    done
    if [ -n "$_miss" ]; then
        t_fail "9: docs/HARNESS.md is missing:$_miss -- every JICHI_HARNESS_* name read from the loader header must appear in the settings table (D13 item 4, the table cannot drift)"
    else
        t_ok "9: the settings table names all three JICHI_HARNESS_* settings read from the loader header"
    fi
fi

# --- 10: jichi-harness and jichi are the same tree (M495 version parity) -----
# smoke_lint check 5 requires this non-lint driver to run the tree's "$BIN"
# once. The assertion: a third program that came from a DIFFERENT build than
# the tree's jichi would report a different version, and this driver fails it.
"$HBIN" --version > "$tmp/hv" 2>/dev/null
"$BIN"  --version > "$tmp/jv" 2>/dev/null
_hv=$(sed -n '1p' "$tmp/hv")
_jv=$(sed -n '1p' "$tmp/jv")
_hvtok=${_hv#* }
_jvtok=${_jv#* }
if [ -n "$_hvtok" ] && [ "$_hvtok" = "$_jvtok" ]; then
    t_ok "10: jichi-harness and jichi report the same version ($_hvtok) -- one tree"
else
    t_fail "10: version mismatch -- jichi-harness says '$_hv' but jichi says '$_jv'; the harness must be built from the same tree as jichi (M495)"
fi

t_done
