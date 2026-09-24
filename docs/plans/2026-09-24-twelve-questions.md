# Twelve questions, 2026-09-24: what jichi has, what it lacks, and what to build next

*A plan, written on threadwork the afternoon the operator asked twelve questions at once. It
is for three readers: the operator, who decides; a self-learner or junior developer, who
wants to know what exists and what is honestly missing; and an agent, which will implement
one of these and needs to know where to start and how to prove it worked.*

**How this page was made, so you can weigh it.** Six read-only research passes read the tree
for the questions' subjects and reported what exists, with paths. Three questions were
answered with something runnable the same day (M740): **1**, **6** and **12**. For the other
nine this page is the answer: a design, not a delivery. Everything stated as a fact about
jichi carries a path you can check; everything stated as a recommendation is labelled one.
What the research found broken along the way is in [`../DEFERRED.md`](../DEFERRED.md), section
"found by the research for the operator's twelve questions (M740)", most urgent first.

| # | Question | State at M740 | Where |
|---|---|---|---|
| 1 | What should a tester send the developers? | **answered** — an email template | [`../PLATFORM_TESTING.md`](../PLATFORM_TESTING.md) §5 |
| 2 | An algorithms course? | none exists; a design below | §2 |
| 3 | Helping learners build, train and fine-tune small models | decided *not in jichi*; a lab design below | §3 |
| 4 | More platforms — Amiga, a Windows clone, bare metal? | ranked candidates below | §4 |
| 5 | Testing on the JLU JupyterHub, next week | a run plan below | §5 |
| 6 | SQLite, possibly over MCP | **answered** — three routes, run | [`../SQLITE.md`](../SQLITE.md) |
| 7 | An extended CHR engine for logic-heavy tasks | a design and its issues below | §7 |
| 8 | UX design, again | a gap list and a study protocol below | §8 |
| 9 | Better TUI commands and control, for users and agents | recommendations below | §9 |
| 10 | File-based cooperation between agents | what exists, and an experiment below | §10 |
| 11 | Multi-stage, multi-model prompt processing | a design and an A/B plan below | §11 |
| 12 | The most sought-after and the most overlooked use case | **answered**, with sources | [`../analysis/2026-09-24-agent-use-cases.md`](../analysis/2026-09-24-agent-use-cases.md) |

---

## 2. An algorithms course

**Short answer: there is none.** The nearest material is
[`../DATA_STRUCTURES.md`](../DATA_STRUCTURES.md) (containers, arenas, the tree jichi did not
write) and graded tasks 78-80 (a hash table against a linear scan, a vector against a list, a
sorted array against a binary search tree), with 52 and 54 (a growable array, an arena).
[`../CURRICULUM.md`](../CURRICULUM.md)'s "what this course does not teach" does not even list
algorithms.

**What jichi has that no textbook has: its own implementations, running.** A course built on
them teaches an algorithm *and* its cost in a program the learner uses every day. The research
counted these, each a real function:

| Part | Lesson | jichi's implementation | The question it teaches |
|---|---|---|---|
| I. Measure before you believe | hashing | `jc_reread_hash` (djb2), `jc_prefix_hash` (FNV-1a) in `src/util/` | how many collisions on *jichi's own* tool names and paths? measure, do not recite |
| | growth by doubling | `jc_vec_reserve`, `jc_sb_reserve` | amortised O(1), shown by counting reallocations |
| | edit distance | `jc_str_edit_distance` (two-row DP), `jc_str_closest` | the "did you mean" behind a mistyped command |
| | diff | `jc_diff_unified` (LCS DP, capped at 4M cells, then a fallback) | why jichi's diff is **not** Myers, and what the cap costs |
| II. Parsing and streams | recursive descent | `src/json/cJSON.c` (`parse_value`, depth cap 256) | a grammar as functions; why a depth cap |
| | a state machine over bytes | `jc_sse_feed` (`src/net/jc_sse.c`) | framing a stream that arrives in arbitrary chunks |
| | validation | `jc_utf8_valid` (RFC 3629) | overlongs and surrogates: the cases a naive check accepts |
| | glob matching | `jc_glob_match` (backtracking `**`) | when backtracking is exponential, and how to prove it is not here |
| III. Choosing and ranking | ranking | `jc_lexical_topn` (BM25-lite), `jc_cosine_topn`, `jc_rrf_fuse` | three ranking functions and one fusion rule, on the repo you are in |
| | a bounded memory | `jc_noprogress_note` (a 32-entry LRU by logical clock) | why an O(n) scan beats a hash + list at n = 32 — measured |
| | retrying | `stream_once`'s backoff (500 ms doubling, 8 s cap, **no jitter**) | what jitter is for, and whether jichi needs it |
| | a worker pool | `run_pool` (`fork` + `select`) | concurrency without threads |

