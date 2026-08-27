# Start here — read a body of research, and actually understand it

*A numbered, first-session walkthrough. Do these steps as you work through a small
set of sources (three or four is plenty to learn the loop). You will end with a
literature matrix, a synthesis, and — the real prize — the habits of a critical,
honest reader.*

## Read this first

This bench helps you **read and organize research; it does not read for you.** The
understanding and judgment are yours — jichi makes your reading sharper and your
notes honest. And the most important habit here is an integrity one: **always keep
the source's ideas separate from your own in your notes**, and keep a citation on
every note. That is what lets you write honestly later. Read
`skills/critical-reading` early.

## Before you begin

You need **jichi** (you have it), **a model** (copy `config.example.json`'s
`models` block into your config; a local one is free and private —
`docs/LOCAL_MODELS.md`; then `jichi doctor`), and **a few sources** you have access
to and will actually read (papers, chapters, articles on one topic).

## Your first literature review, step by step

1. **Open jichi here** (`cd` into this folder, run `jichi`).

2. **Read one source actively.** Read the first source yourself, and as you go:

   ```
   /annotate
   ```

   jichi helps you capture questions, doubts, and connections — and, crucially,
   **label** what is a quote, what is a paraphrase of the source, and what is your
   own thought. A margin full of *your* questions is a good sign. (`skills/critical-reading`
   has how to read a paper well.)

3. **Summarize it critically.** When you finish the source:

   ```
   /summarize-paper
   ```

   A structured summary — question, method, findings, limitations, and **your
   assessment**. That last part (does it convince you? how does it fit?) is the one
   that matters; a summary without it is a book report.

4. **Repeat for your other sources** (steps 2–3). Read them, annotate, summarize.
   Do not skip the reading — the notes are only as good as the reading behind them.

5. **Organize into a matrix.** Once you have a few summaries:

   ```
   /lit-matrix
   ```

   jichi helps you build a **literature matrix** — sources down, themes across — and
   then read it *with* you to find the patterns: where sources agree, where they
   disagree, and the **gap** nobody has addressed. Seeing the gap is the whole point.

6. **Synthesize by idea.** Turn the matrix into a written synthesis:

   ```
   /synthesize
   ```

   Organized by *theme*, showing the conversation between sources — NOT a serial
   "Author A said, Author B said". This is the #1 skill and the #1 mistake; jichi
   keeps you on the synthesis side.

7. **Get a critical read.**

   ```
   Spawn the critical-reading-reviewer agent to review my notes and synthesis.
   ```

   A read-only pass that flags an uncritical summary, notes that blur source and
   self, or a synthesis that slipped back into a list.

That is the loop: **annotate → summarize → matrix → synthesize → review.** When you
are ready to write it into a full paper, switch to the `academic-writing` bench.
Your reading, your judgment, your honest notes — jichi just makes them sharper.

## If you get stuck

- Your notes feel like they just restate the paper → you are reading passively; ask
  the critical questions (`skills/critical-reading`) and record *your* reactions.
- You cannot tell, later, which idea was whose → your notes did not label source vs
  self; fix that habit now (`/annotate`), it prevents accidental plagiarism.
- Your review reads as a list of papers → it is a serial summary, not a synthesis;
  reorganize by theme (`skills/synthesis-not-summary`, `/synthesize`).
- You cannot find a "gap" → read *down the columns* of your matrix, not across the
  rows; the empty cells are the gaps (`skills/literature-matrix`).
