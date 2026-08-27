# Instruments that lie: a session where the tools were wrong more often than the code

*2026-08-16. Hardware rows, a device fleet, three dogfood runs on two other
projects, and eleven landed changes. Written to the convention this directory
uses: what was measured, what was decided and what it rejected, and — at the
end — what this note does **not** license.*

## The one-sentence finding

Across two days of work, **the code under test was rarely the thing that was
wrong.** What failed, repeatedly, were the instruments: probes, gates, monitors,
supervisors, checks, and the summaries of agent runs. Every one of them shared a
single property, and it is worth stating as a definition:

> **An instrument is dangerous when its failure mode is indistinguishable from
> its answer.**

A probe that reports "feature absent" when the compiler is missing. A monitor
that reports "still running" when it is matching itself. A check that reports
"reachable" because `head` exits 0 on empty input. A gate that reports "done"
because the file still parses. A proof that reports "fails without the fix"
while testing a binary that still contains it.

None of these are exotic. All of them look exactly like success.

---

## 1. The catalogue, with what each one cost

| # | The instrument | What it reported | What was true | Cost |
|---|---|---|---|---|
| 1 | `command -v elixir` / `shutil.which` | toolchain present | an asdf **shim** that cannot run under the test harness's private `$HOME` | red CI, 3 wrong diagnoses |
| 2 | a bare `except Exception` in a probe | "toolchain absent" | *the probe itself* raised `NameError` | 4 working courses silently disabled, run still `OK` |
| 3 | `grep -q` over an accumulating journal | "this run changed nothing" | run 1's honest verdict, inherited forever | 2 wrong theories, jichi accused of lying 3× |
| 4 | a monitor reading `gate.txt` | "the run failed" | a *dead* run's file, 13 minutes stale | a discarded device row |
| 5 | `pgrep -f "<pattern>"` | "the job is still running" | the pattern matched **the watcher itself** | 5 incidents, 2 shells killed outright |
| 6 | `curl -s … \| head -c 80 && echo ok` | "the VPN is exposed" | `head` exits 0 on empty input | a false security alarm |
| 7 | `python3 -m py_compile` as a gate | "the conversion is done" | a script that still points at the wrong checkout also parses | 2 defects shipped past the gate |
| 8 | a failed rebuild during a proof | "it fails without the fix" | the **old binary**, still containing the fix | proof void, twice |
| 9 | an agent's final summary | "both files created" | correct `write_file` calls emitted as **prose** | an empty workspace reported as success |
| 10 | a hand-written verb table | 6 verbs `OK` | one had returned `rc=1` on the line above | — (caught immediately) |

Rows 5, 6, 8 and 10 were committed **by the author of this note**, while
cataloguing rows 1–4. That is not incidental. It is the argument of the whole
document: this failure mode is not a competence problem, it is a *shape* that
recurs faster than care can suppress it.

---

## 2. Design decisions, and what each one rejected

Each decision below is recorded with the alternative it turned down, per
`DECISIONS.md`'s rule that a choice with no rejected alternative was not a
choice.

### 2.1 `--max-tool-calls` bounds attempts, not permitted work

**Decided:** a call refused by any gate still spends the cap.

**Rejected:** keeping the depth-0/permitted-only meaning and documenting it. A
cap that counts only what the agent was *allowed* to do gets weaker the more the
agent is refused — probe P13 measured a model repeating one forbidden write five
times, journaling `tool_calls: 0`, and ending `outcome: ok` having spent its
entire token budget. M429 addressed the same thrash by *telling* the model; that
needs the model's cooperation, and `HARDENING.md` §6b is explicit that the
defences worth having are the ones that do not.

**Also rejected:** replacing the old meaning. Two honest meanings were sharing
one counter. A delegate reporting "0 tool calls **and** a denied write" to its
parent is telling the truth, and a count of attempts would destroy it. Both are
kept: `tool_calls` (attempted) bounds the run, `tool_calls_executed` (ran) is
what a delegate reports — and **the pair, journaled together, is the
machine-readable signature of a run thrashing against a gate**, which neither
number gives alone.

### 2.2 An explicit `--edit-scope` outranks an *inferred* read-only

**Decided:** a path inside an operator's typed `--edit-scope` is exempt from a
read-only constraint that was *guessed* from prose. An **authored** one still
binds.

