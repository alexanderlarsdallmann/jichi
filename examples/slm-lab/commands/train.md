---
description: Start one training run in the background, with its command line and seed written down first.
---
Run one training run of `charlm`, so that it can be reproduced and compared.

1. Agree on **one** change from the previous run (or "the defaults" for the first),
   and the question it answers.
2. Write the exact command line, with `--seed`, into `notes/runs.md` **before** it
   runs, under a heading that names the change.
3. Start it in the background with its output going to a file, for example
   `./charlm --seed 1 corpus.txt > runs/run-01.txt`. Poll it rather than waiting.
   A run of a few thousand steps takes seconds to minutes on a CPU.
4. When it ends, copy its `baselines`, the last `step` line and the `result` line
   into the notes under the command.

Do not interpret the numbers here. That is `/evaluate`.
