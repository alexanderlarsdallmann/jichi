#!/bin/sh
# smoke: D-02 regression - clean before reset for D-02 (M977).
#
# THE DEFECT. A file ignored NOW but NOT at the checkpoint was being deleted
# by undo because 'git clean' ran AFTER 'git reset --hard'. The reset restored
# an older .gitignore that no longer ignored the file, so clean deleted it.
# The dry-run preview (git clean -nd) ran before reset and never listed it,
# creating a mismatch between what undo would do and what it said it would do.
#
# THE FIX. Run 'git clean' BEFORE 'git reset --hard' in restore_to. Now clean
# reads the same rules as the dry-run preview (which ran before reset). A file
# ignored now is never touched; one the restored .gitignore no longer ignores
# survives as an untracked file - the documented promise.
#
# THE TEST. A dataset/ directory with a file is created and added to .gitignore
# AFTER the first checkpoint. A second checkpoint is taken (dataset/ is ignored
# now, so not captured). Undo to the first checkpoint should leave dataset/x.bin
# intact, but the unfixed code deletes it. This driver runs the sequence through
# the real jichi binary and asserts the dataset file survives.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "needs git"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws="$tmp/ws"
mkdir -p "$ws"

# Create initial repo with one file
(cat << 'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"data.txt"}
rule
  count 2
  tool write_file {"path":"data.txt","content":"v1\n"}
rule
  count 3
  text done.
rule
  status 500
  body {"error":"unexpected request"}
EOF
) > "$tmp/replies.mm"

mm_start "$tmp/replies.mm" "$tmp/cap" 2

cat > "$tmp/config.json" <<CFG
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":true,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
CFG

(cd "$ws" && git init -q . && git config user.email t@example.com &&
 git config user.name t &&
 printf 'v1\n' > data.txt &&
 git add -A && git commit -qm first) >/dev/null 2>&1

# Run once to create checkpoint 1 (write_file triggers snapshot)
(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" --auto \
    -p "write v1" < /dev/null) > "$tmp/run1.out" 2>&1
mm_stop

# Verify checkpoint 1 was created
cd "$ws" && checkpoints=$(with_deadline 30 "$BIN" checkpoints < /dev/null 2>&1)
case "$checkpoints" in
    *"1"*)
        t_ok "checkpoint 1 created" ;;
    *)
        t_fail "no checkpoint 1: $checkpoints" ;;
esac

# Add dataset/ with a file, and .gitignore it (ignored now, but not at checkpoint 1)
mkdir -p "$ws/dataset"
printf 'ignored-data\n' > "$ws/dataset/x.bin"
printf 'dataset/\n' > "$ws/.gitignore"

# Verify dataset/x.bin exists before undo (control)
if [ -e "$ws/dataset/x.bin" ]; then
    t_ok "dataset/x.bin exists before undo (control)"
else
    t_fail "dataset/x.bin missing before undo - something broke the fixture"
fi

# Undo to checkpoint 1
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" undo < /dev/null 2>&1)
case "$out" in
    *"reverted"*)
        t_ok "undo completed" ;;
    *)
        t_fail "undo failed: $out" ;;
esac

# D-02 check: dataset/x.bin should STILL exist because:
# - It's ignored NOW (by the current .gitignore containing "dataset/")
# - But it WAS tracked at checkpoint 1 (no .gitignore back then)
# - On the unfixed tree: clean runs AFTER reset, reads restored .gitignore, deletes it
# - On the fixed tree: clean runs BEFORE reset, uses current .gitignore, leaves it alone
if [ -e "$ws/dataset/x.bin" ]; then
    t_ok "D-02 regression: dataset/x.bin survives undo"
else
    t_fail "D-02: dataset/x.bin was deleted by undo - clean ran after reset"
fi

t_done
