---
description: First look at a dataset — shape, columns, dtypes, missing values, a few rows. Look before you analyze.
---
Take a first, honest look at the dataset before any analysis. Do NOT draw
conclusions yet — the goal is to understand what you are holding.

For the file the user names (or ask which file, and what each column means):

1. Load it and report: number of rows and columns; each column's name, dtype,
   and a plausible meaning (say "unknown — ask" if you cannot tell).
2. Show the first few rows and the summary statistics for numeric columns.
3. Count missing values per column, and flag anything suspicious: impossible
   values, duplicated rows, a date column stored as text, a category with a typo.
4. Write a short `notes/exploration.md` recording what the data is, where it came
   from, and the questions this first look raises.

End by stating: what looks trustworthy, what looks off, and **one clear question**
worth analyzing next. Keep everything in the workspace; do not upload the data.