And a closing lesson on what jichi does **not** have — no binary search, no hash table, no
tree — which is `DATA_STRUCTURES.md`'s thesis turned into an exercise: when the simple thing is
right, and how you would know it had stopped being right.

**Recommendation.** Two steps, cheapest first.

1. **A reading-track page, `ALGORITHMS.md`, ungraded**, four lessons (hashing, edit distance,
   the SSE state machine, BM25), each in the house shape of the reading guides: why it exists,
   the shape, the C, *prove it to yourself* (a measurement the learner runs), where it bit us.
   It costs a page and can ship in one milestone.
2. **Graded tasks later**, once the page has been read by someone. That is a real milestone:
   the curriculum's lints hold every graded task to a two-sided grader proof
   (`tests/e2e/curriculum_graders.py`), an exact count of `CANNOT RUN` guards
   (`assignment_guard_lint.sh`), a closed list of stage slugs (`stage_index_lint.sh`), and the
   counts in `CURRICULUM.md` (`docs_counts_lint.sh`). All four must move together.

Exercises stay **C**, the operator's decision of 2026-07-28 (`CURRICULUM.md`, "Why C").

**For the operator:** a reading page first, or straight to a graded track? And does the course
target the self-learner alone, or a course at JLU with an instructor
([`../curriculum/INSTRUCTOR.md`](../curriculum/INSTRUCTOR.md))?

**For an agent implementing step 1:** write one lesson, and make its *prove it to yourself*
block a command you ran, whose output you pasted. `tutorial_refs_lint.sh` will hold every
`src/` path and `function()` the page names to the tree — add the page to its hand-written list.

---

## 3. Helping learners build, train and fine-tune small language models

**Short answer: jichi will not train anything — that was decided — but it can be the
learner's lab partner.** [`../ML_SUPPORT.md`](../ML_SUPPORT.md) says it plainly: *"No training,
no fine-tuning, no gradient anything, no dataset builders, no model-file loaders … shell out,
never vendor."* That decision stands; nothing here reopens it. What it leaves open is
everything around the training — and that is most of what a learner struggles with.

**What already exists, and was never run:** [`2026-08-edge-ai-uno-q.md`](2026-08-edge-ai-uno-q.md)
(a small model on an edge board — Qwen3 0.6B, Llama 3.2 1B, Gemma 3 1B, SmolLM2 360M named as
candidates; plan only) and [`2026-08-edge-ai-curriculum.md`](2026-08-edge-ai-curriculum.md)
("tracked, not implemented"). [`../LOCAL_MODELS.md`](../LOCAL_MODELS.md) covers serving
(llama.cpp, Ollama, LM Studio) and which local models can drive tools.

**What a plan needs to decide, in this order:**

1. **Who, and to what level.** Four levels are genuinely different courses:
   (a) *read* a tiny model's code — inference in a few hundred lines of C exists
   (`llama2.c`-style), which fits a C-first curriculum;
   (b) *train* a character-level model from scratch — minutes on a CPU;
   (c) *fine-tune* a 0.5-1 B model with LoRA — a GPU, and a day;
   (d) *evaluate, quantise and serve* the result — and drive it with jichi.
2. **Where the compute is.** A CPU is enough for (a) and (b). (c) needs a GPU: the bench GPU
   documented in [`../BENCH_LOCAL_GPU.md`](../BENCH_LOCAL_GPU.md) (16 GB), or the JupyterHub
   if it offers one — unknown, and §5 asks.
