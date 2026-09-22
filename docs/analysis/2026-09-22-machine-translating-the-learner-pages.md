# Four pipelines against one local translator, and the page that was dropped

*2026-09-22. `llm-jp-4-8b-thinking` on a local LM Studio, against two
learner-facing English pages. Follow-up to
[`2026-09-17-local-japanese-translation.md`](2026-09-17-local-japanese-translation.md),
which measured five models on one paragraph and concluded "do not bulk-translate
this documentation with a local model". This measures the same model on whole
pages, with four different pipelines, and reaches the same verdict from the other
direction: the best pipeline still leaves a quarter to a third of a page in
English, and no amount of retrying moves it.*

## The result, in one table

One instrument for every row: a body line counts as **untranslated** when, outside
fenced code and outside the English banner comments, it is over 12 characters,
contains a 4+ letter English word, and contains no CJK character at all. Table
rows, blockquotes, images and mermaid lines are excluded; **headings and list
items count, because they are content.**

| pipeline | `VOCABULARY.md` | `TUTORIAL_BEGINNER.md` |
|---|--:|--:|
| A — 2,200-char chunks, one attempt | 22 | **24** |
| B — per-unit, per-line prompts | 108 | — |
| C — per-unit, continuation lines joined | 50 | — |
| D — 2,200-char chunks, 6 retries, validated | **18** | 44 |

**The best result per page came from different pipelines**, which is itself worth
knowing: more retries helped the glossary and hurt the tutorial. Final state:
18 of 77 body lines (**23%**) and 51 of 168 (**30%**) still English.

*(The tutorial's 24 and 51 are the same file measured before and after the
instrument was fixed — see "the count was wrong three times" below.)*

## What each pipeline got wrong

**A — whole chunks.** Entire definition lists came back in English while the
structural checks passed, because **heading count matched**: the headings were
present, just untranslated. A shape check cannot see that the shape is full of
the wrong language.

**B — per line, no format constraint.** Catastrophic in a specific way: given a
list item alone, the model *expanded* it. `- **rollback** — restoring the last
green checkpoint…` came back as a `## rollback` heading with an invented
`**概要**` section, and `## Learning with jichi` came back with the placeholder
**`（ここに本文が入ります）`** — "body text goes here". A small unit with no format
constraint invites the model to write a document.

**C — per unit, with format constraints.** Much better behaved, and it introduced
a subtler failure: my validator accepted any output containing **one** Japanese
character, so a mostly-English paragraph with a single translated word passed. It
reported 11 failures where the audit found 108. *A floor of zero cannot validate
— and neither can a floor of one.*

**D — chunks plus real validation.** The best for the glossary, and it showed the
limit clearly: three chunks failed **six attempts each**, eighteen requests, every
one leaving English behind. That is a property of the model on this content, not
a bad roll.

## What survives is not random

The glossary's 18 lines are essentially **two whole sections** — "Who may do what"
(posture, verdict, fence, reference root, privileged gate, kinetic) and "Learning
with jichi" (assignment, hint ladder, prediction, grade, tier, tutor stance, gate,
record). Both are dense runs of `- **term** — long definition with code spans and
em dashes`. Every pipeline failed on those two and no other.

The tutorial's 51 lines are the opposite shape: spread across **all eleven
sections**, every section holed.

## Why one page shipped and the other did not

`docs/i18n/ja/VOCABULARY.md` is committed. `TUTORIAL_BEGINNER.md` was drafted,
measured, and deleted.

The percentages are close (23% vs 30%) and are not the reason. **The distribution
is.** A glossary missing two named sections is still a usable reference for the
other six, and a reviewer can be handed a task with edges: *translate these two
sections*. A sequential tutorial that drops into English every third line, in
every section, is worse for a learner than reading the English original — and
worse to hand a reviewer than nothing, because "check the whole thing" is not a
task, it is the original job.

`docs/i18n/README.md`'s own policy is the test this had to pass: *"a partial
language is better than none and much better than four unreviewed pages presented
as complete — but a reader must be able to tell which they are looking at."* One
page can say what it is missing. The other could only say "much of this".

## Two things that did work, and should be kept

**Fenced code blocks must never reach the model.** The first tutorial run
collapsed ``` blocks into inline spans, producing `` `sh   jichi setup   ` ``
where the English had a copyable command, and **fabricated a link target**
(`docs/AUTOCOMPLETE.md`, which appears nowhere in the English). Lifting the
blocks out, substituting sentinels and restoring them afterwards fixed it: 0 of
14 sentinels lost on the re-run, and neither the fabricated link nor a lost
heading recurred. **A command a learner copies is not a thing to
machine-translate.**

**Verify with the gate's own extraction, not a lookalike.** The first checker
reported a fabricated figure `843`. There was none: the English says `~1,843` and
so does the Japanese. Python's `\b` finds no word boundary between the CJK
character before a number and the digit, so it matched `843` out of an intact
`1,843`. `i18n_tracks_lint` uses
`grep -oE '[0-9]+([.,][0-9]{3})+|[0-9]{3,}' | tr -d '.,'`, which has no such
problem. The rule this project already has — *verify a gate's pattern with the
gate's own tool* — applies to a **proxy for a gate** exactly as much as to the
gate.

## The count was wrong three times, which is the lesson worth keeping

The untranslated-line figure for `TUTORIAL_BEGINNER.md` was published as **6**,
then **24**, then **51** — the same file, three filters. Six came from a check
that only looked at lines over 45 characters, so every short lead-in (*"In your
project directory:"*, *"Answer the prompts:"*) was invisible. Twenty-four came
from a filter that excluded lines starting with `-` or `#`, which silently
excused untranslated list items and headings. Fifty-one is the number under the
rule stated at the top of this page.

None of those filters was written dishonestly and all three were wrong. **An
instrument that is not written down is re-invented slightly differently every
time it is used**, and the drift is always in the flattering direction, because a
filter is tightened when its output looks too alarming and never when it looks
reassuring. The rule is now a docstring in the script rather than a regex in a
shell pipeline.

## What is NOT established

- **That a better local model cannot do this.** One model, chosen because the
  earlier measurement ranked it first of five. A larger Japanese model, or one
  with a longer context than this one's 8,192 tokens, was not tried.
- **That the two resistant sections are resistant for the reason suggested.**
  Density of `- **term** —` items correlates with failure across four pipelines;
  nothing here isolates it from length, em-dash count or code-span count.
- **Quality of what *was* translated.** Everything above measures *whether*
  Japanese is present, which is checkable without Japanese. Whether it is *right*
  — the 監督下で / 監視下で distinction the earlier page turned on — still needs a
  native reviewer, and the banner on the shipped page says so.
