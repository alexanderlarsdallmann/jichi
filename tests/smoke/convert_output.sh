#!/bin/sh
# smoke: jichi-convert's -o honours --force and --dry-run, as its --help promises (2026-11-23).
#
# THE DEFECT. `jichi-convert --help` says "--force  Overwrite existing files (default: skip)" and
# "--dry-run  Show what would be written; write nothing". The --assets path wrote through write_out(),
# which honours both; the plain `-o <path>` path called jc_write_file() directly and honoured neither.
# Measured on 2026-11-23 at a0a8b4ae, and the same code is in the published v0.14.1:
# `jichi-convert config.yaml -o out.json` replaced an existing out.json, rc 0, no warning. The
# README's own migration example was `-o ~/.jichi`, a user's global config. No test ran the
# converter's command line, which is how it went unseen.
#
# UNIVERSE: the `-o <path>` branch of src/convert/jc_convert_main.c, without --assets.
# CELLS (each in its own scratch directory; HOME isolated; stdin closed):
#   1  -o to a NEW path writes the converted config (the positive control: the converter works here)
#   2  -o to an EXISTING path without --force leaves it byte-identical and says "exists; use --force"
#   3  -o to an EXISTING path with --force replaces it with the converted config
#   4  -o with --dry-run writes nothing: a new path stays absent
#   5  -o with --dry-run and --force leaves an existing path byte-identical
# Red without the fix: cells 2, 4 and 5 (the file is written in each).
. "$(dirname "$0")/_smoke.sh"

CONV="$SMOKE_ROOT/jichi-convert"
[ -x "$CONV" ] || t_skip "jichi-convert is not built"

t_plan 5
smoke_home
src=$(smoke_tmp)
cat > "$src/config.yaml" <<'EOF'
name: smoke
version: 1.0.0
models:
  - name: local
    provider: openai
    model: smoke-model
    apiBase: http://127.0.0.1:1/v1
EOF

# conv DIR [ARGS...] -- convert into DIR/out.json; stdout and stderr kept in DIR.
conv() {
    _cv_d=$1; shift
    (cd "$_cv_d" && "$CONV" "$src/config.yaml" -o "$_cv_d/out.json" "$@" \
        < /dev/null > "$_cv_d/stdout" 2> "$_cv_d/stderr")
}

# --- 1: a new path is written (positive control) -----------------------------
d=$(smoke_tmp)
conv "$d"; rc=$?
if [ "$rc" -eq 0 ] && grep -q '"smoke-model"' "$d/out.json" 2>/dev/null; then
    t_ok "-o to a new path writes the converted config (rc 0)"
else
    t_fail "-o to a new path: rc=$rc, out.json $( [ -f "$d/out.json" ] && echo 'lacks the model' || echo 'missing') -- $(head_bytes 200 < "$d/stderr" 2>/dev/null)"
fi

# --- 2: an existing path is kept without --force ------------------------------
d=$(smoke_tmp)
printf 'EXISTING-MARKER\n' > "$d/out.json"
cp "$d/out.json" "$d/orig"
conv "$d"
if cmp -s "$d/orig" "$d/out.json" && grep -q 'exists; use --force' "$d/stderr"; then
    t_ok "-o to an existing path without --force leaves it byte-identical and says so"
else
    t_fail "-o overwrote an existing file without --force, or did not say why it skipped: $(head_bytes 120 < "$d/out.json") / $(head_bytes 200 < "$d/stderr")"
fi

# --- 3: --force replaces it ---------------------------------------------------
d=$(smoke_tmp)
printf 'EXISTING-MARKER\n' > "$d/out.json"
conv "$d" --force; rc=$?
if [ "$rc" -eq 0 ] && grep -q '"smoke-model"' "$d/out.json" && ! grep -q 'EXISTING-MARKER' "$d/out.json"; then
    t_ok "-o with --force replaces an existing file with the converted config"
else
    t_fail "-o with --force did not replace the file: rc=$rc, $(head_bytes 120 < "$d/out.json")"
fi

# --- 4: --dry-run writes nothing to a new path ---------------------------------
d=$(smoke_tmp)
conv "$d" --dry-run
if [ ! -e "$d/out.json" ]; then
    t_ok "-o with --dry-run writes nothing (a new path stays absent)"
else
    t_fail "-o with --dry-run wrote $d/out.json"
fi

# --- 5: --dry-run with --force leaves an existing path alone ---------------------
d=$(smoke_tmp)
printf 'EXISTING-MARKER\n' > "$d/out.json"
cp "$d/out.json" "$d/orig"
conv "$d" --dry-run --force
if cmp -s "$d/orig" "$d/out.json"; then
    t_ok "-o with --dry-run --force leaves an existing file byte-identical"
else
    t_fail "-o with --dry-run --force changed an existing file: $(head_bytes 120 < "$d/out.json")"
fi

t_done
