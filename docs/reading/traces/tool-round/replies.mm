# tool-round/replies.mm -- the reply table for tests/tools/mockmodel.
#
# Format: tests/tools/mm_core.h documents it in full. First matching rule
# wins; `count N` matches the Nth request of the run (1-based).
#
# This file is the model. Everything the "model" decides in this trace was
# decided here, in advance -- which is exactly why the trace is reproducible,
# and exactly what a real model does not give you.
wire openai

# Request 1: the user's sentence arrives with the tool menu. Ask to read the
# file before touching it.
rule
  count 1
  tool read_file {"path":"notes.txt"}

# Request 2: the read result is now in the conversation. Order the edit. The
# argument names are the ones edit_file's schema declares -- old_string, not
# old (src/tools/jc_tool_edit.c:edit_schema). Getting that wrong is a real
# run's most common tool failure, and it is chapter 4's trace.
rule
  count 2
  tool edit_file {"path":"notes.txt","old_string":"buy milk","new_string":"buy oat milk"}

# Request 3: the diff came back. Say what was done, in one line.
rule
  count 3
  text Changed 'buy milk' to 'buy oat milk' in notes.txt.

# Anything else is a bug in this fixture, and says so out loud rather than
# being quietly answered. (With MAX_REQUESTS=3 the mock is gone by then; this
# rule is what fires if someone raises that bound.)
rule
  status 500
  body {"error":"unexpected request -- the fixture expects exactly 3"}
