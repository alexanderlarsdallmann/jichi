# plain-turn/trace.sh -- the definition of this trace, sourced by ../capture.sh.
#
# A question, an answer, and nothing else: no tool call, so the agent loop
# runs exactly once. The shortest path there is through the machinery, and the
# control against which chapter 1's tool round is read.

# A question about C, not about the workspace -- so no file needs opening for
# it to be answerable, and the model choosing not to look is not negligence.
PROMPT="in one sentence, what is a translation unit in C89?"

# One round trip, and request 2 finds nothing listening. That bound is an
# assertion: this run must not need a second call.
MAX_REQUESTS=1

# The same workspace chapter 1 starts from, deliberately. workspace.after is
# then a claim with teeth: identical bytes mean nothing on disk was touched.
seed_workspace() {
    printf 'buy milk\nfeed the cat\n' > notes.txt
}

# Identical to chapter 1's invocation, flag for flag. The only difference
# between the two traces is what the model answers.
run_trace() {
    "$BIN" --config "$CONFIG" --auto --no-session --output jsonl -p "$PROMPT"
}
