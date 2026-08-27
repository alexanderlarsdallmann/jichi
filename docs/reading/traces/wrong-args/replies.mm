# wrong-args/replies.mm -- the reply table for tests/tools/mockmodel.
#
# THIS FIXTURE IS WRONG ON PURPOSE, and it was wrong by accident first: it is
# the first draft of tool-round/replies.mm, kept because of what the run did.
# The edit call names `old` and `new`; edit_file's schema declares `path`,
# `old_string` and `new_string` (src/tools/jc_tool_edit.c:edit_schema).
wire openai

# Request 1: read the file first. This matters -- it satisfies the
# read-before-edit guard, so the failure below is ONLY the argument names and
# not two faults at once.
rule
  count 1
  tool read_file {"path":"notes.txt"}

# Request 2: the edit, with the wrong field names.
rule
  count 2
  tool edit_file {"path":"notes.txt","old":"milk","new":"oat milk"}

# Request 3: the model has now seen the tool's error, and reports success
# anyway -- which is what a confident model does with a tool result it did not
# read. Nothing here checks the claim, which is the chapter's whole subject.
rule
  count 3
  text Done -- notes.txt now says oat milk.

rule
  status 500
  body {"error":"unexpected request -- this trace expects exactly 3"}