**Rejected:** only naming the conflict in the refusal. It fixes the diagnosis
and not the run — the operator still gets no report, and their flag still loses
to a keyword scan. Also rejected: exempting the whole run (that disarms the
constraint everywhere instead of on the declared path), and letting an explicit
scope beat an *authored* read-only (two explicit declarations in conflict are
the operator's to resolve; silently picking one hides the conflict).

**The principle came from the module's own header**, which already called an
inferred constraint "a guess" and capped its blast radius for that reason. A
guess outranking a typed flag inverted the module's own stated principle.

### 2.3 The fleet pushes; it does not share a queue

**Decided:** a supervisor assigns work over SSH, one connection per device.

**Rejected:** extending `AUTONOMOUS_LOOPS.md`'s shared-queue topology across
machines. Its claiming rule is an atomic `rename(2)`, which is atomic *within a
filesystem*; a Pi, a tablet and a proot guest share none. Manufacturing one with
NFS or sshfs would put a network filesystem's rename semantics underneath the
only thing keeping two agents off one task. Push **removes** the coordination
problem instead of solving it.

**Corollary decided:** the **deadline** scales per device by its measured
build-time multiplier; the **token budget** does not. A token is the same work
everywhere; wall-clock is not. A fleet with a uniform deadline kills its slowest
member and calls it a timeout.

### 2.4 Devices never hold credentials

**Decided:** devices use a keyless model on the LAN; work needing a keyed model
runs on the workstation.

**Rejected:** pushing the API key to each device. The gateway turned out to be
publicly reachable (a keyless request from the Pi returns 401, not a connection
failure), so a device *could* call it directly — if it held the key. A fleet
member is a borrowed tablet or a board on a shelf, and a credential written to
its disk outlives the run, the task, and usually the operator's memory of having
put it there. **Deferred, not rejected:** a key-injecting relay on the
workstation, worth building only if a fleet ever needs a model the LAN cannot
serve.

### 2.5 The config accepts the JSONC its documentation shows

**Decided:** strip comments and trailing commas in jichi's own config loader.

**Rejected:** removing the comments from 15 documented examples. The machinery
already existed, was unit-tested, and was already used to read *other tools'*
configs — lenient with Claude Code's files and strict with its own, while the
documentation had long assumed otherwise. This is a strict widening: stripping
comments from a file that has none is the identity.

**The guards are the point.** Two of the four new checks assert that a genuinely
malformed config is *still* rejected with the same message, and that the `//`
inside every `http://` apiBase survives. A widening that swallowed real syntax
errors, or ate a URL, would be far worse than 15 unpasteable examples.

### 2.6 What was deliberately *not* done

- **The seeded fuzz harness.** Its register entry gates it on "a pure-core
  defect that seeded input would plausibly have caught". This session produced
  none — every defect was in tests, rigs, docs, or composition. Building it
  anyway would have overridden the register's reasoning because the work
  happened to be available.
- **The `--strict-green` default flip.** Reserved to the operator: it changes a
  stable-tier contract. What was supplied is the evidence it asked for (a second
  project's corpus, 0/2 on tight scopes, combined 0/23) — plus the discovery
  that the measurement's denominator was inflatable, below.
- **Porting zigodot's agent server to `std.Io`.** The project's own
  `MODEL_KNOWLEDGE.md` records the available models as writing Zig **0.11**
  dialect, 0/4 probes compiling under 0.16. Agent edits *inside* existing 0.16
  code have worked here; a new transport is novel API surface with no training
  signal, and it would become the trust boundary between jichi and the engine.

---

## 3. What the agent runs actually showed

Three unbudgeted headless runs against two other repositories, every diff read
line by line.

**Mechanical facts: essentially perfect.** 13/13 line counts exact. 52/54 symbol
names resolved. Correct C89. Correct Zig matching a reference implementation
verbatim. Fences held every time — nothing was ever written outside
`--edit-scope`.

**Specific claims: fabricated in every single run.**

| Run | The false claim |
|---|---|
| chrtext docs | rewrote `jlu_continue` as `jichi_continue`; the binary is `jichi` |
| zigodot docs | put `$GODOT_SRC` inside a **tool-call** block, where no shell expands it |
| zigodot module map | `runFile` and `AnalyzerError` — neither exists anywhere in `src/` |

Three for three. Each looks right. Each would have shipped on the summary, and
none survived the diff. **The summary was reliably wrong in exactly the way the
diff was reliably right.**

Two further observations that change how to brief an agent:

- **Name the invariant, not the index.** "Use `parents[1]`, since they live in
  `scripts/`" is correct for `Path(__file__)` and wrong the moment the agent
  introduces `SCRIPT_DIR = ….parent` — which it did, in three files, resolving
  the repo root one level too high. Say "the root is the parent of `scripts/`".
- **Budget only when measuring the budget.** A 250k cap said the agent could not
  finish a task and left a red gate; the same task unbudgeted finished green in
  **756k tokens**, iterating to a passing verifier on its own. The cap was
  measuring the cap.

---

## 4. Recommendations for hardening jichi

Ordered by leverage, with the evidence each rests on.

1. **Make "a gate that cannot fail for its requirement" a lint, not a habit.**
   Every gate this session that certified nothing was syntactically valid: a
   `py_compile` gate for a path-conversion task, a `command -v` gate for a
   toolchain. The generalisable rule is that a verify command must be shown to
   fail on the *unfixed* tree; jichi already knows how to run a verifier and
   already journals its exit code, so the missing piece is a *rehearsal* — run
   the gate against the pre-change tree once and record that it was red. This is
   the open `DEFERRED` row on gate rehearsal, and this session is its best
   argument yet.
2. **Journal what a run was allowed to touch, not just how much.** Done here for
   `edit_scope_globs`, after discovering that a count could not distinguish
   `--edit-scope AGENTS.md` from `--edit-scope '**'` — and that a corpus of the
   latter reports a perfect false-positive rate while proving nothing. Apply the
   same test to every other counted field: *can this number be inflated without
   anyone noticing?*
3. **Ship the diff, not the summary, as a headless run's primary artifact.**
   3/3 runs produced a false claim in prose and a correct change in the diff.
   jichi already computes unified diffs for its own previews; a headless
   `--auto` run that ends by emitting the diff it produced would put the
   trustworthy artifact where the reviewer looks first.
4. **Detect an under-declared context window.** Implemented: when mid-turn
   compaction falls short *and* the server has accepted a request larger than
   the declared limit, say so and name `contextLength`. Worth extending to
   `doctor --live`, which already makes a real call and could compare
   `prompt_tokens` against the declared window without any new machinery.
5. **Make every build step reachable by the gate.** zigodot's agent server
   rotted through a toolchain migration because it is never `installArtifact`'d
   and `zig build test` never touches it — the identical mechanism its own
   `conformance.zig` documents in its header. Twice in one repository makes it
   structural. jichi's equivalent question: *which of our targets does `make ci`
   never compile?*
6. **Prefer a lint to an audit** — the project's own maxim, re-earned. Every
   class found by grep in this session (`/tmp` fixtures, `command -v` guards,
   hardcoded checkout paths, unpasteable doc examples) was found *because
   someone grepped*, and each had survived multiple careful readings.

---

## 5. For learners

The step-by-step material is not here; it is in the places the curriculum
already reads:

- **[ANECDOTES.md](../ANECDOTES.md) #54–#56** — the three war stories, in the
  house format (symptom → dead ends → root cause → lesson). #54 is the toolchain
  that was on PATH and could not run; #55 is the supervisor that accused jichi
  of lying three ways; #56 is the proof that tested the fix it had removed.
- **[SESSION_RUNBOOK.md](../SESSION_RUNBOOK.md)** — the fixed order of execution
  and the process rules, each with the number of times it was broken *in this
  session*. Prose discipline failed; `scripts/preflight.sh` refuses a busy tree
  and `scripts/pin-driver.sh` removes the whole class of "the build deleted the
  binary I was driving with".
- **[curriculum/09](../curriculum/09-the-agent-is-sometimes-wrong.md)** — the
  module whose thesis this session re-proved: *the convincing surface and the
  true state are different objects; only a check connects them.*

---

## 6. What this note does not license

- **It does not say local models are unusable.** One model (`qwen2.5-coder-14b`
  via LM Studio) emitted *perfectly correct* `write_file` calls as prose and
  could not drive the loop; another on the same server (`qwen3.5-9b`) did
  native tool calls and worked. `doctor --live` distinguishes them in one
  request. The lesson is *probe the model*, not *avoid the models*.
- **It does not say the agent runs were unsuccessful.** All three landed real,
  reviewed, gate-green commits in other people's repositories. The correction
  rate was one false claim per run, all caught by reading the diff — which is a
  workable ratio, and only workable *because* the diff was read.
- **It does not close the compaction rows.** 7/7 `unrelieved` was measured under
  a context limit I had under-declared by 8×; the mechanism is real and the rate
  is confounded, and the register says so. A workload pressing a *correctly*
  declared window is still owed.
- **It does not claim the re-read loop is gone.** 0% over 14 calls is not
  evidence against 72% over 2,056. `tests/measure/reread_ratio.py` carries a
  50-call floor and prints `NOT EVIDENCE` below it precisely so this note cannot
  be cited as having closed that question.
- **It is one operator, one week, two machines.** Every count in §1 is from a
  single session. The *shapes* are likely general; the frequencies are not a
  measurement of anything but this session.
