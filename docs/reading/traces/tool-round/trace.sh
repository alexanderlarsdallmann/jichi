# tool-round/trace.sh -- the definition of this trace, sourced by ../capture.sh.
#
# One sentence in, two tool calls out, one file changed. The smallest run that
# still goes round the agent loop more than once.

# What the user types. Deliberately vague about HOW: the file to read and the
# edit to make are the model's decisions, which is the point of the trace.
PROMPT="in notes.txt, change 'buy milk' to 'buy oat milk'"

# The run's own upper bound: three round trips, and request 4 finds nothing
# listening. See replies.mm for what each one is answered with.
MAX_REQUESTS=3

# Runs inside the throwaway workspace, before jichi starts. Two lines, and
# 'buy milk' occurs exactly once -- edit_file refuses an ambiguous match, and
# that refusal is chapter 1's third exercise, not this trace.
seed_workspace() {
    printf 'buy milk\nfeed the cat\n' > notes.txt
}

# The invocation, spelled out because the chapter explains it flag by flag.
# --auto approves the tool calls (this is a fixture, not your homework
# directory); --no-session keeps ~/.jichi out of it; --output jsonl is the
# machine surface chapter 1 reads.
run_trace() {
    "$BIN" --config "$CONFIG" --auto --no-session --output jsonl -p "$PROMPT"
}
