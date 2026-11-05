# Review 0 — Start here: a whole-tree review of jichi, October 2026

*The overview of the 2026-10-08 review of jichi, written on threadwork (the
operator's second machine) by Claude Opus 5.5 at master `17bbb895` (M913), on the
operator's request: "review the complete jichi source code, and documentation,
and write an honest, thorough, and comprehensive documentation on your findings,
recommendations, design decisions", for self-learners and junior developers.*

---

## Who this is for, and how to use it

**For** a self-learner or junior developer who wants to understand jichi as a
whole and see a real code review of it: what the code does well, where it is
fragile, and why. You need no prior knowledge of the code; the chapters link to
the guided tours where they help.

**After reading this page you will know:**

- how the review was made and how far to trust each kind of statement in it;
- the shape of jichi in one picture, and the request it serves;
- the findings that matter most, grouped into seven themes a reviewer can look
  for in any codebase;
- what to fix first, and why in that order;
- which chapter to read next for the part you care about.

**How to read a finding.** Every finding names its evidence as `path:line` at
commit `17bbb895`. Its **severity** says what happens if it fires:

- **high** — data loss, a crash, a security boundary, or a fence that does not
  hold;
- **medium** — a wrong result or a misleading report in a plausible case;
- **low** — an edge case, an inconsistency, or a maintainability cost;
- **note** — worth knowing, not a defect.

Its **status** says how it was established:

- **verified** — traced end to end, or reproduced in a private copy;
- **plausible** — reasoned, not traced to the end.

Security findings are described **by class only** in these public pages; their
mechanisms are kept with the project's internal records until they are fixed,
as [`SECURITY.md`](../../SECURITY.md) promises reporters.

---

## 1. How the review was made

**The slices.** The tree was cut into twelve slices, and each was read by one
reviewer, a Claude Opus 5.5 subagent with a shared brief:

- **Eight code slices** cover every C file in `src/`:
  - the agent loop and context management;
  - `src/main.c`, in two halves;
  - the tools;
  - the rest of `src/chat` with sessions, snapshots, commands and skills;
  - the utilities;
  - the network, providers, JSON, configuration and platform;
  - the TUI, setup and ACP;
  - the index, LSP, MCP, converter and scaffolds.
- **One slice** covers the build, the gates and the tests.
- **Two documentation slices:** the learner pages, and the design pages with the
  registers.

**The brief.** Read every line; report each finding with its evidence; mark it
verified or plausible; check `docs/DEFERRED.md` first and report a known issue as
"already recorded"; write a junior-developer explanation of the slice; pick real
code excerpts that teach something; map what a context-pressure relay would
touch. Reviewers worked read-only, and verified findings only in private copies
of the tree, offline, with the mock model.

**What happened to the plan, honestly.**

- **Six reviews are complete:** both halves of `main.c`, the tools, chat and
  sessions, the I/O layers, and the tests and gates.
- **Six were paused** at the operator's request when their token use competed
  with a high-priority task on another machine. Each had been writing its report
  as it went, and each report is kept as it stood:
  - the agent-loop and TUI reports had filled every section;
  - the util, index/LSP/MCP, learner-docs and design-docs reports have sections
    marked unfinished.
- **The chapters below say which kind of report they rest on.** A part of a slice
  that no reviewer reached is listed as *not reviewed*. It is not described as
  clean.

**Checking the reviewers.** A review by models is model output. Four of the high
findings were re-read in the source by the author of this page before it was
written: the prune guard, the signal handler, the config editor's parser, and the
secret registry's arena. **All four were confirmed.** The rest carry the
reviewer's own status, and every one names the lines a maintainer must read
before acting.

**The measurements and experiments** behind the context chapter are described
in [Review 2](2026-10-08-review-02-when-the-window-fills.md) §3 and §5.

**In numbers:**

| | |
|---|---|
| C source read | ~93,000 lines in `src/`, ~17,000 in `include/` |
| Test code read | the Makefile, the test harness and tools, ~30 smoke drivers, 6 unit files in full |
| Documentation read | the learner path and the design pages; the registers by structure and search |
| Findings | about 200 across the twelve reports, about 15 high after merging duplicates |
| Experiments | three small live runs on a free model (Review 2 §5) |

---

## 2. jichi in one picture

jichi is an AI coding agent: a program that sends a conversation to a language
model, lets the model call **tools** (read a file, edit one, run a command), runs
those tools, sends the results back, and repeats until the model answers. It is
written in C89, depends only on libcurl, and runs on Linux and other POSIX
systems. The layers, as the code is organised:

```
                 +-----------------------------------------------+
  you  ------>   |  src/main.c   (CLI, subcommands, run modes)   |
                 |  src/tui      (interactive terminal)          |
                 |  src/acp      (editor protocol)               |
                 +-----------------------+-----------------------+
                                         |
                 +-----------------------v-----------------------+
                 |  src/chat     the agent loop (jc_agent.c),    |
                 |               compaction, envelope, sessions  |
                 +-+---------------------+---------------------+-+
                   |                     |                     |
          +--------v---------+  +--------v---------+  +--------v---------+
          | src/provider     |  | src/tools        |  | src/session,     |
          | request dialects |  | ~26 tools in a   |  | snapshot, index, |
          | (OpenAI,         |  | git repo; sub-   |  | lsp, mcp,        |
          |  Anthropic)      |  | agents, pool     |  | command, skill   |
          +--------+---------+  +--------+---------+  +------------------+
                   |                     |
          +--------v---------+  +--------v---------+
          | src/net: HTTP,   |  | src/util: proc,  |
          | SSE (libcurl)    |  | telemetry, str   |
          +------------------+  +------------------+
          src/json, src/config, src/platform underneath everything
```

**In words.** `main.c` parses the command line and picks a mode: interactive, a
headless run, a subcommand, a daemon. Every mode ends up in the agent loop in
`src/chat/jc_agent.c`. The loop asks a **provider** to build the request in the
model's dialect, sends it through `src/net`, reads the streamed reply, runs the
tools the reply asks for from `src/tools`, and loops. Sessions, snapshots, the
code index, language servers and MCP servers are services the loop and the tools
use. The utilities and the JSON, config and platform layers sit under all of it.

**One turn, as pseudocode:**

```
run_turn(user_message):
    history += user_message
    maybe_summarize_old_history()            # between turns only (Review 2 §2.1)
    loop until the model answers or a fence stops it:
        request = provider.build(system_prompt, tools, history)
        reply   = stream(request)            # text deltas shown as they arrive
        history += reply
        for each tool call in reply:
            result = run_tool(call)          # errors are VALUES the model reads
            history += result
        elide_old_tool_output_if_pressed()   # mid-turn (Review 2 §2.2)
    save_session(history)
```

The guided tour of exactly this, step by step, is
[Annai 2–6](../reading/ANNAI.md).

---

## 3. What jichi does well

A review that lists only defects misleads, so this section comes first. The
strengths below are visible in the code, not only in the docs.

1. **The discipline holds at scale.**
   - About 110,000 lines of C89 build at zero warnings under strict flags.
   - `sprintf` is absent, and a lint enforces it.
   - Tool errors are values the model reads, never control flow: a failed edit
     returns `JC_OK` with an error result (`src/tools/jc_tool_edit.c`).
2. **Pure cores, thin I/O.** The decisions that matter are pure functions with
   exhaustive unit tests: the loop detectors (`src/util/jc_toolloop.c`), the
   envelope's budget checks, the compaction passes, the run outcome
   (`src/util/jc_outcome.c`). The I/O around them is thin and driven end to end
   with a mock model. This is the pattern the relay proposal copies.
3. **Lifetimes are named.** Three arenas, session, turn and tool call, each with
   a stated rule, and a lint keeps the tools on the short ones. Most of the
   memory findings below are violations of that rule, found *because* it is
   written down.
4. **Measure before changing.** Compaction, calibration and the context gauge
   each carry the measurement that shaped them, with the date and the corpus, and
   corrections are kept visible. The registers (`ROADMAP`, `DECISIONS`,
   `DEFERRED`, `ANECDOTES`) record rejected alternatives and failures, which is
   rare and valuable for a learner.
5. **The compiler holds contracts.** For example, the run's stop reasons are
   switched without `default:`, so a new reason fails the build at every surface
   that must handle it (`include/jc_outcome.h:20-25`).
6. **Fences are layered.** Path fence, edit scope, permissions, the privileged
   command gate below the approval verdict, Landlock for write children, and a
   disk fence. Most findings about fences are gaps at the edges, not missing
   fences.

---

## 4. The findings that matter most

The high findings, merged across the reports. Each row names the chapter that
explains it; a security finding is described by its class.

| # | Finding (class for security) | Where | Status |
|---|---|---|---|
| H1 | The second Ctrl-C kills jichi with no teardown; the TUI leaves the terminal without echo | `src/main.c:145-157`, `:17253-17255` | verified (pty) |
| H2 | `prune --keep` with a non-number deletes every session of every project | `src/main.c:1268`, `:3375` | verified, re-read |
| H3 | A budget stop in the middle of a tool round leaves calls unanswered; resuming sends a malformed request | `src/chat/jc_agent.c:2668-2674` | verified (mock) |
| H4 | Secret redaction can lose its secret within one turn (a lifetime class) | `src/command/jc_refs.c:383-385` | verified, re-read |
| H5 | A parallel child's change to a file whose name git quotes is dropped, and its worktree deleted as merged | `src/tools/jc_parallel.c:87-100` | verified |
| H6 | `/tutor` frees memory owned by an arena (wrong ownership contract) | `src/tui/jc_tui.c:4192` | reported verified |
| H7 | `config set` on a config with comments wipes it down to one key and keeps it trusted | `src/config/jc_configedit.c:92-114` | verified, re-read |
| H8 | The run's reach footer can overflow a stack buffer when the plan-drift list is long | util slice, unfinished report | reported verified |
| H9 | `jc_proc_capture` crashes when called without an output buffer and the child prints | util slice, unfinished report | reported verified |
| H10 | Terminal control bytes from the model or files reach the terminal on some TUI paths (a terminal-injection class) | TUI slice | reported verified |
| H11 | ACP: a second `session/new` discards the first session unsaved; prompts run on whatever session is held | TUI slice (ACP) | already recorded (desktop D4, def-297); re-verified |
| H12 | LSP diagnostics after an edit can report "No diagnostics." for a file that has errors | index/LSP slice, unfinished report | reported verified |
| H13 | LSP edits are applied at byte columns where servers send UTF-16 columns: non-ASCII lines are corrupted | index/LSP slice, unfinished report | reported verified |
| H14 | The summarizing compaction cannot run in a headless run; nothing ends a session under exhaustion | `src/chat/jc_agent.c:5211-5224` | measured (Review 2) |
| H15 | The entry pages give contradictory platform pictures, and the beginner path demands a tick a local model cannot give | learner docs | reported verified |

---

## 5. Seven themes, and what they teach

Individual findings get fixed; themes are what a reviewer learns to look for.

### 5.1 "It said it worked": false success

The most common pattern in the review: an operation **reports success when it
did not do what it says**.

- `prune` with a mistyped number keeps zero sessions (H2).
- The parallel merge says "no conflicts" after dropping a file (H5).
- `edit_file` cuts a file at its first NUL byte and reports the edit (tools slice).
- `git_add` stages the first 18 paths and says "Staged changes" (tools slice).
- The edit-scope sweep reports "nothing changed" when git itself failed (chat
  slice).
- LSP reports "No diagnostics." for a broken file (H12).
- A run that stopped half done is reported `done`, exit 0 (Review 2, R2-4).

**What to learn.** The dangerous bug is not the crash; it is the green light on
a failure, because everything downstream trusts it. When you review, ask of every
success path: *what did it check before saying so?*

### 5.2 Lenient where it should be strict, strict where it should be lenient

CLAUDE.md asks for **lenient** reading of booleans a human or a model writes. The
review found the opposite problem with **numbers**:

- `atoi` and `atol` turn `abc` into 0 and `1k` into 1, so `--max-tool-calls abc`
  silently removes a fence and `--context-limit 32k` becomes 32 tokens (`main.c`
  slice);
- `undo dry-run`, with the dashes forgotten, performs the undo (`main.c` slice);
- the config editor reads a file **strictly** as JSON where the loader accepts
  comments (H7).

**What to learn.** "Lenient" and "strict" are not virtues on their own. Read a
value the way its writer meant it, and **refuse** a value you cannot read
rather than guessing 0.

### 5.3 Lifetimes: who owns this memory, and for how long

jichi's arena rules are explicit, which is why violations are findable:

- a secret is registered from the per-turn arena, against the registry's
  "must outlive the process" contract (H4);
- `/tutor` frees what an arena owns (H6);
- claim-ticket paths are written into a session that outlives the store they
  point into (agent-loop slice).

**What to learn.** For every pointer you store, name the owner and the
lifetime. When the comment says "must outlive the process", search for the
arena it came from.

### 5.4 Exits are the hardest code

Many high and medium findings are on the way out of something:

- the second Ctrl-C (H1);
- a budget stop in the middle of a round (H3);
- an abort that reads as HTTP 200 success (I/O slice);
- usage errors that exit 1 where the contract says 2 (`main.c` slice);
- an interrupted `attempt` still graded (`main.c` slice);
- a session saved only at the end of a turn, so a long headless run loses its
  history to a SIGKILL (chat slice).

**What to learn.** Write down every way a piece of code can end, and test each.
The success path is the one that gets exercised; the exits are where the state
goes inconsistent.

### 5.5 Long runs were designed as many turns, and are one turn

The context mechanisms were built for conversations with turns, but jichi's
heaviest use is headless runs that are one long turn. [Review 2](2026-10-08-review-02-when-the-window-fills.md)
measures the consequence, and the
[context-relay proposal](../proposals/2026-10-context-relay.md) designs the
answer.

**What to learn.** Check your assumptions about the *shape* of the workload.
"Between turns" is a fine place for a mechanism only if your users have turns.

### 5.6 The cold paths are less tested than the hot ones

The 2026-10-05 self-audit said it first, and this review confirms it:

- the daemon (one stalled client wedges every request);
- ACP (H11, and a cancelled write that still writes the file);
- the converter (YAML flow lists silently dropped);
- the LSP transport (no test at all);
- `jichi-convert`'s import of a foreign config (fences dropped silently).

The hot path, the agent loop with the tools, is tested deeply.

**What to learn.** Coverage follows use. A feature used weekly by its author is
tested by that use; one used once is not. Put a test on every documented feature
before claiming it.

### 5.7 Documentation drift where no lint reaches

Pages that a lint checks stay true; pages it does not, drift. Examples:

- COMPACTION.md still says the `[context]` note fires once per loop, and that
  argument elision is unrecoverable, both overtaken by M900 and M909;
- AUTONOMOUS_LOOPS.md's supervisor files a capped, unfinished run as done;
- the beginner pages teach a config merge rule the loader no longer has;
- several tutorials teach structures the tree no longer has.

**What to learn.** A sentence in a doc is a claim about code. The project's own
answer is the right one: turn the claims that matter into lints (def-319), and
date the rest.

---

## 6. What to fix first

In order, with the reason for the order:

1. **H1, the signal handler.** Every interactive user hits it, and every
   handover design depends on clean signal handling. The fix is small: install
   with `sigaction` and keep the handler a flag-setter.
2. **H2 and the number-parsing family (§5.2).** Data loss from a typo. One
   helper that parses a whole number or refuses, used by every numeric flag.
3. **H7, `config set`.** Data loss in a file the user wrote by hand.
4. **H3, the budget stop mid-round.** It corrupts saved sessions, and the relay
   needs clean round boundaries.
5. **H4 and H6, the lifetime pair.** Security-relevant (H4) and crash-prone (H6).
6. **H5 and the false-success family (§5.1).** Silent loss of an agent's work.
7. **H8–H13,** after the reports that found them are finished and their
   findings re-read.
8. **The context relay** ([proposal](../proposals/2026-10-context-relay.md)),
   after its prerequisites (its §9, which overlaps 1, 4 and 5 above).
9. **The documentation drift (§5.7):** the lints of def-319 first, then the
   pages.

---

## 7. The chapters

| Chapter | Subject | State |
|---|---|---|
| 0 | this overview | written |
| [1](2026-10-08-review-01-the-agent-loop.md) | the agent loop and context management | written |
| [2](2026-10-08-review-02-when-the-window-fills.md) | when the context window fills: measured | written |
| [3](2026-10-08-review-03-main-and-the-run-modes.md) | `main.c` and the run modes | written |
| [4](2026-10-08-review-04-tools-and-delegation.md) | tools, subagents and the parallel pool | written |
| [5](2026-10-08-review-05-sessions-memory-control.md) | sessions, memory, control, snapshots | written |
| [6](2026-10-08-review-06-the-utilities.md) | the utilities | written (65 of 78 files reviewed; the rest listed) |
| [7](2026-10-08-review-07-the-wire.md) | the wire: network, providers, JSON, configuration | written |
| [8](2026-10-08-review-08-tui-setup-acp.md) | the TUI, setup and ACP | written |
| [9](2026-10-08-review-09-reaching-outside.md) | index, LSP, MCP, converter, scaffolds | written (code read in full; part of the templates not reviewed) |
| [10](2026-10-08-review-10-tests-and-gates.md) | the tests and the gates | written |
| [11](2026-10-08-review-11-the-documentation.md) | the documentation | written (two paused reports; what was not reviewed is listed) |
| — | [the context-relay design](../proposals/2026-10-context-relay.md) | written |

The reviewers' full reports, with every location, reproduction and the
security mechanisms, are kept with the project's internal records for the
maintainers.

---

## 8. Read further

- [docs/README.md](../README.md): the router into all of jichi's documentation.
  *Primary for jichi.*
- [The Annai](../reading/ANNAI.md) (a guided tour of one request) and
  [the Fukabori](../reading/FUKABORI.md) (one design decision per chapter): the
  best way into the code before the chapters of this review. *Primary for jichi.*
- [analysis/2026-10-05-JICHI-CLAUDECODE-ANALYSIS.md](2026-10-05-JICHI-CLAUDECODE-ANALYSIS.md)
  and the other 2026-10-05 self-audit pages: the previous whole-tree review, by
  jichi itself, which this one confirms in several places. *Primary for jichi.*
- [TEST_INTEGRITY.md](../TEST_INTEGRITY.md): why "a test never observed failing
  has never been observed working", the rule behind §5.1's lesson. *Primary for
  jichi.*
- Karl E. Wiegers, *Peer Reviews in Software: A Practical Guide*, Addison-Wesley,
  2002: how to run a review and grade its findings. *Secondary.*
- The GNU C Library manual, "Basic Signal Handling" (`signal` versus `sigaction`,
  and why a strict C mode changes `signal()`'s semantics): the background of H1.
  *Primary.*
