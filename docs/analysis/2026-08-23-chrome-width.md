# How wide is jichi's chrome, really? (measured)

*2026-08-23, M554. Stage **A1** of
[`proposals/2026-08-accessibility-by-default.md`](../proposals/2026-08-accessibility-by-default.md).
Every number here is produced by `tests/test_width.c`, which prints this table on every
`make test` — so it cannot go stale without the build saying so.*

## Why measure at all

M549–M553 replaced compact chrome with prose, on the operator's instruction and by ear:

```
[tokens in=4,946 out=37]   ->   4,946 input tokens used, and 37 output tokens used.
```

25 columns became 50. That is the right trade for a listener, and it **spends a budget
nobody had counted.** The budget is *shared*: a chrome line that wraps is worse for both
audiences — some screen readers re-announce a wrapped line, and visual alignment breaks.
So the question was not "is prose nicer" but "does prose still fit, in every language we
ship".

**The instrument is `jc_term_str_cols`, not `strlen`.** It carries the UAX #11 East Asian
Width table, so a Japanese glyph counts two columns. A byte count would have reported
Japanese as ~3× wider than it is and hidden the thing that actually matters: **Japanese
prose is shorter in characters and wider in columns than English.** Bytes answer a
question nobody asked.

## The catalog, in columns

| entry | en | de | es | ja | zh |
|---|---|---|---|---|---|
| `WORKING` | 7 | 7 | 10 | 6 | 6 |
| `ALLOW_PROMPT` | 44 | **66** | 59 | 64 | 52 |
| `ALLOWED` | 7 | 7 | 9 | 12 | 6 |
| `ALLOWED_ALWAYS` | 29 | 33 | 34 | 38 | 22 |
| `ALLOWED_EDITED` | 16 | 20 | 19 | 24 | 16 |
| `DENIED` | 6 | 9 | 8 | 12 | 6 |
| `QUEUED` | 24 | 35 | 30 | 24 | 22 |
| `QUEUE_FULL` | 24 | 35 | 28 | 34 | 20 |
| `QUEUE_UNSENT` | 31 | 35 | 38 | 32 | 25 |
| `QUEUE_DROPPED` | 20 | 29 | 26 | 26 | 16 |
| `ALLOW_PROMPT_ACC` | **75** | 56 | 49 | 54 | 42 |
| **max** | **75** | 66 | 59 | 64 | 52 |

Budget: **78 columns** (80 minus a two-space chrome indent). Warning band from 66.

## Finding 1 — the German folklore was wrong, and low

The design assumed **+20–35%**, which is the figure UI-localisation guidance usually
quotes. Measured on this catalog:

| | de | es | ja | zh |
|---|---|---|---|---|
| worst expansion, **percent** | **+50%** | +42% | **+100%** | +18% |
| worst expansion, **columns** | **+22** | +15 | +20 | +8 |

**+50% on the entry the two languages share** — `ALLOW_PROMPT`, 44 → 66 columns. The
folklore figure would have justified a budget that German overruns.

## Finding 2 — and my first statistic was misleading

The first version of this measurement reported German as **18% shorter** than English. It
divided the per-language *maxima*, which compares **different entries**: German peaks on
`ALLOW_PROMPT` and English peaks on `ALLOW_PROMPT_ACC`, the only entry that received the
M552 "as in" treatment. A ratio of maxima answers *"which language has the widest line"* —
a real question, and not the one being asked.

**The percentage misleads in the other direction too.** Japanese measures **+100%**, which
is `DENIED` going from 6 columns to 12. True, dramatic, and harmless. What decides whether
a line wraps is the **absolute** column count, and there the ranking is different: de +22
and ja +20 on the approval prompt are the expansions that cost budget. The test now prints
both and labels which one wraps a line.

## Finding 3 — three of the ten new chrome lines were over budget

This is what A1 was for. Measured *before* substitution and *after*:

| chrome line | fixed | + substituted | total | verdict |
|---|---|---|---|---|
| `%s input tokens used, %s output tokens used, and %s read from cache.` | 62 | 33 | **95** | **over** |
| `In total this session used %s input tokens, and %s output tokens.` | 61 | 22 | **83** | **over** |
| `In total this session used … at a cost of %.4f dollars.` | **84** | 30 | **114** | **over before a single number was substituted** |

