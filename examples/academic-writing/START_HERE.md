# Start here — write a stronger paper (that's yours)

*A numbered, first-session walkthrough. Do these steps once on a real assignment.
You will end with a clearer argument, honest citations, and — the real prize —
writing skills you keep. The paper is yours; jichi is your coach, not your ghost.*

## Read this first

This bench **coaches your writing; it does not write your paper.** jichi helps you
plan, strengthen, cite, and polish *your own* work. It will not produce essay text
for you to submit — that would be academic dishonesty, and it would rob you of the
learning that is the entire point. And **you** are responsible for academic
integrity: cite your sources, and **check your institution's rules on using AI**
(they vary — some require disclosure, some restrict it). Read
`skills/academic-integrity` before you go far.

## Before you begin

You need **jichi** (you have it) and **a model** (copy `config.example.json`'s
`models` block into your config; a local one is free and private —
`docs/LOCAL_MODELS.md`; then `jichi doctor`). Have your **assignment brief**
handy — the question, the type (essay / lab report / review), the length, and the
required citation style.

## Your paper, step by step

1. **Open jichi here** (`cd` into this folder, run `jichi`) and start with the
   most important sentence you will write:

   ```
   /outline
   ```

   jichi (the `writing-coach`) will not let you skip the **thesis** — one specific,
   arguable sentence your whole paper defends. Most weak papers have no real
   thesis; nail yours here. Then it helps you outline the argument that supports
   it. It writes `outline.md` — *your* plan.

2. **Now you write.** Draft from your outline — badly is fine, that is what first
   drafts are for. Get your ideas down in your own words. jichi does not write
   this; you do. (A beaten blank page beats a perfect plan.)

3. **Get feedback on YOUR draft.** When you have a draft, ask:

   ```
   /feedback
   ```

   jichi reads what you wrote and tells you where the argument is strong and where
   it is weak — an unsupported claim, a wandering paragraph, a drifting thesis. It
   tells you *what* to fix and *why*; **you** fix it, in your voice. Revise, then
   ask again.

4. **Cite honestly.** As you use sources:

   ```
   /cite
   ```

   jichi flags claims that need a source, helps format references in your required
   style, and makes sure you know: a paraphrase still needs a citation. It keeps a
   running `references.md` for you.

5. **Get a tough read before you submit.**

   ```
   Spawn the argument-reviewer agent to review my draft.
   ```

   A read-only, fair-but-tough pass on your argument — the reader every paper
   needs before a marker sees it. It describes what to strengthen; you strengthen
   it.

6. **Polish last.** Only once the argument is sound:

   ```
   /proofread
   ```

   A clarity, grammar, and flow pass — suggestions that keep *your* voice, not a
   rewrite into someone else's.

That is the loop: **thesis → outline → (you write) → feedback → cite → review →
proofread.** The paper is yours at every step; jichi makes it clearer, better
argued, and honestly sourced — and makes *you* a better writer for the next one.

## If you get stuck

- You cannot state your thesis in one sentence → that is the exercise; keep
  working it with `/outline`. A fuzzy thesis is a paper not yet ready to draft.
- A marker "wants more analysis" → your paragraphs probably state and show but do
  not *explain why* (`skills/argument-structure`, the claim→evidence→analysis
  unit).
- Unsure whether something needs a citation → it almost certainly does; cite it
  (`skills/citation-formats`). When in doubt, cite.
- Unsure whether you may use AI at all → **check your course/university policy**;
  it is your responsibility, and this bench is built to coach, not to write.