3. **The toolchain, outside jichi:** Python with PyTorch for (b)-(c), or a C training code for
   learners who stay in C; a LoRA library for (c); llama.cpp for quantising and serving (d).
   jichi runs them; it does not contain them.
4. **Data, and its licence** — a public-domain corpus for (b); for (c), a dataset whose licence
   allows it, and a held-out split the learner never trains on.
5. **What counts as done** — the honesty part, and the part jichi is actually good at: a
   validation loss on held-out data, a before/after on a fixed prompt set, and a note of what
   was *not* evaluated. Training produces confident-looking numbers that mean nothing without a
   held-out set; this is §2.2 of [`../PLATFORM_TESTING.md`](../PLATFORM_TESTING.md) in another
   domain.

**What jichi would contribute — a design, all of it inside existing mechanisms:**

- **Skills** ([`../SKILLS.md`](../SKILLS.md)): *train-a-char-model*, *lora-finetune*,
  *evaluate-honestly*, *quantise-and-serve* — recipes the model loads on demand.
- **A scaffold pack** (`jichi init`, [`../SCAFFOLDING.md`](../SCAFFOLDING.md)), *slm-lab*: the
  skills, a read-only reviewer agent that checks for train/test leakage, and an `AGENTS.md`
  naming the lab's boundaries.
- **Long runs supervised, not blocking**: training runs as a background command
  ([`../BACKGROUND.md`](../BACKGROUND.md)) the agent polls, under a deadline.
- **Graders for the exercises** (level b and up): "your tokenizer round-trips this text",
  "validation loss below X on the held-out file you were not given".
- **Closing the loop:** the model a learner fine-tuned, served by llama.cpp, runs jichi's own
  driven task ([`../PLATFORMS.md`](../PLATFORMS.md), *Driven*) — the same two turns every
  platform row runs. A learner's model either calls a tool and reports the phrase, or it does
  not, and that is an honest, cheap, final exam.

**For the operator:** which level first; which compute; and whether this is a course track
(graded, in `docs/assignments/`) or a scaffold pack (ungraded, in `examples/`). **Recommendation:**
level (b) on a CPU as a scaffold pack first — no GPU, no dataset licence questions, and it
teaches the evaluation habit before the expensive part.

---

## 4. More platforms: Amiga, a Windows clone, bare metal, something else

**What FreeMiNT taught, as a template** ([`2026-09-freemint-aranym.md`](2026-09-freemint-aranym.md)):
measure the whole POSIX gap in one `make -k` log; fix gaps with probes, not `#ifdef`s; pin every
download; let the rig write the emulator's config; give the guest a halt and a side channel;
drive over plain HTTP with a TLS-free libcurl; script the rig only after a hand run.

**The wall every candidate meets is `fork`.** jichi calls `fork()` in ten files — shell tools,
background commands, MCP stdio servers, git snapshots, the parallel pool, LSP — and needs
`termios`, `select` over pipes and PTYs, and AF_UNIX sockets
([`../PORTING_WINDOWS.md`](../PORTING_WINDOWS.md) has the table). A system without `fork` is a
**port**, not a row.

