# What to build next: a review of the tree after M712, and the loop no detector counts

*2026-09-23, M713. A review, a set of measurements and the findings they
produced. **Nothing in `src/` changes on this page.** It is written for two readers
at once: a **self-learner** who wants to watch a mature project decide what to do
next — including where the deciding went wrong — and a **developer** who will pick
up one of the items. The design decisions and the milestones that would build them
are the companion plan, [`plans/2026-09-after-m712.md`](../plans/2026-09-after-m712.md).
Every measurement below reads offline and calls no model; §8 has the commands.*

---

## Contents

0. [The answer first](#0-the-answer-first)
1. [How this review was done — and what it could not do](#1-how-this-review-was-done--and-what-it-could-not-do)
2. [Where the project stands](#2-where-the-project-stands)
3. [Findings in the product](#3-findings-in-the-product)
4. [Findings in the instruments and the corpus](#4-findings-in-the-instruments-and-the-corpus)
5. [Findings in the records](#5-findings-in-the-records)
6. [What I got wrong during the review](#6-what-i-got-wrong-during-the-review)
7. [For self-learners: the habits this review exercised](#7-for-self-learners-the-habits-this-review-exercised)
8. [Reproduce it yourself](#8-reproduce-it-yourself)
9. [What this page does not claim](#9-what-this-page-does-not-claim)

---

## 0. The answer first

**The machinery is in excellent health; the product has defects that no gate can
see, because every gate runs offline.** At `b284ab18` (M712, v0.10.0) the full
`make ci` is green on the development bench in **16 m 45 s**. The last ten days
were platform rows, test-tooling repairs and teaching material: of **57,848**
changed lines, **7.6 %** were in `src/`. `DEFERRED.md`'s own "what I recommend
doing next" list is done, except for decisions waiting on a corpus that ordinary
use is not producing — on this machine, jichi ran on **one day out of the last 48**.

What the evidence asks for next is **the agent's behaviour on real work**:

1. **A loop of *successful* calls is invisible.** The in-turn loop detector (M432)
   is fed failures only. M687's run made **200 tool calls with 0 errors** after it
   had the answer at call sixteen, and nothing but the tool-call cap stopped it. In
   this machine's (older) telemetry, **20 of 264 turns** *(corrected at M714: **13 of 264** — the stronger full-tier key; see [`analysis/2026-09-23-the-corpus-pilot.md`](2026-09-23-the-corpus-pilot.md) §7)* repeat one successful call
   at least five times with the same answer *and nothing changed in between*
   ([§3.1](#31-a-loop-of-successful-calls-is-invisible-to-every-detector)).
2. **An inferred constraint can forbid the task it was inferred from.** A prompt
   that *described* two tests that "do not compile" was read as "do not compile";
   the run refused its own builds for **21 minutes and 2,215,762 tokens** and was
   rolled back. It is the **third** misparse class of the same heuristic, each of
   which cost a whole unattended drive
   ([§3.2](#32-an-inferred-constraint-can-forbid-the-task-it-was-inferred-from)).
3. **Building an index costs about ten times the memory it needs** — one JSON
   node per float, 92 % of peak heap, and a peak above the total RAM of the two
   smallest machines the matrix says can run the agent loop
   ([§3.3](#33-an-index-build-materialises-one-json-node-per-float)).

And two smaller ones on the configuration front door: M709's "jichi chooses no
vendor" stops at two fallbacks (**read, not reproduced** — [§3.4](#34-m709s-no-house-vendor-stops-at-two-fallbacks-read-not-reproduced)),
and `config validate` exits **0** on a config that `doctor` rejects (**measured** —
[§3.5](#35-config-validate-says-ok-to-a-config-jichi-will-not-run)).

Underneath the decisions sit two instruments that **cannot answer their own
question even once a corpus exists** ([§4.3](#43-two-instruments-cannot-answer-their-question-even-with-a-corpus)),
and the records have slipped in three visible places: a behaviour fix shipped in
public v0.10.0 **with no changelog entry**, two living pages **deny a result the
project recorded**, and the reference bench runs **uutils coreutils** without a
single page saying so ([§5](#5-findings-in-the-records), [§2](#the-bench-itself)).

---

## 1. How this review was done — and what it could not do

**The host.** `threadwork`, the development bench. The operator asked for a review
("determine the next steps in design, and development") after several days of work
had landed on master.

**What was read.** The ROADMAP's standing block and the M687–M712 entries,
`DEFERRED.md` in full, the source around every finding cited here, and the commit
window since 2026-09-13. Four **read-only sub-agents** surveyed in parallel: the 75
files in `docs/plans/` and `docs/proposals/`, the 203 commits of the window, product
capability and code health, and the local corpus. They were forbidden to build, to
write, or to call a model. **Every claim of theirs that this page repeats was
re-checked by hand**, and two of them were wrong or overstated
([§6](#6-what-i-got-wrong-during-the-review)).

**What was measured.** Only offline: the full gate at `b284ab18`, the installed
binary's `doctor` and `config validate` against throwaway configs in a scratch
`HOME` with no key in the environment, and this machine's `~/.jichi.d` through the
repository's own `tests/measure/` scripts plus one new one
(`tests/measure/success_repeats.py`, [§3.1](#31-a-loop-of-successful-calls-is-invisible-to-every-detector)).
**No model was called.**

**What could not be done, and why it matters for every number below.**

- **The corpus the register cites is not on this machine.** `git reflog` says this
  checkout's last local commit was M636g (2026-09-16 17:31) and that M637–M712
  arrived in **one fast-forward at 09:06 today** — they were written on the other
  machine, and so were their measurements: M690's 114 journals, M710's 49,600
  events and its worst session, `0bf75213`, are all absent here. **Every rate on
  this page is this machine's**, and each one says which builds produced it.
- **One probe was refused.** To test [§3.4](#34-m709s-no-house-vendor-stops-at-two-fallbacks-read-not-reproduced)
  I wanted a loopback listener that records request headers while a fake key sits
  in the environment. The session's permission classifier refused it as
  credential exploration, and I did not work around the refusal. §3.4 therefore
  rests on reading the code, and is labelled that way; the reproduction belongs in
  the milestone that fixes it, with the tier's own `mockmodel` request capture.

---

## 2. Where the project stands

| | |
|---|---|
| **HEAD** | `b284ab18` — M712; `JC_VERSION` `0.10.0` (`include/jc_version.h`) |
| **Public tree** | advanced to the **M709 state** on 2026-09-22 — public `7180954` = private `02ef9c25`, tag `v0.10.0` on both remotes (recorded in `README.md`; not re-verified against the forges here) |
| **The gate, here** | `ci: OK (gcc + clang build/test, asan/ubsan, leakcheck, valgrind, curl-free link, faults, smoke, mutant, e2e)` — **16 m 45 s**, exit 0. Unit suite **13,598 checks / 0 failures** in every build (13,605 under `FAULT=1`); `smoke: OK (317 drivers, 1854 checks)`; `e2e: OK` |
| **Size** (the v0.10.0 recount) | 326 source files, ~110,600 lines; ~100,000 test lines; 513 English pages. `src/main.c` alone is **16,110** lines |
| **The last ten days** | 203 commits dated 2026-09-15 … 09-23, **M625–M712**; 98 carry a milestone number, 105 do not |
| **Where the lines went** | 57,848 changed: `src/` **4,384 (7.6 %)**, `include/` + `Makefile` 1,024, `tests/` + `scripts/` **17,563 (30.4 %)**, documentation **34,854 (60.3 %)** |
| **The installed binary** | was build `b059ae67` (M636g), **182 commits** behind, and printed the same `jichi 0.9.0` banner as the stale tree build; the operator reinstalled during the review and `jichi --version` now prints `build: b284ab18` |

What those ten days bought is real and is recorded in the ROADMAP: illumos green at
**317 of 317** drivers, both Windows layers rigged and driven, **19 of 19** emulated
architecture triples driven, the agent loop closing on a **96 MB** machine, a single
outcome derivation behind all five output surfaces (M688), and M709's removal of the
priced vendor default. None of this page argues that work was misplaced. It argues
that the next hours are worth more **inside the agent** than around it.

### The bench itself

The gate result above is also a platform datum nobody has recorded:

| | |
|---|---|
| OS | Ubuntu 26.04.1 LTS, Linux 7.0.0-34-generic, x86-64, 32 threads, 246 GiB |
| Toolchain | glibc 2.43, gcc 15.2.0, clang 21.1.8, valgrind 3.26.0, libcurl 8.18.0 / OpenSSL 3.5.5 |
| **Userland** | **uutils coreutils** — the Rust reimplementation Ubuntu adopted — installed 2026-04-23 (`coreutils-from-uutils`), and **upgraded 0.8.0 → 0.10.0 at 09:07 today**, half an hour before this review's gate ran |

On this machine `timeout`, `env`, `stat`, `mktemp`, `date`, `sort`, `tr`, `wc`,
`head`, `tail`, `cut`, `ls`, `sleep`, `od`, `dd`, `tee` and `nproc` are uutils;
`cp` and `mv` are diverted to GNU. **`timeout` is the tool the smoke runner kills a
hung driver with.** No file in `docs/`, `tests/` or `scripts/` mentions uutils, and
`PLATFORMS.md`'s development-box row describes a different machine (Ubuntu 24.04,
kernel 6.8, gcc 13.3, clang 18.1). This project's portability findings are mostly
*utility* behaviour — illumos `grep -o`, a pax header only GNU tar swallows,
`env -u`, a multi-character awk `RS` — so the userland is an axis in exactly the
sense the libc is, and the bench's has been unrecorded since April. It surfaced
indirectly: the operator's unprivileged `make install` failed — correctly — with
`chmod failed with error Operation not permitted (os error 1)`, which is not GNU
`install`'s wording.

---

## 3. Findings in the product

### 3.1 A loop of successful calls is invisible to every detector

**What.** jichi has two in-turn repetition detectors. M105's `jc_editwatch` notices
a model redoing the same **edit**. M432's `jc_toolloop` notices a model repeating a
**failing** call — and only a failing one: the branch that feeds it is
`if (res.is_error)` in `src/chat/jc_agent.c` (the comment above it reads *"FAILED
calls only"*). A model that re-issues a call that **succeeds**, and gets the same
answer back, meets no detector at all. The only bound is `--max-tool-calls`.

**The incident.** M687, 2026-09-20, on the other machine, a current build. The task
was "which git branches are merged into master". The model had the answer by call
sixteen and then kept re-querying the same branches. The run ended:

```
[jichi] checked: verify green · 200 tool calls, 0 errors (0 refused by a fence) · ...
[envelope] verified ok (tokens 4,887,733, tool calls 200)
```

**0 errors** is the whole story: there was nothing for M432 to count. M687 fixed
what the footer *said*; nothing yet changes what the loop *does*.

**How common is the shape?** `tests/measure/success_repeats.py` (new in M713) reads
telemetry `tool_call` events and counts, per turn, a successful call whose
*(tool, argument summary, output size)* equals an earlier one's. It reports two
counts, because the first over-reads: edit → test → edit → test with the same
passing output is re-verification, not a loop. **"Unchanged"** resets a turn's
counts after every successful mutating call, so it counts only *the same question,
the same answer, and nothing changed in between*. On this machine (84 files, 264
turns with tool calls, 11,276 calls; `read_file` and mutating tools excluded):

| a successful call repeats | raw (whole turn) | **unchanged** (nothing mutated between) |
|---|---|---|
| ≥ 3 times | 72 turns (27.3 %) | **51 (19.3 %)** |
| ≥ 5 times | 34 (12.9 %) | **20 (7.6 %)** |
| ≥ 10 times | 10 (3.8 %) | **8 (3.0 %)** |
| *for comparison: a **failed** call repeats ≥ 3 times — M432's domain* | *19 turns* | |

The worst turns: `run_tests` **55** times in a 111-call turn (**33** of them with
nothing edited in between), one shell command **47** times, `search_code` **43**
times, `list_files` 20 times.

> **Corrected at M714, the same day — the table above over-counts.** It keyed every
> event on the argument *summary* and the output's *byte count*, but **7,313 of the
> 11,276 events carry the `full` tier**, and keyed on their whole arguments and a
> hash of the result the counts fall:
>
> | a successful call repeats | raw | unchanged |
> |---|---|---|
> | ≥ 3 times | 52 | 33 |
> | ≥ 5 times | 22 | **13 (4.9 %)** |
> | ≥ 10 times | 7 | 6 |
> | *a failed call ≥ 3 times* | *16* | |
>
> Equal summaries and equal byte counts were hiding different calls — `search_code`
> drops out of the worst-turn list entirely once its full arguments are compared.
> The finding stands; its size was overstated. The original table is kept because a
> correction that erases what it corrects teaches nothing
> ([`2026-09-23-the-corpus-pilot.md`](2026-09-23-the-corpus-pilot.md) §7).

**How to read that table — the honest limits.**

- **It is a historical rate.** 11,059 of the 11,276 events carry **no build stamp**
  (pre-M290 builds), every hit predates M432 (2026-08-14), and the post-M432 window
  on this machine is **10 turns** — the script prints `NOT EVIDENCE` for it, and
  reports 0 hits there, which is not evidence of anything either.
- **It is a proxy.** At the default `metrics` tier the event carries an argument
  *summary* (≤ 160 characters), not the arguments; equal byte counts are not equal
  bytes; and a shell command can change the tree without the log saying so, so
  shell calls never reset the "unchanged" count.
- So the table says the shape was **common**, and M687 says it **survives on a
  current build**. Neither says how common it is today. That number lives on the
  other machine, and fitting a detector's thresholds to it is step 0 of the plan's
  D1 — the way M432's thresholds were fitted to two corpora before a line was
  written.

**Why it matters.** On the free local models this project uses, a loop costs
wall-clock rather than money — M687 spent **4,887,733 tokens over 200 tool calls**,
about 24,000 per call, most of them after the answer was known — and an unattended
run's exit code is its whole interface. A loop that ends at the cap is indistinguishable, to a caller
reading `$?`, from a run that finished.

### 3.2 An inferred constraint can forbid the task it was inferred from

**What.** In `--auto`, jichi reads constraints out of the request and **enforces**
them: *"Auto-adopt in `--auto` (default on) … This is the mode the whole feature
exists for"* ([CONSTRAINTS.md](../CONSTRAINTS.md)). Interactive sessions do not
auto-adopt, precisely so that a casual mention does not become a hard rule.

**The incident** (commit `1d31473d`, 2026-09-21, a supervised drive on zigodot). The
prompt opened *"Two tests at the end of analyzer.zig do not compile:"*. `compile`
is a token of the `build` command key and `do not` is a negation cue, so the run
announced `do not run build commands (make / cmake / compile / ...)` and refused its
own `zig build test`. It ran **21 minutes, 42 tool calls, 2,215,762 tokens**, ended
`verify_failed`, and the envelope rolled the work back. Held constant with one
scripted tool call, only the prompt changing:

```
"Please check the toolchain."                          0 refused by a fence
"The tests do not compile. Please check the toolchain."  1 refused by a fence
```

**The pattern, not the incident.** This is the third misparse class, and each cost
a whole `--auto` drive:

| | the misparse | cost |
|---|---|---|
| **M168** | descriptive prose read as an instruction | a ~1.5 M-token drive |
| **M207** | a prohibition about **one** file ("do not edit the pipeline") put the whole run in read-only | a ~1.5 M-token drive |
| **`1d31473d`** | a *subject* in front of "do not" — a statement about the code — read as an order | 2,215,762 tokens, rolled back |

Between them, M433 measured **4 of 7** runs silently adopting a constraint from
descriptive prose and built `jichi brief-check`, which pre-flights a brief and
prints every constraint it would infer **and the line that produced it** — so the
scanner is already exposed as an offline instrument. Three incidents, three guards,
one heuristic: each fix narrows the phrasing that fires, and none makes the *next*
false positive visible when it happens during a run. The loop detector did fire in
the third one (*"verify stuck on the same error (2x)"*), but its advice is about
the verifier; nothing told the model, or the reader of the footer, that the
refusals came from a rule inferred from the prompt.

**Why it matters.** The false positive fires in exactly the mode that has no reader
for the `WARN` line, and it looks to the model like a fence — the thing it has been
taught not to argue with.

### 3.3 An index build materialises one JSON node per float

M700's massif page ([2026-09-22-index-embedding-allocation-massif.md](2026-09-22-index-embedding-allocation-massif.md))
established it: the embeddings response is parsed into a full JSON tree, **one node
per float** — **1,509,888** numbers, **≈ 184 MB** at ~128 bytes a node, **92 % of
peak heap**, in `jc_embed_parse` (`src/net/jc_embed.c`). A plain
`jichi index --reindex` of this repository (3,045 files, 16,557 chunks) peaked at
**170,048 kB** resident, climbing monotonically. The page recorded the direction of
a fix — read the vectors straight into the flat `f32` buffer the index already
keeps — as *"not proposed as work"*, and **no register row points at it** (0
matches in `DEFERRED.md`).

**Why it matters here.** The matrix's proudest small rows ran the agent loop in
**160 MB** and **96 MB** of total RAM. An index of this repository peaks above both.
`codebase_search` builds the same index.

### 3.4 M709's "no house vendor" stops at two fallbacks (read, not reproduced)

M709 removed three built-in defaults that together pointed a fresh install at a
priced model nobody had chosen. Two more assumptions of the same kind remain:

| where | what it does when the model entry names **no** provider |
|---|---|
| `src/config/jc_config.c:72-75` (`resolve_key`) | with no `apiKey`/`apiKeyEnv`, takes `OPENAI_API_KEY` if the provider is `"openai"`, **otherwise `ANTHROPIC_API_KEY`** — including when the provider is unset or a string jichi does not know |
| `src/provider/jc_provider.c:21-27` (`jc_provider_create`) | `"openai"` → OpenAI dialect, `"anthropic"` → Anthropic; anything else is **guessed from the model id** (`gpt`/`openai` substring → OpenAI) and otherwise **defaults to the Anthropic dialect** |

**Measured:** for an entry with a model id and an endpoint but no provider, the
installed v0.10.0's `doctor` says **nothing** about the provider or the dialect —
its only provider line is `ok provider transport: https (or loopback)` — and
`config validate` prints `OK`.

**Inferred, not observed:** such an entry would speak the Anthropic dialect to
whatever server it names and attach `ANTHROPIC_API_KEY` from the environment, if
one is set. Every shipped example and `CONFIG_TUTORIAL.md` §0a set `provider`, and
`setup` writes it, so the path needs a hand-written config. But it is the same "a
choice nobody made" that M709 removed from three other places, and a key sent to a
host it does not belong to is not a cost that can be taken back.

### 3.5 `config validate` says OK to a config jichi will not run

**Measured** on the installed v0.10.0 (`build: b284ab18`), in a scratch `HOME` with
no key set, against `{"models":[{"name":"a"}]}`:

```
$ jichi --config cfg.json config validate      -> OK: .../cfg.json
                                                   1 model(s); active: ?     exit 0
$ jichi --config cfg.json doctor                -> x no model is configured   exit 1
```

`DEFERRED.md` carries this question as *"whether `config validate` should surface
posture warnings"*, and its premise is that such a config's active model *"becomes a
priced built-in default"*. **That has been false since M709**, which removed the
default. What is left is starker: `validate` approves a config the program refuses
to run. The row's revisit condition was *"when someone reports the OK as
misleading"*; this page is that report.

### 3.6 `main()`'s dispatch chain keeps producing one class of defect

`src/main.c` dispatches its subcommands through **54** `strcmp(args.pos[0], …)`
branches, each with its own prologue and epilogue. Three incidents, recorded in the
code's own comments, share that root:

| | what happened |
|---|---|
| **M444** | the autonomy envelope was armed after the dispatch that needed it |
| **M608** | secret redaction was armed **~1,100 lines below** the dispatch chain, so a subcommand's child ran with the registry empty — `brief-check --verify CMD` handed `CMD` the configured key variable intact |
| **M693** | **28** early-return exits each freed a hand-maintained subset, and `jichi doctor` leaked under LeakSanitizer |

Each got a local fix and a lint. The shape that produces the class is unchanged.

---

## 4. Findings in the instruments and the corpus

### 4.1 Four decisions wait on a corpus this machine does not have

| decision (`DEFERRED.md`) | instrument | its floor | this machine | verdict |
|---|---|---|---|---|
| should a capped one-shot exit non-zero? (item 7) | `capped_oneshot.py` | 20 capped runs | 167 journals, 126 completed, **0** with `stop_reason` | `NOT EVIDENCE` |
| `--strict-green`'s tracked-path rule | `strict_green_fp.py` | none numeric | 38 scoped runs, 33 ok, 2 flagged; **0** records carry `tracked` | cannot answer |
| the compaction latch's re-arm rule | `compaction_pressure.py` | > 12 post-latch pressed turns | 8 passes, **2** in the core, one day | not met |
| a `doctor` re-read advisory | `reread_ratio.py` | 50 `read_file` calls | **0** `--output jsonl` streams | nothing to measure |

Every journal here was written by a build older than the fields these rows need
(stamps `0.9.0` or none). The latest event of any kind on this machine is from
2026-09-16 17:00.

### 4.2 Ordinary use is not producing a corpus

Journals on this machine by month: **June 89, July 31, August 10, September 2**.
Counting every journal and telemetry event since 1 August, activity falls on three
days — 2026-08-04, 08-05 and 09-16 — so nothing between 2026-08-06 and 2026-09-15,
and nothing since. Most model
calls went to `jlu/qwen3-coder-next` (5,639 calls in 157 sessions). The other
machine's corpus is larger, but a register row that waits for "more journals" is a
plan only if something produces journals. None of the four rows above names what
would.

### 4.3 Two instruments cannot answer their question even with a corpus

- **`capped_oneshot.py`** exists to ask about `--no-session` one-shots, which have no
  next prompt to resume from. But the journal's `start` event (written in
  `src/chat/jc_agent.c` beside `jc_env_journal_begin(app->env, "start")`) records
  budget, deadline, tool-call cap, verifier and edit scope — **nothing that marks a
  one-shot** — and the `end` event records nothing that says **a final answer was
  produced**. The script therefore counts every capped journal, keeps only the last
  `start`/`end` pair of a multi-turn file, and uses `no_changes` as its "answered"
  proxy, which its own output says is *"the nearest proxy the journal carries"*.
  Twenty capped runs recorded this way would still not answer the question the row
  asks.
- **`reread_ratio.py`** reads only `--output jsonl` streams, because ordinary
  telemetry cannot tell paging from re-reading: at the default tier a `read_file`
  event's summary is the **path alone** (`args_full` is written only at the `full`
  tier, `src/chat/jc_agent.c`, beside `jc_eventlog_full`), and keying a read metric
  on the path is the trap M287 retracted a finding over. A summary that also carried
  **offset and limit** — two numbers, no content — would let every ordinary session
  feed the measurement.

### 4.4 The corpora decisions rest on live on one machine, and get pruned

M710's worst session is not here. M700's massif page opens by recording that the
four sessions it wanted to replay **had already been pruned** from the session
store. The craft A/B's blinded pack was lost the same way (M545): ~3.73 M input
tokens of data in a `results/` directory nothing archived. A decision whose evidence
cannot be re-read is a decision nobody can revisit — and this register is built on
revisiting.

### 4.5 The capability bench has no recorded run since July

`tests/bench/` is the fixed dogfood suite: 11 tasks, 21 points, graded by each
spec's own verifier. Its only analysis page is
[2026-07-27-local-gpu-bench.md](2026-07-27-local-gpu-bench.md); since then its runner
and corpus changed only for licence headers, and its results directory is
git-ignored. Every gate runs offline, and the driven task proves only that the loop
**closes once**. So between v0.9.0 and v0.10.0 **nothing recorded whether the agent
became better or worse at tasks.**

---

## 5. Findings in the records

### 5.1 A behaviour fix shipped in v0.10.0 without an entry

`1d31473d` (§3.2) changed `src/chat/jc_constraint.c` (+63) and
`tests/test_constraint.c` (+29). It is an ancestor of `02ef9c25`, the published
state. `CHANGELOG.md` had **no line for it** and the ROADMAP no entry. It landed with
nine other commits authored on 2026-09-21 on a branch and re-committed on 2026-09-22
between 17:15 and 17:19 — the grounded-discourse page and its grader, tasks 85 and
86, the Godot page, a register walk among them — and **none of the ten carries a
milestone number**. One of them (`bea447d3`) says so itself: *"No ROADMAP entry and
no milestone number… Both are still owed when it lands."* **M713 adds the changelog
line, marked as recorded late; the numbers stay owed** (the plan says why).

### 5.2 Two living pages denied a result the project had recorded

[`SESSION_RUNBOOK.md`](../SESSION_RUNBOOK.md) §4b — the page every session starts
from — and [`READING_OPEN_SOURCE.md`](../READING_OPEN_SOURCE.md) said the self-hosting
write slice had **never** completed a real task end to end, *"criterion 2 … openly
unmet"*. The pack's own README records **"Criterion 2 met on the re-run
(2026-08-03)"**: M271, one `/add-test`, **131k of 800k tokens, 8 tool calls**,
verifier green, the shown-red step handed to a human exactly as the command
requires. `git log -S` dates the two sentences to M513 and M514, **2026-08-21 —
eighteen days after the run that met the criterion**. M711's sweep of 219 living
pages did not find them. **Corrected in place at M713**, and precisely: criterion 2
was met **once**, under the fence as it stood before M517 narrowed it; this review
found no later write-slice run in the ROADMAP; and criterion 1 rests on thin ground
(M515 records it as met *"on one diff"*).

### 5.3 Design documents whose status line says "not built" after they were built

| file:line | says | what the tree shows |
|---|---|---|
| `plans/2026-09-files-and-structures.md:3` | "**Status:** planned, not built" | `FILE_HANDLING.md`, `DATA_STRUCTURES.md` and tasks 76–80 exist |
| `plans/2026-09-language-course.md:11` | "nothing here is built yet" | the course shipped (M671, M674, M675) |
| `proposals/2026-07-curriculum.md:3` | "Nothing here is built" | the curriculum shipped (M173–M177) |

A sub-agent's survey counted **about twenty** such lines across the 75 plan and
proposal files; **the three above were re-checked by hand and the rest were not.**
M711 left `plans/` and `proposals/` out of its sweep on the principle that *"a dated
record is correct as a record"* — right for a measurement, wrong for a status line
written in the present tense, which is a claim about today every day it is read.
Not fixed here: whether status lines should be dated, maintained, or linted is a
policy question (plan, operator decisions).

### 5.4 The v0.10.0 publication is recorded only in the README

`README.md` records the M709-state cut (public `7180954` = private `02ef9c25`, 2,063
files compared byte for byte, tag `v0.10.0` on both remotes). The ROADMAP records the
cut before it (M691) and not this one. That gap is how this review first got the
public state wrong ([§6](#6-what-i-got-wrong-during-the-review)); the M713 entry
records it.

---

## 6. What I got wrong during the review

Kept because a review that reports only what it fixed is the shape this project
exists to distrust.

1. **I told the operator the public tree lacked M709.** I had searched the ROADMAP
   for publication records, found M691's cut of the M690 state, and reported the
   *absence* of a later one as a fact. The README records the M709-state cut with
   its tag. **A negative claim — "X has not happened" — is a claim about every place
   X could have been recorded, and I had checked one.** A sub-agent's report
   contradicted me; I verified before writing anything on it.
2. **I started the full gate without saying so, and the operator then wanted to
   install.** A `make install` during `make ci` could have copied a half-built or
   sanitizer binary into `/usr/local/bin`. Stopping it took two steps, not one: the
   harness stopped the shell, but a `make … run_tests` child ran on until it
   finished by itself, and it was `scripts/preflight.sh` — not the stop command's
   "success" — that showed it. The runbook's *"stop that job **and confirm it
   stopped**"* earned its place again.
3. **My first repetition count keyed `read_file` on its path** — the paging trap
   M287 had already documented. The committed script excludes `read_file` by
   default and **prints what it leaves out**.
4. **My first byte-equality count over-read legitimate re-verification.** The
   committed script reports the stricter *unchanged* count beside the raw one, and
   §3.1 leads with it.
5. **A sub-agent overclaimed, and I nearly repeated it.** It reported that
   `reread_ratio.py`'s limit is *"the script's parser, not the data"*, because
   telemetry carries `read_file` paths. At the default tier those are summaries
   without offset or limit, so the register row was right; the finding became
   §4.3's narrower one.
6. **My first draft of §3.2 miscounted the history it was arguing from.** It said
   M168 fixed *"the first false-positive class (seven measured runs …)"* and
   called `1d31473d` the second. The *4 of 7* figure is M433's, not M168's, and
   there were three classes, not two — M207's one-file prohibition that made a
   whole run read-only sits between them. Re-reading the changelog entry I was
   paraphrasing caught it, and it also turned up the better instrument:
   `brief-check` already exposes the scanner offline. The error made the argument
   *weaker* than the evidence, which is the less dangerous direction and still an
   error.

---

## 7. For self-learners: the habits this review exercised

Each finding above is an instance of something this project teaches. The left
column is the habit; the right is where to read it properly.

| habit | where it showed up here | where it is taught |
|---|---|---|
| **Audit the universe, not the result.** A green detector tells you its universe is clean. | M432's universe is *failed* calls; the loop that cost 4.9 M tokens was made of successes (§3.1) | [TEST_INTEGRITY.md](../TEST_INTEGRITY.md) §"Audit the universe"; [TESTING_TUTORIAL.md](../TESTING_TUTORIAL.md) §6 |
| **A negative claim needs every source.** | §6.1 — one page searched, absence reported | the same rule, turned on a review |
| **A later change can void an earlier reason.** | M709 made the `config validate` row's premise false (§3.5) | [DEFERRED.md](../DEFERRED.md) §"Check the checkable part of a reason" |
| **The bench is part of the experiment.** | uutils under the whole gate since April, recorded nowhere (§2) | [SESSION_RUNBOOK.md](../SESSION_RUNBOOK.md) §5, "the bench reference" |
| **Evidence exists only while it is kept.** | pruned sessions; the lost craft A/B pack (§4.4) | ANECDOTES, M545 |
| **Measure the population before building the gate.** | the two counts of §3.1, before any detector exists | `CLAUDE.md`, "Tests — the rules" |
| **Say which claims you read and which you reproduced.** | §3.4 is labelled "read, not reproduced" | [GROUNDED_DISCOURSE.md](../GROUNDED_DISCOURSE.md) |
| **Confirm a stop by its effect.** | §6.2 — the harness said stopped; preflight said busy | [SESSION_RUNBOOK.md](../SESSION_RUNBOOK.md) §0 |

**Three read-only twins** — experiments that need no model, no key and no special
hardware, only a jichi checkout and an installed `jichi`:

1. **Count your own repeats.** Run the §8 telemetry command against your own
   `~/.jichi.d`. With little or no telemetry it prints `NOT EVIDENCE` — which is the
   first thing worth learning from it: a rate from ten turns is not a rate.
2. **Watch two front doors disagree.** Write the one-line config from §3.5 into a
   scratch directory, point a scratch `HOME` at it, and run `config validate` and
   `doctor` side by side. Then add a `"model"` and see which one changes its mind.
3. **Ask which userland you have.** `ls --version | head -1` and the target of
   `command -v timeout` tell you whether your coreutils are GNU, uutils, BusyBox or
   a BSD's — and whether a result someone else measured was measured on the same
   tools you have.

---

## 8. Reproduce it yourself

Every block reads; none writes to the repository or calls a model. Commands that
start `jichi` use a scratch `HOME` so your real configuration is neither read nor
changed.

```sh
# in the jichi checkout -- where the recent work came from
git reflog --date=iso | head -3
git log --since=2026-09-13 --oneline | wc -l
git log --since=2026-09-13 --numstat --format= | awk '$1 ~ /^[0-9]+$/ {n=$1+$2; t+=n; if ($3 ~ /^src\//) s+=n} END {printf "%d of %d changed lines in src/\n", s, t}'
```

```sh
# in the jichi checkout -- the corpus measurements (offline, read-only)
python3 tests/measure/success_repeats.py                        # all of ~/.jichi.d/telemetry
python3 tests/measure/success_repeats.py --since 2026-08-14     # post-M432 only
python3 tests/measure/capped_oneshot.py
python3 tests/measure/strict_green_fp.py
python3 tests/measure/compaction_pressure.py
```

```sh
# anywhere -- two front doors, one config (needs an installed jichi, no key)
j=$(command -v jichi) && d=$(mktemp -d) && mkdir -p "$d/home"
printf '{"models":[{"name":"a"}]}\n' > "$d/cfg.json"
env -i PATH=/usr/bin:/bin HOME="$d/home" "$j" --config "$d/cfg.json" config validate; echo "validate: $?"
env -i PATH=/usr/bin:/bin HOME="$d/home" "$j" --config "$d/cfg.json" doctor > "$d/doctor.txt"; echo "doctor: $?"
grep 'model is' "$d/doctor.txt"
```

```sh
# in the jichi checkout -- the detector's universe, and the two fallbacks
grep -n 'FAILED calls only' src/chat/jc_agent.c
grep -n 'getenv("ANTHROPIC_API_KEY")' src/config/jc_config.c
sed -n '12,28p' src/provider/jc_provider.c
```

```sh
# anywhere -- which userland is under your gate
ls --version | head -1
readlink -f "$(command -v timeout)"
```

---

## 9. What this page does not claim

- **Not** that successful-call loops are as common today as §3.1's table: that table
  is pre-M432 and mostly pre-M290, and the post-M432 window here is ten turns.
- **Not** that jichi sends a key to the wrong host today: §3.4 is read from the code,
  and its reproduction was not run.
- **Not** that the other machine's corpora say what this machine's say, in either
  direction.
- **Not** that about twenty design documents carry stale status lines: three were
  checked.
- **Not** a capability comparison with other coding agents. A sub-agent surveyed
  one from the documentation — plan mode, subagents, hooks, sandboxing and the
  rest — and it was not re-verified line by line, so it is not reproduced here.
  [`COMPARED.md`](../COMPARED.md) is the page that maintains that comparison.
- **Not** an order of priorities for the operator. The companion plan recommends one
  and says why; the choice between the product band, the platform band and the
  teaching band is his.
