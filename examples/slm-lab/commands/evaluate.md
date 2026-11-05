---
description: Read a run's losses against the baselines and against the previous run, and say what the numbers do and do not show.
---
Read one run's output honestly.

1. Quote the **validation** loss of the last step, the **bigram** baseline, and the
   difference between them. The training loss is not the result.
2. Read the curve: do both losses fall? If training loss keeps falling while
   validation loss rises, that is overfitting. Say at which step it began, and that
   the best model was the one before it.
3. If there is a previous run with one change, compare the two validation losses
   and name the change. A difference smaller than the run-to-run spread is no
   difference. If only one seed was run, say that the spread is unknown.
4. Say what was not evaluated: one text, one held-out slice, one seed.

End with one honest sentence, such as "a longer context lowered validation loss by
0.08 on this text, one seed", and write it into `notes/runs.md`.