| Candidate | What it would teach | The wall | Cost | Verdict |
|---|---|---|---|---|
| **GNU/Hurd** (Debian, in QEMU) | glibc on a *microkernel*: `fork`, signals, pipes and PTYs implemented by servers, not a kernel | known POSIX gaps (e.g. no `PATH_MAX`) — each one a defect detector | an evening | **first** |
| **Haiku** (QEMU) | a non-Unix lineage with a POSIX layer and its own package manager | unknown; gcc and curl exist there | an evening or two | **second** |
| **A fork-free jichi** (a design, not a row) | which features need a process, and whether a read/edit/search agent works without one | ten call sites | a milestone to *measure* | **unlocks the four below** |
| **AROS** (x86, QEMU) / **AmigaOS 3** (FS-UAE) | the Amiga API; no MMU on classic AmigaOS | no `fork`; partial POSIX (ixemul/libnix on 3.x) | a port | after the fork-free design |
| **FreeDOS + DJGPP** | a single-tasking OS with a gcc and a TCP stack | no `fork` (spawn only) | a port | after the fork-free design |
| **NuttX** (POSIX RTOS; QEMU first, then a board) | the RAM floor for real — jichi's *own* peak was 1,668 KB on the 96 MB tiny rig ([`../LOW_MEMORY.md`](../LOW_MEMORY.md)) | `vfork`/`posix_spawn` only, TLS, a libcurl build | a research milestone | the "edge AI edge case", honestly labelled |
| **A unikernel** (Unikraft, OSv) | jichi as the only program on a hypervisor | no `fork` at all in a single address space | as NuttX | only with the fork-free design |
| **ReactOS** | a Windows-NT clone | jichi has **no native Windows port**, by design (`PLATFORMS.md`) | a port of the port | **last** — or run Cygwin on it, as a Cygwin defect detector |
| **Minix 3, SerenityOS, Redox, Plan 9, QNX** | each a different POSIX story | varies; QNX is licensed | an evening each | possible later rows; none ahead of Hurd |

**Bare metal, honestly.** The project decided at Tier C that jichi does not run on a
microcontroller: 520 KB of SRAM, `fork` structural, MMU-less Linux rejected
([`2026-07-hardware-testing.md`](2026-07-hardware-testing.md)). What *does* work on metal is the
pattern already recorded in [`../ROBOTICS.md`](../ROBOTICS.md): **the microcontroller is a tool,
not a host** — jichi runs on a board with an OS and reaches the controller through an MCP
server marked `kinetic`. The one number that makes the question worth reopening is the 1,668 KB
peak above: if the fork-free design shows a useful agent without processes, an RTOS with
megabytes of PSRAM becomes an experiment rather than a fantasy.

**For the operator:** Hurd next? And is the fork-free design worth a measuring milestone — it
decides four rows at once. **For an agent:** start from `make -k` in the guest and save the log
before fixing anything; the gap list is the first result.

---

## 5. The JLU JupyterHub, next week

**What exists** ([`../JUPYTERHUB.md`](../JUPYTERHUB.md), 902 lines): jichi was built and run in
JupyterLab (tier J1, 11/11) and in a two-user JupyterHub in a VM (tier J2, 9/9) — **against a mock
model only**. There is no PLATFORMS row, the browser half (xterm.js and its keys) was never run,
and notebook support is deferred on a measurement: one notebook with one figure cost ~65,600
tokens, 257 times its paired `.py` ([`../DEFERRED.md`](../DEFERRED.md), "JupyterHub and notebooks").

**What next week can add that nothing before could:** the institution's *real* hub, with a
*real* model — the institution's own gateway, reached from inside the university network.

