# Data-analysis project conventions

*This file tells the agent how to behave in a data-analysis workspace. jichi
loads it automatically. Keep it short; edit it to match your project.*

## What runs where (read this first)

jichi does **not** run your analysis itself. It writes and edits scripts and
notes in this workspace, and it runs them for you through the shell — but the
analysis actually happens in **your Python environment**, which must have
`python3` and the libraries you use (`pandas`, `matplotlib`, …) installed. If a
command fails with `ModuleNotFoundError`, that is your environment, not jichi:
install the library (`pip install pandas`) and try again. jichi will tell you
which command it is about to run — read it before you approve.

Your **data stays local.** The raw files live in this workspace; nothing is
uploaded anywhere. Only the text of your prompts and the code/notes jichi reads
go to the model, the same as any jichi session — so do not paste private rows
into the chat if the dataset is sensitive; point the agent at the file instead.

## The method, always

1. **Question before chart.** State a testable question and what answer would
   mean "no" *before* looking for a result.
2. **Look before you analyze.** Shape, dtypes, missing values, a few rows.
3. **Clean explicitly.** Every dropped row or filled value is written down in
   code and in a note. Never edit the raw data in place.
4. **Simplest sound method.** A mean or a group-by before a model; a model only
   if the summary needs it. State the sample size next to every statistic.
5. **Reproducible.** A linear pipeline, a random seed, a notebook that runs
   top-to-bottom. If it cannot be re-run to the same result, it is not a result.
6. **Honest.** The caveat goes in the sentence, not a footnote. "The data cannot
   tell us" is a valid, respectable finding.

## The workflow (commands)

- `/explore` — first honest look at a dataset.
- `/clean` — explicit, written-down cleaning into a new file.
- `/analyze` — answer one question with the simplest sound method.
- `/report` — a short, honest, reproducible write-up.

Before you trust a result, have the read-only **`methods-reviewer`** agent read
the analysis — it hunts for the classic mistakes (a claim bigger than the data,
p-hacking, a misleading chart, non-reproducibility).
