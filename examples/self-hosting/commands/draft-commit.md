---
description: Draft a house-style commit message from the current diff (delegates to the committer agent; read-only, does not commit).
agent: committer
---
Draft a conventional-commit message for the current change: read the diff and
status, warn if the branch is master/main, and output the message in a fenced
block plus the `git commit -F -` command to apply it. Do not commit.

$ARGUMENTS