**Ask or record first** (each changes the plan): the spawner (local process, Docker, Kubernetes)
and the image's OS and libc; whether `gcc`, `make` and libcurl headers are there or must come from
conda; network egress to the gateway; a GPU, if any (it decides §3's level c); home persistence,
quota and idle culling; the `/tmp` inode limit (M739's lesson); how API keys are expected to be
provided (never in a notebook, which gets shared).

**The run, cheapest first:**

1. `jichi doctor` and a build from source in a hub terminal, no sudo — the J2 recipe.
2. `make check-target` in the hub — the smoke tier needs PTYs, which the hub's terminal provides.
3. **Drive it:** the two turns of the driven task against `jlu/qwen3-coder-next` → the first
   JupyterHub row that is **Driven**.
4. The browser half, with a person: the TUI in JupyterLab's terminal, the keys that browsers
   take for themselves (the DEFERRED row says about ten minutes).
5. One real learner notebook, measured: does the 257× hold, and do learners live in notebooks?
   That answer is the trigger the notebook deferral has been waiting for.

**For the operator:** a date, and which learner scenario matters — terminal-first or
notebooks-first.

---

## 7. An extended CHR engine for logic-heavy tasks

**Short answer: jichi is equipped at the transport level and not yet at the level that
matters, which is whether models use a solver well.** No CHR, Prolog or logic engine is
mentioned anywhere in the tree. What jichi has is the way in:

- an **MCP client** (stdio and streamable HTTP; tools, resources and prompts;
  [`../MCP.md`](../MCP.md)) — the natural home for a solver with state;
- **user-defined tools** with the arguments as JSON on stdin — enough for a stateless call;
- **skills**, which is how a model learns a notation it has barely seen;
- journals and `--verify`, which is how a run proves what it did.

**The design recommended: an MCP server around the engine.** Tools with narrow jobs:
`chr_load` (a program), `chr_query` (a goal, with limits), `chr_store` (the constraint store now),
`chr_explain` (a trace of the last derivation). The server holds the engine's state for the life
of the jichi run (one stdio process per run, so no session affinity problem).

**The issues, and whose they are:**

| Issue | Why it matters | Owner |
|---|---|---|
| **Termination.** A CHR program need not terminate. | jichi's MCP timeout is **hard-coded at 120 s**, not configurable, and a killed call is the only outcome. | the engine must take a step or time budget per query; jichi should make the MCP timeout configurable |
| **Output size.** A constraint store can be huge. | jichi has **no MCP result cap**: up to 8 MiB per message goes whole into history. | the server caps and says what it cut (as `examples/sqlite/sqlite_mcp.py` does); jichi should gain a cap |
| **Structured results.** | jichi ignores MCP's `structuredContent` and reads text only. | return JSON *as text*; jichi could pass structured content through |
| **Errors as values.** | An unsatisfiable goal is an answer, not a failure. | the server returns it with `isError: false`; a real engine error with `isError: true` |
| **Model competence.** Models have seen little CHR. | A tool the model cannot use is not a capability. | a skill: syntax, ten worked examples, the *extended* features; a `chr_check` tool that parses a program before it runs |
| **A name collision.** jichi has "constraints" of its own — rules about tool use ([`../CONSTRAINTS.md`](../CONSTRAINTS.md)). | A model — and a learner — will conflate them. | name every tool `chr_*` and say "CHR constraint" in every description |
| **Trust.** A Prolog host can run shell commands. | jichi's fences bound jichi, **not** a server process ([`../MCP.md`](../MCP.md)). | run the engine sandboxed, with its shell predicates removed |
| **Long solves.** | jichi skips MCP progress notifications. | fine for seconds; for minutes, jichi would need to show progress |

**How to know it worked — the part that decides the whole question.** A set of logic-heavy
tasks with known answers (scheduling, type inference, puzzles with a checkable solution), run
twice through [`../../scripts/corpus-drive.sh`](../../scripts/corpus-drive.sh): with the tool
and without it. Count correct answers, and read what the model *did* with the solver — asked it
first, or reasoned first and checked after. The second pattern is the neuro-symbolic loop worth
having; the first may just move the error.

**For the operator — the questions only you can answer about your engine:** its host language
and how a program calls it; whether it is stateful; whether it is deterministic; what "extended"
adds (soft constraints? probabilities? time?); and its licence. **For an agent:** build the MCP
server against a *toy* CHR program first (gcd, or a small scheduler) and check it with
`jichi mcp call` before any model sees it — §3.2 of [`../SQLITE.md`](../SQLITE.md) is the pattern.

---

## 8. UX design, again

**What exists is more than it looks like, and it is scattered.**
[`../INTERFACE_TUTORIAL.md`](../INTERFACE_TUTORIAL.md) states the thesis (*"You are here. This is
what you can do here."*) and its checklist; [`../ACCESSIBILITY.md`](../ACCESSIBILITY.md) the
accessible mode; [`../TUI_RENDER.md`](../TUI_RENDER.md) the renderer;
[`../proposals/2026-08-tui-chrome-channel.md`](../proposals/2026-08-tui-chrome-channel.md) the
content/chrome split (*content is never altered; chrome is jichi's own lines*); and
[`../proposals/2026-08-accessibility-by-default.md`](../proposals/2026-08-accessibility-by-default.md)
the synthesis — *prose where a human must understand, compression where a human must scan.*

**What does not exist:** a single UX principles page for the TUI; any study with learners. The
only usability evidence is the operator's own screen-reader sittings (2026-08-22/23) — and those
found **12 of 20** defects in M551-M570, where the test suite found none
([`../analysis/2026-08-24-twenty-milestones-by-ear.md`](../analysis/2026-08-24-twenty-milestones-by-ear.md)).
That ratio is the argument for the study below.

**Recommendations.**

1. **One principles page** collecting what the five documents above decided, so a contributor
   finds the rules in one place. It restates; it does not invent.
2. **A learner study, small and repeatable:** three to five self-learners, five tasks (install
   and build; a first prompt; approve an edit; undo it; send a platform report with §5's
   template), think-aloud, no coaching. Record time, errors, the moments they asked for help, and
   a severity per finding. The INTERFACE_TUTORIAL checklist's last item — *watch one person use it
   without coaching* — has never been recorded as done.
3. **The defects already found** ([`../DEFERRED.md`](../DEFERRED.md), M740 section): Esc cannot
   interrupt a turn, and nothing says so; `/accessible` is missing from `/help`; the built-in
   `/design` hides a scaffolded one in the TUI only.

**For the operator:** who the learners are and how to reach them (a JLU course?), and consent —
a study records people.

---

## 9. Better TUI commands and control, for users and for agents

**The inventory** (research, 2026-09-24): 55 slash commands in `TUI_CMDS[]`
(`src/tui/jc_tui.c`, counted), a line editor with history search, completion and a kill ring, and for
programs: a control socket (`status`, `inject`, `pause`, `resume`, `abort`, and `mode`, which
only narrows — [`../CONTROL.md`](../CONTROL.md)), a daemon, an ACP server, and `describe` as the
machine-readable contract.

**For users — recommended, each small:**

| Proposal | Why |
|---|---|
| **Esc interrupts a running turn** (with a line that says so) | people press Esc; today only Ctrl-C works, and nothing says Esc does not |
| **`/why`** — the last tool decision, from the journal: which rule allowed or refused it | the most-asked question in an agent, answered from the record rather than the model's memory |
| **`/fences`** — mode, edit scope, path fence, constraints, budgets, on one screen | the posture is spread across five places today |
| **a lint comparing `help()` with `TUI_CMDS[]`** | `/accessible` is in one and not the other; nothing noticed |
| **saved input history** (0600, bounded) | an open decision since `HARDENING.md` §7.5: whether a prompt with a secret in it may be saved |

**For agents and supervisors:**

| Proposal | Why |
|---|---|
| **`jichi ps`** — live runs, their leases and control sockets | the discovery chain exists (lease → journal → socket) but no command walks it |
| **`describe` lists the control socket's `mode` verb** | it is missing there today |
| **keep "only a human grants"** | [`2026-08-tui-fence-grant.md`](2026-08-tui-fence-grant.md)'s invariant; a supervisor may narrow or abort, never widen |

**For an agent implementing any row:** the TUI is tested through a pty (`tests/tools/ptydrive`);
every key gets a driver, and `keys_lint.sh` holds the documented keys to the code.

---

## 10. File-based cooperation between agents

**What exists** (research, 2026-09-24): delegation as tools — `spawn_subagent` (one child, in
process) and `spawn_parallel` (a fork pool; write children in their own git worktrees, merged
first-writer-wins); `jichi workflow` (stages in sequence); an advisory per-workspace **lease**;
per-run **journals**; the **control socket**; and file conventions — `.jichi/board.json`,
`.jichi/PLAN.md` (a planner's plan, checked against what an executor touched), memory, the lessons
draft, and a queue of directories in `examples/autonomous-loop/` (`pending/`, `running/`, `done/`,
`failed/`, a task claimed by an atomic `rename(2)`).

**What was decided:** [`../AGENT_COLLABORATION.md`](../AGENT_COLLABORATION.md) — *no agent-to-agent
negotiation protocol, no shared mutable agent memory*; the interfaces between agents are files,
the board, budgets, journals and verifiers. **This page recommends keeping that stance** — and
making the file half of it real, because today it is a set of conventions no one has measured.

**What is broken, first** (from reading, in DEFERRED): two processes lose each other's board
updates; a lease is taken read-then-write, so two runs starting together could both hold it; no
command lists what is running.

**The experiment that turns opinion into experience:**

1. **Tonight's drive is the first data**: its `sub` and `par` arms measure delegation on real
   work ([`2026-09-24-overnight-drive.md`](2026-09-24-overnight-drive.md), Q5).
2. **Write the queue down as a protocol**, from what `examples/autonomous-loop/` already does: a
   task is a file; a claim is a `rename` into `running/` (atomic within one filesystem, and only
   there); a result is a file with a first line `VERDICT: pass|fail|blocked`; a heartbeat is a
   file whose mtime a supervisor reads; the lease is the tree's lock; the journal is the record.
   State the POSIX rules each step relies on — `rename(2)` within a filesystem, `O_CREAT|O_EXCL`,
   and why NFS needs `link(2)` instead.
3. **Run N headless agents against one queue** of zigodot tasks, supervised and unsupervised.
   Count duplicate work, stale claims after a kill, conflicts at merge, and the moments a human
   had to step in.
4. **Decide after the numbers:** a built-in queue (`--loop`, deferred in
   [`../AUTONOMOUS_LOOPS.md`](../AUTONOMOUS_LOOPS.md)) or jichi as a good citizen of external
   orchestration, as today.

**For the operator:** is the zigodot project the right testbed for step 3, after tonight?

---

## 11. Multi-stage, multi-model prompt processing

**What exists** (research, 2026-09-24): routing between a fast and a strong model, escalating on
hard signals ([`../ROUTING.md`](../ROUTING.md)); plan mode and `write_plan`; `jichi workflow` with
per-stage models, whose `refute` stage found **12 of 12** planted false claims against **1 of 12**
for a "review critically" prompt; a retrieval-only query rewrite; the UserPromptSubmit hook, which
can add to a prompt; `--design`/`--spec` documents. **Nothing rewrites the user's own words**, and
the research found no deferred item that proposes it.

**The design, and the rule it must keep.** Stages: *clarify* → *restate* → *plan* → *execute* →
*verify* → *report*, each on the model that suits it (a small fast model to restate, a reasoning
model to plan — `jlu/qwen3.8-27b` is the free one — a coder to execute). **The rule: a restatement
is shown before it is used** — confirmed by the user in a session, recorded in the journal in a
headless run — because a silent rewrite is a change of instructions nobody approved. That is the
same integrity as M530's *a preview must read every argument exactly as the executor will*.

**The risks, named so the experiment can look for them:** intent drift (the restatement is subtly
not the request); cost and latency on easy turns; the telephone game across stages; and injection
amplified — content in the prompt can instruct the restating model.

**The A/B, before any code:** three arms over the drive corpus — plain; restate-then-execute;
plan-then-execute with the plan handed to the executor (today `PLAN.md` is written and not fed
back, which is the one-line difference to test). Measure verifier results, tool calls, tokens and
time, and have a person grade a sample for **intent fidelity** — the one metric no script can
compute. The first two arms need no new code: a two-stage custom command with a `model:` per stage
([`../COMMANDS.md`](../COMMANDS.md)) or a workflow spec will do.

**For the operator:** is intent fidelity worth a person's hour per drive? Without it, the
experiment measures only speed.

---

## What the operator is asked to decide

| # | Decision | Recommendation |
|---|---|---|
| 2 | Algorithms: a reading page first, or a graded track? | the page first |
| 3 | SLM lab: which level, which compute, a pack or a track? | level (b), on a CPU, as a scaffold pack |
| 4 | Next platform, and a fork-free measuring milestone? | GNU/Hurd; yes to the measurement |
| 5 | A date for the hub; terminal-first or notebooks-first? | terminal-first, then one real notebook measured |
| 7 | The engine's language, state, determinism, "extended", licence | answers needed before a design can be final |
| 8 | Learners for a study, and consent | a JLU course, three to five people |
| 10 | zigodot as the queue testbed after tonight | yes, if tonight's delegation arms produce usable data |
| 11 | A person's hour per drive for intent fidelity | yes — otherwise the A/B measures only speed |
