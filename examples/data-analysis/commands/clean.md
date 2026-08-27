---
description: Clean a dataset explicitly — every decision written down in code and in a note, nothing silent.
---
Clean the dataset for analysis, making every decision **explicit and reversible**.
Silent cleaning is how wrong conclusions are born.

Work in a single, top-to-bottom script (e.g. `clean.py`) that reads the raw file
and writes a cleaned copy — never edit the raw data in place. For each step:

1. State the problem you are fixing (missing values, wrong dtype, duplicates,
   outliers, inconsistent categories) and how many rows/values it affects.
2. Make the fix in code, with a comment saying *why* — and, crucially, whether
   the fix could change the eventual answer.
3. Record it in `notes/cleaning.md` as a short list a reader can audit.

Rules: keep the raw file untouched; do not drop rows or fill values without
writing down the count and the reason; if removing outliers changes the result,
report the answer both ways. Ask before any decision that could bias the outcome.
