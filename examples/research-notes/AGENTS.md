# Research-notes project conventions

*This file tells the agent how to behave in a research-reading workspace. jichi
loads it automatically. Keep it short; edit it to match your project.*

## What this bench is

A bench for **reading research critically and organizing it** — a literature
review, a reading log, annotated sources, a synthesis. The reading, understanding,
and judgment are yours; jichi helps you do all three more deliberately, in plain
files you own. It does not read the papers for you or hand you conclusions to
copy — it makes your reading sharper and your notes honest.

Pairs with jichi's source-reading guides (`docs/reading/ANNAI.md`,
`docs/reading/FUKABORI.md`) for reading *code/sources*, and with the
`academic-writing` bench when your notes become a paper.

## The two habits that matter most

1. **Read critically, not passively.** For every source, go past "what does it
   say?" to "should I believe it, and how does it fit?" — the question, the method,
   the findings, the limitations, and *your* assessment. Highlighting is not
   reading. (`skills/critical-reading`)
2. **Keep source ideas separate from your own — always.** In every note, mark what
   is a **quote** (exact words), what is a **paraphrase** (the source's idea, your
   words — still theirs, still needs a citation), and what is **your** thought.
   Attach the full citation to every note. Muddled notes are how honest students
   accidentally plagiarize months later; this labeling prevents it.

## From reading to a review

1. **Annotate** each source actively as you read.
2. **Summarize** each with a structured, critical note (including your assessment).
3. **Organize** with a **literature matrix** — sources by themes — so patterns,
   agreements, disagreements, and the *gap* become visible.
4. **Synthesize** by *idea*, not by source — show the conversation between sources
   and build toward the gap. Never a serial "Author A said, Author B said".

## The workflow (commands)

- `/annotate` — active, honestly-labeled notes on one source.
- `/summarize-paper` — a structured, critical summary (with your assessment).
- `/lit-matrix` — build the sources-by-themes matrix; find the patterns and gap.
- `/synthesize` — turn the matrix into a synthesis organized by idea.

Use the read-only **`critical-reading-reviewer`** agent to check your notes and
synthesis — it flags an uncritical summary, notes that blur source and self, a
matrix that is just a list, and a "synthesis" that is really a serial summary.
