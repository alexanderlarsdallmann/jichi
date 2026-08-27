# wrong-args/trace.sh -- the definition of this trace, sourced by ../capture.sh.
#
# The same sentence and the same workspace as tool-round. One difference, in
# replies.mm: the model calls edit_file with the argument names a person would
# guess. Everything the two traces do differently follows from that one field.

PROMPT="in notes.txt, change 'buy milk' to 'buy oat milk'"

MAX_REQUESTS=3

seed_workspace() {
    printf 'buy milk\nfeed the cat\n' > notes.txt
}

run_trace() {
    "$BIN" --config "$CONFIG" --auto --no-session --output jsonl -p "$PROMPT"
}
