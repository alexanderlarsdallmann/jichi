# Reading code for review — five readings, and how to run one

*The instrument, written down so the reading does not have to be reinvented
each time. It is the code counterpart of [DOC_REVIEW.md](DOC_REVIEW.md): that
page catches prose that is coherent and untrue of the program; this one catches
a **reading** that is coherent and untrue of the program — the summary that
names every function correctly and describes a flow the code does not have.
M627.*

## 1. The reader is one person, alone, and the code is not theirs

Review for a **self-learner working alone**: no colleague to ask "does this go
through the vtable?", no author to confirm who frees the buffer. That clause
does the work. It turns "I think this is the path" into a claim that must be
**checked against something that cannot be wrong about the program** — the
source itself, and a recorded run of it. Everything below is a way of asking
the program rather than yourself.

The four passive guides teach the material: [Annai](reading/ANNAI.md) walks
one request through the code, [Fukabori](reading/FUKABORI.md) argues one design
decision per chapter, [Tsuiseki](reading/TSUISEKI.md) replays recorded runs byte
for byte, [Kiroku](reading/KIROKU.md) reads the record. This page is what you
*do* with them: the discipline, and its graded floor,
[`assignments/74-read-the-turn.md`](assignments/74-read-the-turn.md).

## 2. The rubric — five readings of one piece of code

Every non-trivial piece of code can be read five ways, and a reading that made
only one of them is a reading that will be surprised. Each row is a question, the
move that answers it, and the failure it catches.

| | Question | The move | The failure it catches |
|---|---|---|---|
| **Abstraction → concrete** | Where does this abstract call *actually* go? | Find the dispatch — the function pointer, the vtable, the table of handlers. Name **every** implementation that fills it, then the one this path reaches. | "It calls the provider" — true, and says nothing. An abstraction followed to one target is a guess about the others. |
| **Control flow** | What is the real order of execution, and which branch runs? | Walk it as a state machine: the loop, its exits, where a retry re-enters. Write the sequence of function names in the order they run for one concrete input. | A flow described from the *names* of things (`retry`, `handle`) rather than from the branches that decide. |
| **Data flow** | Where is this value born, where does it die, who owns it between? | Pick **one** value and refuse to let go of it. Allocation, every handoff, the free. Which arena — or none — and why. | A lifetime assumed from a variable's *scope* rather than its ownership; the buffer "in the arena" that a callback freed. |
| **Execution** | Did it *actually* run that way? | Reconcile the reading against a **recorded run** — an event stream, the bytes on the wire, the file left behind — never against your expectation. Match events to the function that emitted each. | The reading that is consistent with itself and contradicted by the log. |
| **Review** | What would make *my* reading fluent and wrong? | Name the one claim you would least like the record to contradict, and what you did to check it. Then name one finding a reviewer would raise about the code, with evidence — and put it through the four critical questions of a consequence argument (curriculum 07 §2b): how likely, how costly and to whom, what the alternative costs, and what the evidence is for the *likelihood*. | Confidence in proportion to fluency rather than to checking. Tsuiseki 04's whole finding: a wrong call and a right one leave identical summaries. |

The order matters and is the order to write in. Abstraction first, because
until you know *which* concrete code runs you cannot trace anything. Execution
fourth, because it is the check on the three before it. Review last, because
it is about the reading, not the code.

## 3. How to run a reading

1. **Name the unit.** One turn, one request, one tool call — a piece with a
   beginning and an end you can point at. "The agent loop" is a subject; "one
   turn from `jc_agent_run_turn` to the answer" is a unit.
2. **Cite where you read, every time.** Anchors in the form the reading guides
   use — `src/chat/jc_agent.c:jc_agent_run_turn`, a path and a symbol. Not a line
   number (they rot; [`reading_refs_lint.sh`](../tests/smoke/reading_refs_lint.sh)
   forbids them in the guides for that reason). An anchor is a checkable claim; a
   sentence without one is an opinion about code.
3. **Use the agent as a reading partner, not a reader.** `find_definition`,
   `find_references`, `/map`, "which struct fills this slot?" — it fetches, you
   read. If you load the `code-reading` skill, it will ask you to **predict**
   before it reveals, because a prediction you got wrong is the only reading that
   teaches.
4. **Check against a record before you write the Review.** Take a trace
   (`docs/reading/traces/`, or `--output jsonl` on a run of your own) and match at
   least three events to the code. Where the record and the reading disagree, the
   record wins and the reading says so.
5. **Grade it.** For the shipped unit,
   `jichi grade docs/assignments/74-read-the-turn.md`. The grader checks the
   floor — five sections, the named subject, every concrete implementation, a
   recorded run, and (from the checkout) that **every anchor resolves**. Whether
   the reading is *insightful* is judgment, yours and a reviewer's; that it is
   complete, anchored and checked, a script can say.

## 4. What the grader checks, and what it cannot

- **Checked** — the five sections exist; the reading names its subject and
  *both* concrete `build_request` implementations; the Execution reading names a
  trace under `docs/reading/traces/`; at least six distinct `file.c:symbol`
  anchors; and, when graded from the jichi checkout, that each anchor's symbol
  is in the file it names. An invented citation fails the grade, by name.
- **Skipped, and said so** — anchor resolution when `src/` is not beside the
  assignments (the e2e proof harness copies only `docs/assignments`). The grade
  then rests on the structural floor and prints that it did. A gate you cannot
  run is skipped, not passed — the M625 rule, applied to one check.
- **Not checkable** — whether your control-flow order is *right*, whether the
  value you followed is the one that matters, whether the review finding is real.
  Coherent, anchored, checked and **wrong** is still possible; that is why the
  reference reading is a comparison to argue with, not an answer key.

## 5. When to read this way

- **Before you change code you did not write** — task 24's method
  ([READING_OPEN_SOURCE.md](READING_OPEN_SOURCE.md)) is the fix-forward form of
  the same five readings.
- **When a summary sounds right.** The agent's, a colleague's, your own. The
  Execution reading exists for exactly that moment.
- **When a bug report hands you an effect.** Tsuiseki's direction — from the
  log back to the code — is the Execution and Control-flow readings run in
  reverse, and it is the one you will use most.
