#!/bin/sh
# jhub-measure-notebooks.sh - what does a Jupyter notebook COST jichi, measured.
#
# WHY.  docs/plans/2026-08-jichi-with-jupyterhub.md states, correctly, that jichi
# has no notebook support: `grep -rc ipynb src/ include/` returns nothing, so an
# .ipynb is a large JSON file with base64 blobs in it.  It then recommends the
# jupytext paired-.py workflow.  That recommendation deserves a number, not an
# adjective -- and the number is obtainable TODAY, with no JupyterHub anywhere,
# because it is a fact about jichi rather than about Jupyter.
#
# METHOD.  For each fixture: run one bounded headless turn against `mockmodel`
# scripted to call read_file on that file, then ask `jichi context history` what
# the resulting tool result cost the conversation.  That subcommand is the right
# instrument because it reports the SAME byte heuristic compaction uses, so the
# number is the one jichi will actually act on -- not one this script invented.
#
# The jsonl `tool_result` event is deliberately NOT used: it carries a bounded
# 512-byte `preview`, not the content, so it cannot answer a size question.
#
# Isolation: a fresh $HOME per measurement (so `context history` reads the right
# session), $JC_CONFIG pinned at an empty file (so a dev box's ./local/config.json
# cannot make a live model call), snapshots/repoMap/references off.
#
# Usage:  sh jhub-measure-notebooks.sh [--out FILE]
# Env:    JHUB_DIR (artifacts), JHUB_REPO (the checkout supplying jichi + mockmodel)
# Exit:   0 measured, 1 a measurement failed, 2 usage
set -eu

OUT=""
for a in "$@"; do
    case "$a" in
        --out) OUT="__next__" ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        -*) echo "unknown option: $a" >&2; exit 2 ;;
        *)  case "$OUT" in __next__) OUT="$a"; continue ;; esac
            echo "unexpected argument: $a" >&2; exit 2 ;;
    esac
done

DIR="${JHUB_DIR:-$HOME/.cache/jichi-jupyterhub}"
REPO="${JHUB_REPO:-$(cd "$(dirname "$0")/.." && pwd)}"
BIN="${JHUB_JICHI:-$REPO/jichi}"
MOCK="${JHUB_MOCKMODEL:-$REPO/tests/tools/mockmodel}"
[ -n "$OUT" ] || OUT="$DIR/results/notebook-cost.md"

for f in "$BIN" "$MOCK"; do
    [ -x "$f" ] || { echo "missing: $f" >&2; exit 1; }
done
[ -d "$DIR/fixtures" ] || { echo "no fixtures -- run jhub-prepare.sh first" >&2; exit 1; }

M="$DIR/measure"
rm -rf "$M"
mkdir -p "$M" "$(dirname "$OUT")"

# one measurement: file -> tokens the tool result cost the conversation
measure() {
    _f="$1"
    _w="$M/$_f.d"
    mkdir -p "$_w/ws" "$_w/home" "$_w/cap"
    cp "$DIR/fixtures/$_f" "$_w/ws/"

    cat > "$_w/replies.mm" <<EOF
wire openai
rule
  count 1
  tool read_file {"path":"$_f"}
rule
  match "\\"role\\":\\"tool\\""
  text MEASURED
rule
  status 500
  body {"error":"unexpected request"}
EOF

    "$MOCK" --script "$_w/replies.mm" --capture "$_w/cap" \
            --port-file "$_w/.port" --deadline 90 --max-requests 3 >/dev/null 2>&1 &
    _mm=$!
    _i=0
    while [ ! -s "$_w/.port" ]; do
        _i=$((_i + 1))
        [ "$_i" -gt 10 ] && { echo "mockmodel never announced a port" >&2; return 1; }
        sleep 1
    done
    _p=$(cat "$_w/.port")

    cat > "$_w/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$_p/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0,"contextLimit":400000}
EOF

    HOME="$_w/home"; export HOME
    printf '%s' '{}' > "$HOME/null-config.json"
    JC_CONFIG="$HOME/null-config.json"; export JC_CONFIG

    ( cd "$_w/ws" && timeout 90 "$BIN" --config "$_w/config.json" --auto --no-lite \
        -q -p "read the file" < /dev/null ) > "$_w/run.out" 2>&1 || true
    kill "$_mm" 2>/dev/null || true
    wait "$_mm" 2>/dev/null || true

    ( cd "$_w/ws" && timeout 60 "$BIN" --config "$_w/config.json" --no-lite \
        context history < /dev/null ) > "$_w/hist.out" 2>&1 || true

    # "    read_file           ~65631    (100%, 1 call)"
    sed -n 's/^ *read_file *~\([0-9][0-9]*\).*/\1/p' "$_w/hist.out" | head -1
}

CAP=262144
printf 'measuring %s\n' "$DIR/fixtures"
: > "$M/rows"
for f in small.ipynb small.py large.ipynb large.py outputs.ipynb outputs.py; do
    [ -f "$DIR/fixtures/$f" ] || continue
    bytes=$(wc -c < "$DIR/fixtures/$f" | tr -d ' ')
    tok=$(measure "$f" || true)
    [ -n "$tok" ] || { echo "  $f: MEASUREMENT FAILED (see $M/$f.d/hist.out)" >&2; exit 1; }
    trunc=no; [ "$bytes" -gt "$CAP" ] && trunc=yes
    printf '%s\t%s\t%s\t%s\n' "$f" "$bytes" "$tok" "$trunc" >> "$M/rows"
    printf '  %-16s %8s bytes -> ~%s tokens%s\n' "$f" "$bytes" "$tok" \
        "$( [ "$trunc" = yes ] && printf ' (truncated at the 256 KB cap)' )"
done

{
    printf '# What a notebook costs jichi -- measured, not estimated\n\n'
    printf 'Produced by `jhub-measure-notebooks.sh` on %s.\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf '%s; fixtures are byte-stable (`jhub-make-fixtures.py`).\n\n' \
           "$("$BIN" --version 2>&1 | head -1)"
    printf 'Instrument: one `read_file` call per fixture against a scripted mock model,\n'
    printf 'then `jichi context history` -- the same byte heuristic compaction uses, so\n'
    printf 'these are the numbers jichi itself acts on.\n\n'
    printf '| file | bytes | tokens the tool result cost | truncated at 256 KB? |\n'
    printf '|---|---:|---:|---|\n'
    while IFS="$(printf '\t')" read -r f b t tr; do
        printf '| `%s` | %s | ~%s | %s |\n' "$f" "$b" "$t" \
            "$( [ "$tr" = yes ] && printf '**yes**' || printf 'no' )"
    done < "$M/rows"
    printf '\n## The comparison that matters\n\n'
    printf '| pair | .ipynb tokens | paired .py tokens | the notebook costs |\n'
    printf '|---|---:|---:|---:|\n'
    for base in small large outputs; do
        nb=$(awk -F'\t' -v k="$base.ipynb" '$1==k{print $3}' "$M/rows")
        py=$(awk -F'\t' -v k="$base.py"    '$1==k{print $3}' "$M/rows")
        [ -n "$nb" ] && [ -n "$py" ] || continue
        printf '| `%s` | ~%s | ~%s | %s |\n' "$base" "$nb" "$py" \
            "$(awk -v a="$nb" -v b="$py" 'BEGIN{ if (b>0) printf "%.0fx", a/b; else printf "-" }')"
    done
    printf '\n'
} > "$OUT"

printf '\nwrote %s\n' "$OUT"
