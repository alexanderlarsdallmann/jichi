---
title: Delegate with a leash
audience: student
phase: implementation
difficulty: advanced
points: 3
verify: "sh docs/assignments/13-delegate-with-a-leash/test.sh"
hints:
  - You are not solving the inner task -- the agent is. Your work is the ENVELOPE around it. Read the module page's anatomy of the one-liner before running anything.
  - The run's verify is the inner task's own check; the edit scope is the work/ directory; the journal path is the one this assignment's grader reads. Assemble those three and the rest is flags.
  - "From the bench root: jichi -p \"<the inner task, stated precisely>\" --auto --edit-scope 'docs/assignments/13-delegate-with-a-leash/work/**' --budget-tokens 200k --verify \"sh docs/assignments/13-delegate-with-a-leash/work/check.sh\" --journal docs/assignments/13-delegate-with-a-leash/journal.jsonl -- then read the journal before you grade."
---
Everything so far you supervised turn by turn. This task is **delegation**:
one bounded, unattended `--auto` run — and what is graded is not the work
product alone but the **evidence** that the run stayed inside its bounds.
The journal is the provenance.

**The inner task** (deliberately small — the envelope is the exercise): have
the agent create `docs/assignments/13-delegate-with-a-leash/work/report.txt`
containing exactly the line

```
delegation with verification
```

`work/check.sh` is the inner task's own verify; it exists already — read it.

**Your work** is the envelope around that task. Run jichi headless with:

- the inner task as the prompt, stated precisely;
- `--auto` (unattended) with an **edit scope** confining writes to the
  task's `work/` directory;
- a **token budget** (200k is plenty);
- `--verify` pointing at `work/check.sh`, so the run cannot claim success
  without passing the inner check;
- `--journal docs/assignments/13-delegate-with-a-leash/journal.jsonl` — the
  grader reads this exact path.

Then — before you grade — **read the journal yourself** (`jichi runs` can
summarize it). Find the `start` line, the `verify` line with `"exit":0`, and
the `end` line with its outcome and token count. Reading run evidence is the
skill this module exists to teach; the grader only checks that the evidence
says what a bounded, verified, in-scope run says:

1. `work/report.txt` is correct (the inner task succeeded),
2. the journal exists and records a `start` and an `end` with
   `"outcome":"ok"` and no rollback,
3. at least one `verify` event passed (`"exit":0`),
4. **no `out_of_scope` events** — the run touched nothing outside its fence.

A journal is a text file, and yes — you could forge one. You would be
practising exactly the wrong skill (Stage 3's Module 9 is about *catching*
that move), and in any taught setting the counter is live provenance: rerun
it in front of someone. Alone, you are on your honor, as always.

Grade with `jichi grade docs/assignments/13-delegate-with-a-leash.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/13-delegate-with-a-leash.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/13-delegate-with-a-leash.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
