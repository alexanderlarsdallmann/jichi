#!/bin/sh
# smoke: headless has three verbosity levels, and stdout is only the answer
# (M326h; design in docs/proposals/2026-08-headless-progress.md).
#
# Before this there were two: a firehose and silence. The default printed the
# RAW tool-argument JSON -- so a write_file of a 200 KB file wrote 200 KB to
# stderr -- while `-q` printed nothing at all, and the TUI (which has less room)
# had had the sensible middle setting all along via jc_tool_arg_summary.
#
# stdout separately carried one blank line per TOOL ROUND, because
# hl_message_end wrote a separator for assistant messages that produced no text.
# `docs/EMBEDDING.md` says "stdout is the answer"; `\n\n\nAll done.` is not.
#
# WHAT IS AND IS NOT PINNED HERE. stderr text is explicitly not an interface
# (EMBEDDING.md: "never parse stderr"), so this driver does NOT freeze the exact
# wording. It pins the three PROPERTIES that were the defect: silence stays
# silent, the default names the tool without dumping its arguments, and -v can
# still get the arguments back. stdout, which IS a contract, is pinned exactly.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# write_file carries an argument that must not reach stderr by default: the
# `content` key is the one that scales with the file. It is 900 bytes here --
# just under the mock's 1024-byte script-line limit -- so the size check below
# is two-sided: the pre-fix binary blows the bound, the fixed one does not. A
# 30-byte payload (the first draft) passed in BOTH directions and proved
# nothing, which is the failure docs/TEST_INTEGRITY.md is about.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool list_files {"path":"."}
rule
  count 2
  tool write_file {"path":"out.txt","content":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"}
rule
  text All done.
EOF

# One mock per invocation: the scripted rules are consumed per connection.
run_level() {
    mm_start "$tmp/replies.mm" "$tmp"
    write_config "$tmp/config.json" "$MM_PORT"
    rm -f "$ws/out.txt"
    (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
        --no-session --auto $1 -p "do it" < /dev/null \
        > "$tmp/out.$2" 2> "$tmp/err.$2")
    mm_stop
}

run_level "-q" q
run_level ""   d
run_level "-v" v

# --- 1: -q is silent ---------------------------------------------------------
if [ ! -s "$tmp/err.q" ]; then
    t_ok "-q writes nothing to stderr"
else
    t_fail "-q wrote $(wc -c < "$tmp/err.q") bytes: $(head_bytes 120 "$tmp/err.q")"
fi

# --- 2-4: the default names the tool, bounded --------------------------------
if grep -q 'write_file' "$tmp/err.d" && grep -q 'out.txt' "$tmp/err.d"; then
    t_ok "the default names the tool and its subject"
else
    t_fail "default lost the tool line: $(head_bytes 160 "$tmp/err.d")"
fi

# The defect itself: the argument payload must not be there.
if ! grep -q '"content"' "$tmp/err.d" && ! grep -q 'AAAAAAAAAA' "$tmp/err.d"; then
    t_ok "the default does not dump the tool arguments"
else
    t_fail "raw arguments still on stderr: $(grep -o 'AAAAAAAAAA' "$tmp/err.d" | head -1)"
fi

# A bound, not just an absence: the summarised form is far smaller than -v.
dsz=$(wc -c < "$tmp/err.d")
if [ "$dsz" -lt 600 ]; then
    t_ok "the default stays small ($dsz bytes for 2 tool calls)"
else
    t_fail "default stderr is $dsz bytes -- something is dumping again"
fi

# --- 5: -v can still get the arguments back ----------------------------------
if grep -q 'AAAAAAAAAA' "$tmp/err.v"; then
    t_ok "-v still shows the raw arguments (the M148 debugging affordance)"
else
    t_fail "-v lost the raw arguments -- diagnosing a malformed call needs them"
fi

# --- 6-7: stdout is the answer, in every level -------------------------------
# The pristine failure: stdout began with one '\n' per tool round.
first=$(head_bytes 1 "$tmp/out.d" | od -An -c | tr -d ' ')
if [ "$first" != "\\n" ]; then
    t_ok "stdout starts with the answer, not a blank line"
else
    t_fail "stdout still opens with a newline per tool round: $(od -c "$tmp/out.d" | head -1)"
fi

if [ "$(cat "$tmp/out.d")" = "All done." ]; then
    t_ok "stdout is exactly the answer"
else
    t_fail "stdout carries more than the answer: $(od -c "$tmp/out.d" | head -2)"
fi

# --- 8: the level changes stderr only ----------------------------------------
if cmp -s "$tmp/out.q" "$tmp/out.d" && cmp -s "$tmp/out.d" "$tmp/out.v"; then
    t_ok "stdout is identical at all three levels"
else
    t_fail "verbosity leaked into stdout: q=$(wc -c < "$tmp/out.q") d=$(wc -c < "$tmp/out.d") v=$(wc -c < "$tmp/out.v")"
fi

t_done
