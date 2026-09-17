---
title: Read the turn — five readings of one request
audience: student
phase: review
stage: extras
difficulty: advanced
points: 4
verify: "sh docs/assignments/74-read-the-turn/test.sh"
hints:
  - Start at the abstraction, not the loop. `include/jc_provider.h` declares `build_request` as a function POINTER -- so the question "what runs?" has as many answers as there are providers. Find every struct that fills that slot before you follow any one of them.
  - For data flow, pick ONE value and refuse to let go of it. The request body is born in `build_request`, handed to jc_http with `req.stream_body = 1`, and freed inside a libcurl read-callback -- by whom, and when exactly? The comment above the call names the reason it cannot be reused across retries.
  - "For Execution, do not reason about what the loop WOULD do -- open `docs/reading/traces/tool-round/expected/stdout.jsonl` and match each event to the function that emitted it. Then ask the review question about your own reading: which sentence of it would a recorded run contradict?"
---

This is the reading track's *review* rung, one above
[task 24](24-read-a-real-project.md): there you read a stranger's small
library to convict one function; here you read **jichi itself** — the
90k-line program you have been driving — and the deliverable is not a fix
but a **reading**, written the way a reviewer reads. The instrument is
[`CODE_REVIEW.md`](../CODE_REVIEW.md): five readings of one piece of code,
each answering a different question, and a review lens that asks what would
make your own reading fluent and wrong.

The piece of code is **one turn of the agent loop**: what happens between the
model being asked and its answer arriving, tool calls included. The
architecture section of `CLAUDE.md` sketches it in eight lines; the
[Annai](../reading/ANNAI.md) walks it; [Fukabori 02](../reading/fukabori-02-the-provider-abstraction.md),
[03](../reading/fukabori-03-the-three-arena-lifetime-model.md) and
[04](../reading/fukabori-04-the-agent-loop-as-a-state-machine.md) each argue
one of its design decisions; and [Tsuiseki 01](../reading/tsuiseki-01-a-tool-round.md)
replays it byte for byte. You have read about it. Now read it.

Write `docs/assignments/74-read-the-turn/READING.md` with these five
sections, each headed exactly as shown:

1. **`## Abstraction to concrete`** — the loop calls `build_request`
   through a function pointer. Name the abstract slot, **every** concrete
   implementation that fills it, and which one a run with the default config
   actually reaches. An abstraction followed to one target is a guess about
   the others.
2. **`## Control flow`** — the real order of execution for one turn, from
   `jc_agent_run_turn` to the final answer: the loop, the branch that decides
   "tool calls or done", and where a retry re-enters. Name the functions in
   the order they run.
3. **`## Data flow`** — follow **one value** through its whole life: the
   request body. Where is it allocated, who owns it at each step, and where
   exactly is it freed? Say which of the three arenas (or none) it lives in,
   and why it cannot be reused across retries.
4. **`## Execution`** — check your reading against a **recorded run**: pick
   one trace under `docs/reading/traces/` and match at least three of its
   events to the function that produced each. Where the record and your
   reading disagree, the record wins — say so.
5. **`## Review`** — the reviewer's question, turned on yourself: which claim
   in your own reading would you least like a recorded run to contradict,
   and what did you do to check it? One finding a reviewer would raise about
   the code, with the evidence.

**Cite where you read**, as `src/chat/jc_agent.c:jc_agent_run_turn` — a path
and a symbol. The grader holds you to at least six distinct anchors, and,
when you grade from the jichi checkout, checks that **every one resolves**:
an anchor to a symbol that is not in that file is the reviewer's own
fluent-but-wrong, and it fails the grade. (Graded from a copy of the
assignments without `src/` beside it, the anchors are checked for form only
and the grade says so.)

Drive the agent as your reading partner — `find_references build_request`,
"which struct fills this slot?", `/map` — but **you** write the reading, and
if you load the `code-reading` skill it will ask you to predict before it
reveals. The conviction standard is the record, and yours.

Grade with `jichi grade docs/assignments/74-read-the-turn.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/74-read-the-turn.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/74-read-the-turn.md` (or `/hint`) gives one rung at a time — free, and recorded. No toolchain is needed: the grader reads your `READING.md` and, from the checkout, the tree it cites.
