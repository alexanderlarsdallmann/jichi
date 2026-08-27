# Reading open-source C — with the agent as your reading partner

*A curriculum extra track (graded floor:
[`assignments/24-read-a-real-project.md`](assignments/24-read-a-real-project.md));
the map is [CURRICULUM.md](CURRICULUM.md). This is the expansion direction
the curriculum design named from the start: jichi must support learners in
"reading, analyzing, testing, and refining open-source C projects" — and
jichi's own source is the first text.*

Most code you will ever work on, you did not write. Reading it is a craft
with a method, and an agent changes the method less than you would think:
it makes the *survey* faster and the *conviction standard* more important,
because now two parties can be confidently wrong about the same file.

## The method (what task 24 grades in miniature)

1. **Contract first.** Headers, README, the public names. What does this
   thing *promise*? Write the promise down before reading any body.
2. **Tests as a trust map.** A project's tests tell you what its authors
   proved; everything else they merely believed. The uncovered function is
   where reading pays. (Task 24's bug lives in exactly that shadow.)
3. **Trace one concrete case on paper.** Not "does trim look right" but
   "five notes, keep two — which index survives?" Agents are excellent at
   running such traces with you; make it show its work line by line.
4. **Convict mechanically.** A finding is not real until a test fails on
   the as-found code and passes on the fix — the two-sided bar
   (Modules 3 and 5), now aimed at foreign code. "I'm pretty sure trim is
   off by one" is a hypothesis; `test_trim.c` is a finding.
5. **Refine with respect.** In a real project the fix goes upstream:
   smallest possible diff, the proof-test included, the analysis as the
   commit message. Maintainers merge evidence, not opinions.

## The agent's role, honestly

Use it to **survey** (`/map`, `@folder:src/`, "which functions have no
caller?"), to **trace** ("walk journal_trim with count=5, keep=2"), and to
**draft** the proof-test. Do not use it to *decide* — decisions rest on
runs, and the whole failure mode of agent-assisted reading is a fluent,
plausible, wrong summary of code neither of you actually executed
(Module 9's territory; the anecdotes ship with this repo for exactly this
reason).

## The road beyond the graded floor

After task 24, practice on real trees, in ascending order of hostility:

1. **jichi itself** — with a written companion:
   [案内（あんない）*Annai* — the guided tour](reading/ANNAI.md) walks the
   source chapter by chapter (and 深掘り（ふかぼり） *Fukabori* will argue its
   decisions). You already built it, `docs/` explains every design
   decision honestly (including the failures), and its history is a course
   in itself: read `docs/analysis/2026-07-29-tool-arena.md`, then find the
   three arenas in `src/` and verify the lifetimes yourself
   (`docs/assignments/` set D is this, shrunk to fixtures). The repo map
   (`jichi map`), `@sym:`/`find_definition` (with an LSP configured), and
   `codebase_search` are the survey tools.
2. **A small classic you already use** — a single-purpose tool of a few
   thousand lines from your distribution's source packages. Repeat the
   method; the goal is one ANALYSIS.md and one *candidate* test gap, not a
   patch.
3. **A project you want to contribute to** — now the refine step is real:
   its CONTRIBUTING.md is your rulebook, its issue tracker your review,
   and the two-sided proof-test is what makes a first-time PR mergeable.

Keep every ANALYSIS.md. A folder of them is the reading record the
capstone (Module 11) asks you to already have the habit of keeping.

## Then drive it on itself

There is a rung above reading, and jichi is the rare codebase where you can
reach it: **point the tool at the source you have been reading.** Read it
([Annai](reading/ANNAI.md)) → trace a run of it
([Tsuiseki](reading/TSUISEKI.md)) → drive it on itself. The working pack is
[`examples/self-hosting/`](../examples/self-hosting/README.md).

**Read this part before the commands.** A tool that develops itself can launder
its own defects: a weak reviewer approves a weak change, and the evidence you
would use to notice is produced by the thing under judgement. That is not a
reason to avoid this — it is the reason the pack is shaped the way it is, and
the shape is the lesson:

- **Read-only first.** The review slice cannot write. Nothing it says can damage
  the tree, so you can point *any* model at it and find out what that model is
  worth as a reviewer — which is the actual experiment.
- **The gate is `make ci`, not the agent.** gcc and clang at `-Werror`,
  ASan/UBSan, valgrind, the smoke tier, the e2e tier. The reviewers are a second
  opinion that runs in seconds; they are not a verdict, and the pack says so in
  its own first section.
- **What you are measuring is the model, not the code.** If a review invents
  nitpicks, misses a planted `long long`, or calls a change "safe" without
  naming the gate, you have learned something about that model and nothing about
  the diff. Module 9 is this skill; this is where you practise it on a real one.

### What you need, and what you do not

You need a built jichi and a **local model server** — nothing institutional.
`config.jichi-dev-local.json` is keyless, points at loopback, and declares its
context window; change the endpoint to whichever backend you run
([LOCAL_MODELS.md](LOCAL_MODELS.md) has llama.cpp, Ollama and LocalAI; the file's
own comment lists the three default ports).

```sh
# in the jichi checkout
mkdir -p .jichi/agents .jichi/commands
cp examples/self-hosting/agents/*.md   .jichi/agents/
cp examples/self-hosting/commands/*.md .jichi/commands/

# the one check that decides whether this can work at all:
./jichi --config examples/self-hosting/config.jichi-dev-local.json doctor --live
```

Two things about that copy, both worth knowing before they confuse you.
`.jichi/` is git-ignored wholesale, so your copies never show up in
`git status` — the two documentation reviewers the repository ships there are
tracked *despite* that rule, which `.gitignore` explains at the rule itself. And
because your five join those two, `jichi agents` will list **seven**. Take yours
out by name when you are finished — not for git's sake, but so the next person
reading this bench sees what the repository actually ships:

```sh
# in the jichi checkout
for f in examples/self-hosting/agents/*.md;   do rm -f ".jichi/agents/$(basename "$f")";   done
for f in examples/self-hosting/commands/*.md; do rm -f ".jichi/commands/$(basename "$f")"; done
git status --short .jichi     # expect: nothing
```

That last command is not a formality. These reviewers work by **reading files**,
so a model that cannot emit a native tool call cannot review anything — it will
answer fluently about a diff it never opened, which is the exact failure Module 9
teaches you to catch, arriving from your own bench. `doctor --live` observes tool
calling and tells you before you trust a word of the output.

Then make a change and ask for the review:

```sh
# in the jichi checkout
./jichi --config examples/self-hosting/config.jichi-dev-local.json -p "/review-diff"
```

### The second rung: let it write, fenced

`config.jichi-dev-write.json` allows edits, and reading *why it is still safe* is
worth more than running it: a positive `editScope` of `tests/**`, `docs/**` and
`CHANGELOG.md` — so the loop can reach neither `src/` nor its own guardrails —
`verify: "make test"` with rollback, and `revertOutOfScope` for a stray edit made
through the shell. That is curriculum
[Module 8](curriculum/08-bounded-autonomy.md)'s subject as a real artifact. Run
it on a branch, never on master, with budgets, and read the journal afterwards
(`jichi runs`) rather than watching it.

### Honest limits, so you can judge whether it is working

- The pack's measured finding is that **model latency, not the harness, gates
  this**: full review turns on a loaded shared endpoint sometimes did not finish
  in 200–300s. A small local model may be *slower*, not faster. Measure yours.
- The write slice has **not** been shown to complete a real task end to end —
  that is criterion 2 of its own promotion bar, and it is openly unmet. You are
  looking at a first slice, not a finished product, and the README says which
  parts have been exercised and which have not.
- `src/` edits are deliberately out of scope. If you want the agent to change
  core code, that is a decision you make by hand, with the gate in front of you.
