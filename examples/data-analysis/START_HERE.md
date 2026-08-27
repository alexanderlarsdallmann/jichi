# Start here — your first data-analysis session

*A numbered, first-session walkthrough. If you have never done this, do exactly
these steps once; you will have a real, honest result at the end.*

## Before you begin (one-time)

You need three things installed on your computer:

1. **jichi** — you are reading this, so you have it.
2. **Python 3** — check with `python3 --version`. If it is missing, install it
   (python.org, or your system's package manager).
3. **pandas** — the data library. Install it with `pip install pandas matplotlib`.

And a **model for jichi to think with**: copy `config.example.json`'s `models`
block into your config and fill in your model (a local one is free and private —
see `docs/LOCAL_MODELS.md`). Run `jichi doctor` to confirm it is set up.

## Your first analysis, step by step

You need a dataset — a `.csv` file. If you do not have one, any small public CSV
works (a spreadsheet you exported, a dataset from your course). Put it in this
folder, say `data.csv`.

1. **Open jichi here** (`cd` into this folder, run `jichi`). You will see the
   mode-and-model prompt — that is jichi's TUI, waiting for you.

2. **Look at the data first.** Type:

   ```
   /explore data.csv
   ```

   jichi will report the shape, the columns, the missing values, and a few rows —
   and write `notes/exploration.md`. **Read what it found before going on.** This
   is the "look before you analyze" rule, and it is the one beginners skip.

3. **Pick one question.** From what you saw, decide *one* thing you want to know —
   phrased so it could be answered "no". Tell jichi in plain words, e.g. "Does
   column A differ between the groups in column B?"

4. **Clean, if needed.** If `/explore` flagged messy data (wrong types, missing
   values), type `/clean` and let jichi walk you through fixing it — in a script,
   with every decision written down.

5. **Analyze.** Type `/analyze` and state your question. jichi will start with the
   boring, checkable number and build up only as far as the question needs —
   always telling you the sample size and the caveats.

6. **Have it checked.** Before you believe the result, ask the reviewer:

   ```
   Spawn the methods-reviewer agent to review my analysis.
   ```

   It reads (never edits) and reports any method mistake. A clean review is worth
   more than a fast one.

7. **Report it.** Type `/report`. jichi writes a short `REPORT.md` — the question,
   the data, what you found (with the caveats in the sentence), what you cannot
   say, and how to reproduce it.

That is the whole loop: **look → question → clean → analyze → check → report.**
Every real analysis is that loop, done honestly. Do it once here; then do it on
something you actually care about.

## If you get stuck

- `ModuleNotFoundError` → a Python library is missing; `pip install` it.
- jichi runs a command you do not understand → do not approve it; ask "what does
  this command do?" first. You are the analyst; jichi is the instrument.
- The result seems too good → it probably is. Run the `methods-reviewer`, and
  read `skills/honest-charts` and `skills/reproducible-notebook`.
