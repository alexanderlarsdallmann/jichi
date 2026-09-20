# Driving jichi on somebody else's project

*The counterpart to [`PLATFORM_TESTING.md`](PLATFORM_TESTING.md). That page
answers **"does it work on this machine?"**. This one answers **"does it work
when somebody uses it?"** — and in this project's experience those have almost
disjoint failure sets.*

---

## 0. Why this page exists, with the arithmetic

Every gate in this tree is **offline**. `make ci` builds with two compilers,
runs sanitizers, valgrind, 315 smoke drivers and the e2e tier — and not one of
them calls a model. That is a deliberate property (it is why the tier runs on a
96 MB machine with no distro), and it has a cost: **the gates cannot see what a
user is told.**

Measured, 2026-09-20/21. Eight headless runs against one foreign repository
produced **four** defects, none of which any gate could have found:

| found by driving | why no gate could see it |
|---|---|
| a capped run reported success on every channel a caller reads (M687) | needs a model that loops; the tier's mock always terminates |
| `doctor` blessed a model id the server does not list (M689) | needs a real server with a real listing |
| the envelope could not tell a reading shell command from a writing one (M689) | needs a run that shells out for real work |
| the live probe discards the server's own HTTP diagnosis | needs a server that refuses in a specific way |

Two of those four had been **written down years-equivalent earlier** in this
project's own registers and left standing. Driving is what turned a note into a
defect somebody had to fix.

**This is not a new tier.** It is a recipe and a log. The artifact is a row
somebody else can read, exactly as in platform testing.

---

## 1. The recipe

### 1. Pick a real task on a real second project

Not a toy, and not this repository. The point is configuration nobody checked,
written months ago, against a server that has since changed. A task with a
**checkable answer** is worth twice one without — you need to be able to grade
the run, and "it looked plausible" is not a grade.

Good shapes: *"which of these branches are merged?"*, *"where is X implemented?"*,
*"add a doc comment to Y"*. Each has a right answer you can get another way.

### 2. Get the ground truth **first**

Before you drive, answer the question yourself and write the answer down.

This is the step people skip, and skipping it converts the whole exercise into
theatre: without ground truth you cannot tell a good answer from a confident
one. In the 2026-09-20 session the model's *second* attempt at a branch
question answered in four seconds and dropped **eleven of twelve** items — and
it read as a clean, fast success.

### 3. Fence it, and cap it

```sh
jichi --config <cfg> -p "<task>" \
      --no-session --edit-scope '<narrow glob>' --max-tool-calls 25
```

- `--edit-scope` bounds the blast radius. Use `NOTHING_MAY_BE_WRITTEN` for a
  read-only task; it is a glob that matches nothing, which is the point.
- `--max-tool-calls` is a **fence, not a budget for measurement**: a model that
  loops otherwise costs millions of tokens and returns nothing. In the session
  above, an unfenced run burned **4,887,733 tokens** producing no answer.
- Keep `--no-session` unless the task needs history. A one-shot is the shape a
  script uses, and the shape whose failures are least visible.

### 4. Record what the caller was told — all of it

Not just the answer. The whole point is the report:

```
answer     : <stdout, verbatim, or "(empty)">
exit code  : <$?>
footer     : <both lines of [jichi] checked:/not checked:>
verdict    : <the [envelope] line>
stop reason: <--output json's stop_reason, if you ran json>
```

**The empty answer is a result, not a failed run.** M687 exists because one was
discarded as "the run didn't work".

### 5. Grade it against step 2, and say which kind of wrong

Three different failures, and conflating them wastes the run:

- **wrong answer** — the model's problem;
- **right answer, badly reported** — jichi's problem, and the richest seam;
- **right answer, but nothing said it was right** — the gates' problem.

---

## 2. What to watch for, from the ones already found

- **An empty answer with exit 0.** Always ask what the footer said.
- **A `✓` that is answering a narrower question than it appears to.**
  `✓ model server reachable` is about a URL, not a model (M689).
- **A warning that fires on something harmless.** It will train you to skip the
  line it is attached to (M689's shell attribution).
- **A number that is suspiciously round**, or a count that matches the cap
  exactly. 200 of 200 tool calls is not a coincidence.
- **The run's own report contradicting itself.** `verify did not conclude` and
  `not checked: (nothing)` appeared four words apart (M688).

---

## 3. The log

One row per drive. This is the record that the work happened, and the corpus
three deferred questions are waiting on —
[`tests/measure/capped_oneshot.py`](../tests/measure/capped_oneshot.py) and
[`tests/measure/strict_green_fp.py`](../tests/measure/strict_green_fp.py) both
read the run journals these produce, and both print **NOT EVIDENCE** until
there are enough.

| Date | Project | Task | Model | Result | What it found |
|---|---|---|---|---|---|
| 2026-09-20 | zigodot | which branches are merged into master | `jlu/qwen3-coder-next` | **no answer**, 200 tool calls, 4,887,733 tokens, exit 0 | M687: the cap is invisible on every channel a caller reads |
| 2026-09-20 | zigodot | same, fenced to 25 tool calls | `jlu/qwen3-coder-next` | 2 calls, 4 s — **dropped 11 of 12 items** | the fast model is wrong for list questions; graded only because ground truth existed |
| 2026-09-20 | zigodot | same | `jlu/qwen3.8-27b` | **exactly right**, 1 call, 15 s | pick the thinking model when the answer is a complete list |
| 2026-09-20 | zigodot | read the upstream Godot reference | `jlu/qwen3-coder-next` | honest *"that directory does not exist"* | a negative result handled well — worth logging too |
| 2026-09-20 | zigodot | add a doc comment, out of scope | `jlu/qwen3-coder-next` | refused by the fence, said why | the fence is enforced **and** legible |
| | | | | | |

---

## 4. Where to go next

- [`PLATFORM_TESTING.md`](PLATFORM_TESTING.md) — the other half: does it work
  on this machine.
- [`AUTONOMY.md`](AUTONOMY.md) — what the envelope bounds and what it does not.
- [`ANECDOTES.md`](ANECDOTES.md) #87, #88 — two of the rows above, written up.