The third is the one worth staring at: **an 84-column fixed part.** It wrapped every
80-column terminal on every session that had a cost to report, and it had shipped in M553
one commit earlier, gated by 12,791 unit checks and 1,413 smoke checks. Nothing in the
tree measured width, so nothing objected.

Fixed by shortening and splitting — two shorter sentences also read better aloud than one
with three clauses:

| now | fixed | + subs | total |
|---|---|---|---|
| `%s input tokens used, and %s output tokens used.` | 44 | 22 | 66 |
| `%s of those input tokens came from cache.` | 39 | 11 | 50 |
| `This session used %s input tokens, and %s output tokens.` | 52 | 22 | 74 |
| `The cost was %.4f dollars.` | 22 | 8 | 30 |

*"In total this session used"* is six words for what *"This session used"* says in three.

## Finding 4 — the English approval prompt was 81 columns

`Allow? Press y as in yes, n as in no, a as in always, e as in edit, v as in view.`
measured **81 columns** and wrapped. Now 75:

```
Allow? Press y for yes, n for no, a as in always, e as in edit, v for view.
```

The cue sits on the two keys that needed it and no others — which is **what the evidence
supported all along.** The operator's report was *"the single vowels a, and e are difficult
to make out"*; `y`, `n` and `v` are consonants that survived. Applying "as in" uniformly
was my choice for tidiness, and tidiness bought a wrapped line. Whether the pattern break
helps a listener — "as in" marking exactly the two hard letters — is a guess, and the next
listening test settles it. The width is not a guess.

## The substitution allowances, stated

A format string's width depends on what fills it, so the budget needs a figure per
placeholder. These are the **widest plausible** values, not typical ones:

| placeholder | allowance | basis |
|---|---|---|
| a token count via `jc_group_num` | 11 | `1,234,567,890` |
| a model display string | 30 | `fast (jlu/qwen3-coder-next)` = 27 |
| a tool name | 24 | `run_terminal_command` = 20 |
| a `%.4f` cost | 8 | `0.1234` = 6 |

## Two lines are outside the budget on purpose

`Calling the tool %s, with %s.` and `The tool %s %s ` are followed by an argument summary
(capped at 200 bytes) or a result body (capped at 360). **Those carry content** — a path, a
command, a file — and content is the channel this project does not reflow, per the
operator's rule that *"within program code all symbols are important, and must be read."*
Their fixed parts are 25 and 11 columns, so the prose added almost nothing to lines that
already wrapped. The exemption is fenced by `tests/test_width.c`'s `nexempt` assertion so it
cannot grow quietly. When this page was written that job belonged to a smoke driver named
chrome_width_lint (check 4), which **M557 retired** — the chrome sentences moved into the
catalog, so there were no copies left to keep in step. *Corrected at M576: this page cited a
deleted driver for twenty milestones, and nothing noticed until a lint was built to look.*

## What this does not measure

- **The prompt line.** `[chat·qwen3-coder-next·2%] ›` is assembled at runtime from the
  mode, model, context percentage and cost, and it is the most-repeated string in a
  session. It is stage A7 and it needs its own arithmetic.
- **Wrapping behaviour itself.** This measures whether a line *fits*; it does not measure
  what a reader does when one does not. That needs a listener.
- **Line-break opportunities inside a long token.** German compounds have none, and
  jichi's breaking is space-based. Stage A5 (UAX #14).
- **Anything about how these widths *sound*.** A line that fits can still be tiring.

## How this stays true

`tests/test_width.c` prints the table and fails the build over a line above 78 columns;
a smoke driver named chrome_width_lint kept the test's copies of the inline chrome in step
with the source, since those strings were not in the catalog and a copy is a lie waiting to
happen. **That driver no longer exists**, for the reason above. Both were perturbed: restoring the 84-column sentence reports
*"84 fixed + 30 substituted = 114 columns"* and reddens the build; padding a catalog entry
past 78 reddens it naming the entry and language; editing a chrome sentence on one side
only reddens the lint.

**When stage A2 moves the inline chrome into the catalog, the copies and that lint both
get deleted** and the catalog loop covers them in all five languages. That is written into
both files as the end condition, because a stopgap without one is just debt.
