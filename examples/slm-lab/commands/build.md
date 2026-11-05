---
description: Compile charlm with strict warnings, and read with the learner what it will do before the first run.
---
Build the lab's program and make sure the learner knows what it does.

1. Compile it with strict warnings, so a mistake in an edit shows up as a warning
   rather than as a strange loss:
   `cc -std=c89 -pedantic -Wall -Wextra -O2 -o charlm charlm.c -lm`.
   Zero warnings is the bar. If there is one, read it with the learner and fix it
   before going on.
2. Read the header comment of `charlm.c` together: what it holds out, which
   baselines it prints, and what it does not do (a character is a byte; no model
   file).
3. Ask which text they will train on, and whether they may use it. Their own
   writing, a public-domain book, or this project's documentation are all fine.
   Note the file and its size in `notes/runs.md`.

End by saying what the first run will print, line by line, so they can read it
when it arrives.
