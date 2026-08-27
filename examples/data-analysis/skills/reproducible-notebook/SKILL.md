---
name: reproducible-notebook
description: How to structure an analysis so anyone (including future you) can re-run it top-to-bottom to the same result.
---
# A reproducible analysis

Reproducibility is not a nicety — it is what separates a result from a guess. If
you cannot re-run it from scratch and get the same answer, you do not actually
know what you found.

**The structure:**

- **One linear pipeline.** Raw data in, cleaned data out, analysis on the cleaned
  data, report at the end — each a separate step that reads the previous step's
  output. No step depends on something typed by hand and forgotten.
- **Never edit the raw data.** Treat it as read-only; all changes happen in code
  that writes a new file.
- **Set a random seed** wherever anything is random (sampling, shuffling, model
  init), so the same run gives the same numbers.
- **In a notebook, run top-to-bottom before you trust it.** Out-of-order cells
  are the #1 source of "it worked yesterday." Restart-and-run-all is the honesty
  check. Better yet, keep the load/clean/analyze steps in plain `.py` scripts the
  notebook calls, so the pipeline runs without a notebook at all.
- **Pin your inputs.** Note the exact data file (and its version/date) and the
  library versions. A result that only reproduces with one mystery version is
  fragile.
- **Write the re-run commands down** in the report, so a stranger can follow them.

The gold standard: delete every output, run the pipeline from the raw file, and
get byte-identical results. If you can do that, the finding is real.
