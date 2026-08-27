---
title: The confident misdiagnosis
audience: student
phase: implementation
difficulty: advanced
points: 4
verify: "sh docs/assignments/15-the-confident-misdiagnosis/test.sh"
hints:
  - Before judging the diagnosis, reproduce the symptom yourself -- then try an input the diagnosis never mentions. An empty line. Two spaces in a row.
  - The proposed patch treats one symptom. List every input the CURRENT code gets wrong; does the patch explain all of them, or only the one in the bug report?
  - Counting separators and counting words are different things whenever separators repeat or the string is empty. The real fix tracks whether you are INSIDE a word; the subtraction only hides one edge of the wrong model.
---
The program in `docs/assignments/15-the-confident-misdiagnosis/` counts the
words in a line. It has a bug — and it also has `DIAGNOSIS.md`: a confident,
fluent, well-formatted analysis (the kind an agent produces in seconds)
naming the root cause and proposing a one-line patch.

> **Prerequisite: a C compiler (`cc`).** This task's grader compiles C. On Debian/Ubuntu: `sudo apt install build-essential`; on macOS: `xcode-select --install`. Without it the failure can read like a wrong answer rather than a missing tool.

**The diagnosis is wrong.** Not obviously wrong — *plausibly* wrong: the
patch really does fix the reported symptom. This is the failure mode nobody
teaches (this project's own record of it: ANECDOTES #19 — blamed the model,
was the request; #21 — every obvious reading was about the model, the cause
was a tool default). Fluency is not evidence. Your grade is **detection**.

Deliverables:

1. **Your verdict**, in
   `docs/assignments/15-the-confident-misdiagnosis/VERDICT.md`:

   ```markdown
   ## Claim
   <what DIAGNOSIS.md asserts, in one sentence>
   ## Evidence
   <the experiment(s) YOU ran — the commands/inputs and what they showed>
   ## Verdict
   <accepted or rejected, and the actual root cause>
   ```

2. **The real fix** in `wc_words.c`. The tests (`test_wc.c` — leave it
   exactly as it is) cover the reported symptom *and* the inputs the patch
   quietly breaks or ignores; the suite passes only for a fix that corrects
   the model of the problem, not the symptom.

Apply the suggested patch first if you like — watching a plausible patch
fail is worth more than being told it would. The agent that wrote the
diagnosis is not your enemy; your unverified trust would be. Check the
claim the way Module 4 taught you to check a comment.

Grade with `jichi grade docs/assignments/15-the-confident-misdiagnosis.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/15-the-confident-misdiagnosis.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/15-the-confident-misdiagnosis.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
