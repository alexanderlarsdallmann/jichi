# Orchestrating several jichi agents on basicfantasy: the experiment

*Written 2026-08-22 (M541). A **runnable** experiment design, not an analysis —
[`DISTRIBUTED.md`](../DISTRIBUTED.md) is the analysis. Every number below was
measured today on the real tree; the commands are the ones to run.*

---

## 0. The measurement that reshaped this plan

I measured basicfantasy's gate before designing anything to run against it, and the
result moved Phase 0 from "warm-up" to "the whole point":

| `zig build …` | tests run | wall |
|---|---|---|
| `test` | **1** | 0s (cached) |
| `test-auth` | 8 | 14s |
| `test-bcrypt` | 3 | 5s |
| `test-logic` | 4 | 0s |
| **total reachable** | **16** | |

And the tests that exist:

```sh
$ grep -rc '^test "' src --include=*.zig | grep -v ':0$' | awk -F: '{s+=$2} END{print s}'
172        # in 29 of 39 source files
```

**16 of 172 test blocks execute. 156 — 91% — never run.** `zig build test` compiles
`src/main.zig` as its test root, `main.zig` carries no `std.testing.refAllDecls`, and
Zig therefore analyses only the root file's own `test` blocks. `webserver.zig` alone
has 23 tests that have never once been executed by the build.

*Counted two ways, because the first count said 195: that grep was not scoped to
`*.zig` and swept build files too. The runbook's step 4 exists for exactly this.*

### Why this is the plan and not a footnote

**A gate that cannot fail cannot make delegation safe.** Every topology below rests
on one assumption — that an agent's branch can be judged mechanically, without a
human reading the diff. With 16 tests, `zig build test` says *green* for almost any
change to almost any file. Point four agents at that and you have four branches you
must review by hand, which is not orchestration; it is four times the work.

So Phase 0 is not preparation for the experiment. **Phase 0 is the first task the
orchestration performs, and its result is the instrument every later phase needs.**
That also makes it the ideal first task on its own merits: 29 files, independent per
file, mechanically verifiable, and it cannot break the game because it changes no
game logic.

---

## 0b. A blocking prerequisite found while measuring: there is no remote

Before any multi-device phase is possible — and independently of this experiment —
**basicfantasy's Zig rewrite exists on one disk with no backup.**

```sh
$ git -C basicfantasy      remote -v      # (nothing)
$ git -C basicfantasy/zig  remote -v      # (nothing)
$ git -C basicfantasy ls-tree HEAD zig
160000 commit 6fbaee189aaa64d821955e5d26e398883a805d5c	zig
$ git -C basicfantasy ls-tree -r HEAD --name-only | grep -c '^zig/'
0
```

Three separate facts, each verified:

1. **Neither repository has a remote.** Not the parent, not the nested `zig/`.
2. **`zig/` is a nested repository, not a registered submodule.** The parent records
   it as mode `160000` — a gitlink — but there is **no `.gitmodules` entry**, so
   `git submodule status` answers *"no submodule mapping found in .gitmodules for
   path 'zig'"*. Git knows a commit belongs there and has no idea where to get it.
3. **`git ls-tree -r HEAD` lists zero files under `zig/`**, against 41 `.zig` files on
   disk. A fresh clone of basicfantasy yields an empty `zig/` directory and no way to
   fill it.

**Why this blocks the experiment.** Phase 3 pushes work to other devices over ssh and
expects them to fetch a branch and push one back. There is nothing to fetch from.
`git worktree add` on the parent does not populate `zig/` either — observed while
setting up Phase 0, which is how this was found.

**Why it matters more than the experiment.** The Zig rewrite is the project's stated
future — 41 files, 172 tests — and it is one disk failure from gone. This is the
operator's decision to make (where to host, and whether it is public), so nothing has
been created; but no phase past 1 can run until it is, and §8's teardown advice about
not deleting a worktree with its journal inside it is the same lesson at a smaller
scale.

**Minimum to unblock:** a remote for `zig/`, a remote for the parent, and either a
real `.gitmodules` entry or a decision to merge `zig/` into the parent history. The
last is worth considering on its own: a nested repo with no mapping is a structure
that loses work quietly.

---

## 1. What the design must live inside

Four standing constraints. The design works within them rather than wishing them
away, and each is a decision already recorded elsewhere in this repository.

| Constraint | Source | Consequence here |
|---|---|---|
| **No shared filesystem** across the fleet | `scripts/fleet-run.sh` — it exists *because* a Pi, a tablet and a proot guest share none, and it refuses to manufacture one with NFS/sshfs rather than put network `rename(2)` semantics under the only thing keeping two agents off one task | the supervisor **pushes**; there is no shared queue |
| **The queue's claiming rule is an atomic `rename(2)`** | `AUTONOMOUS_LOOPS.md` — "no lock file, no coordinator" | that topology is for **N agents on one machine**, and is Phase 2 here, not Phase 3 |
| **The daemon has no network authentication** | `DAEMON.md`; M528 made it verify its socket mode and refuse to serve otherwise, but the transport is a unix socket | agents do not talk to each other's daemons; **ssh is the transport** |
| **No agent-to-agent negotiation** | `AGENT_COLLABORATION.md`, as a reasoned decision — "an interface a human can read is an interface a human can audit" | what crosses between agents is an **artifact and a verdict**: a branch, a failing test, a review file. Never a conversation |

The last one determines what "pair programming" can mean here, and §5 spells it out.

### The capacity ceiling nobody can design around

One LM Studio server, `qwen/qwen3.5-9b`, **`--parallel 2`**. Two concurrent
requests; a third queues. Measured today: median model-call latency 3.2–10.0s,
output 17.6–32.6 tok/s, and a full read-only project survey costs 6–11 model calls.

**So N > 2 agents do not run in parallel — they queue at the model.** Any speedup
past two agents is speedup in *tool work* (file reads, builds, test runs) overlapping
someone else's *model wait*. That is a real effect and worth measuring, but it is not
2× per agent, and a design that promises otherwise is lying about the hardware.

The server binds `192.168.0.24:1234`, **not loopback** (see
[the measurement note](../analysis/2026-08-22-headless-measurement.md) §8) — which is
the single reason a fleet is possible at all: every device on the LAN can reach the
one model. No device needs its own.

---

## 2. The rig

```
                 ┌──────────────────────────────────────┐
                 │  LM Studio  192.168.0.24:1234        │
                 │  qwen3.5-9b · 131k ctx · parallel 2  │
                 └──────────────▲───────────────────────┘
                                │ HTTP (LAN)
      ┌──────────────┬──────────┴─────┬──────────────┐
      │              │                │              │
 ┌────▼────┐   ┌─────▼─────┐   ┌──────▼─────┐  ┌─────▼─────┐
 │ dev box │   │  Pi 400   │   │ Pi Zero 2W │  │  WSL2 box │
 │ 3c/4.8G │   │ 4c/3.8G   │   │  armhf     │  │ 12c/7.6G  │
 │ agent-1 │   │  agent-2  │   │  agent-3   │  │  agent-4  │
 └────┬────┘   └─────┬─────┘   └──────┬─────┘  └─────┬─────┘
      │ ssh push     │                │              │
      └──────────────┴───── git ──────┴──────────────┘
                      branches on the dev box's repo
```

Devices from [`PLATFORMS.md`](../PLATFORMS.md); all four already run `make check-target`
green, so jichi itself is not the variable. Each agent gets:

- **its own git worktree** off the dev box's basicfantasy repo, on **its own branch**
  `exp/agent-N/<task>`;
- the **same** model endpoint;
- `--edit-scope` naming only the files its task owns, so two agents cannot touch one
  file even by mistake;
- `--journal` and `--log` written **per agent**, joined later on the `run` key
  (M420, and M536 made the two sinks agree about what failed).

**Start with two devices, not four.** The model serves two at a time; a four-device
run measures queueing, which is Phase 6.

---

## 3. Phase 0 — make the gate able to fail

**Hypothesis.** The 156 dark tests are dark because of build wiring, not because they
fail. If so, wiring them in is safe and mechanical, and it converts a vacuous gate
into a real one.

**Falsifier.** If a large fraction of the 156 fail once reachable, the hypothesis is
wrong and Phase 0 becomes a *repair* task of unknown size — at which point stop and
re-plan rather than pointing agents at it.

**Run it single-agent first**, on the dev box, because a measurement of unknown size
must not also be a test of the orchestration:

```sh
cd ~/development/adventure/basicfantasy/zig   # the nested repo, not the parent
git worktree add ../../bf-wt/gate exp/gate-real
cd ../../bf-wt/gate/zig

# The instrument, before the work: how many tests run today?
zig build test --summary all 2>&1 | grep 'tests passed'      # expect 1/1

# One line, then measure again.
printf '\ntest {\n    @import("std").testing.refAllDeclsRecursive(@This());\n}\n' \
    >> src/main.zig
zig build test --summary all 2>&1 | grep -E 'tests passed|error'
```

### PHASE 0 WAS RUN. The hypothesis is falsified, informatively.

Executed today in a throwaway worktree of the **nested** `zig/` repo (not the
parent — see §0b for why that distinction cost a false start):

```
$ zig build test --summary all            # the instrument, first
Build Summary: 3/3 steps succeeded; 1/1 tests passed

$ printf '\ntest { @import("std").testing.refAllDecls(@This()); }\n' >> src/main.zig
$ zig build test --summary all
error: 10 compilation errors
```

**The 156 tests are not dark because they fail. They are dark because the code
they test does not compile.** Ten errors across six files:

| File | Error | Kind |
|---|---|---|
| `character.zig:308` | `std.crypto` has no member `random` | **Zig API drift** |
| `quest_tracker.zig:200` | `@intCast` must have a known result type | **Zig API drift** |
| `combat.zig:15` | expected 3 arguments, found 2 | signature drift |
| `inventory.zig` ×4 | expected 7 arguments, found 5 | signature drift |
| `quest_tracker.zig:265` | duplicate switch value | **genuine bug** |
| `world.zig:440` | expected `*item.Item`, found `item.Item` | genuine bug |
| `world.zig:485` | comparison of `*world.Room` with null | **genuine bug** |

So the green gate was green because **it compiled almost nothing**. `zig build
test` builds `main.zig` and whatever `main.zig`'s *used* declarations reach;
`refAllDecls` forces every declaration to be analysed, and six modules do not
survive that. Two errors are Zig 0.14→0.16 standard-library breakage; the rest are
modules that have drifted out of agreement with each other.

**A first attempt used `refAllDeclsRecursive` and failed differently** — that name
does not exist in Zig 0.16's `std.testing`, which exports only `refAllDecls`. Worth
recording because the error (`struct 'testing' has no member named
'refAllDeclsRecursive'`) reads like a project defect and is a version fact.

**What this changes.** My own falsifier #1 (§9) said a large fraction failing means
"repair by a human, not orchestration". On the evidence I withdraw that: ten
*localised, independently verifiable compile errors in six files* are close to ideal
first agent work — each fix is local, the verifier is one command, and `--edit-scope`
can give each agent exactly one file. What is **not** ideal is doing it with no
remote (§0b) and no gate. So Phase 0 splits:

- **Phase 0a — make it compile.** Six files, ten errors, one agent per file,
  `--edit-scope` per file, verify `zig build test`. Success: the build completes.
- **Phase 0b — count what then fails.** Only once it compiles is "how many of the
  172 tests pass" a question that can be asked. That number is the real Phase 0
  result and it is currently **unknown and unknowable**, which is itself the finding.

**Success criteria, in order:**

1. `zig build test` reports **≥ 100** tests (from 1). The floor is deliberately below
   172: `refAllDeclsRecursive` reaches what the root imports, and `main.zig` imports
   10 of 29 test-bearing modules, so full coverage needs a test aggregator listing all
   29 — which is the follow-up task, and a perfect one to split across agents.
2. **The gate goes red when it should.** Break one assertion, confirm red, restore:
   ```sh
   sed -i.bak 's/try expect(true)/try expect(false)/' src/<somefile>.zig  # GNU sed only
   zig build test; echo "must be nonzero: $?"
   mv src/<somefile>.zig.bak src/<somefile>.zig
   ```
   *This step is not optional.* A gate that reports 172 passing tests and cannot fail
   is worse than one that reports 1, because it invites trust.
3. The number of *failing* tests is recorded, whatever it is. That number is the
   Phase 0 result, and it decides whether the experiment continues.

**Blast radius.** A worktree on a branch; `--edit-scope 'zig/src/**'`; the main tree
untouched. basicfantasy has **uncommitted work of the operator's own** (`.gitignore`,
two `local/config.*` deletions, a modified `zig` entry) — a worktree keeps that
entirely out of reach, which is why every phase uses one.

---

## 4. Phases 1–3 — the topologies, in increasing order of what can go wrong

Each phase is one command and one measurement. Run them in order; a phase that fails
its criterion stops the sequence.

### Phase 1 — one agent, one branch (the baseline)

```sh
jichi --config fleet.json --no-session --auto \
      --edit-scope 'zig/src/character.zig' \
      --verify 'zig build test' --verify-kind test \
      --journal j/agent-1.jsonl --log t/agent-1.jsonl --log-level metrics \
      -p 'Raise character.zig test coverage: add tests for the level-up
          boundary and for an invalid class name. Do not change game logic.'
```

**Measures:** wall clock, model calls, tool calls, tokens, and whether the envelope's
verify gate held. **This is the denominator for every later phase** — a 2× claim in
Phase 2 is meaningless without it.

### Phase 2 — two agents, one machine, disjoint scopes

Two `jichi` processes on the dev box, different `--edit-scope`, different branches.
This is `AUTONOMOUS_LOOPS.md`'s topology with the atomic-rename queue, which is sound
here because it *is* one filesystem.

**Measures:** total wall clock against 2 × Phase 1. **Expected: well under 2×, and
well over 1×** — the two agents overlap tool work with each other's model waits, but
share two model slots. If it comes out at ~2×, the model is the bottleneck and adding
machines will not help either; that is a finding, not a failure.

### Phase 3 — two agents, two machines (`fleet-run.sh`)

```sh
sh scripts/fleet-run.sh --devices dev,pi400 --task tasks/gate-aggregator.md
```

`fleet-run.sh` already implements this: the supervisor **pushes** over ssh, which
removes the coordination problem rather than solving it. Its inheritable decisions —
per-device deadline scaling, token budgets *not* scaled because a token is the same
work everywhere — apply unchanged.

**Measures:** the same three numbers, plus **who won**. The Pi 400 has 4 cores to the
dev box's 3 but ~1/5 the single-core speed; both wait the same time on the same model.
Prediction: **the machines' speeds barely matter**, because the model dominates. If
true, that is the most useful result in the whole experiment — it means the fleet's
value is *concurrency of tool work*, not compute.

---

## 5. Phases 4–5 — delegation and review, as artifacts

Both phases are shaped by `AGENT_COLLABORATION.md`'s decision: **no negotiation.**
What crosses between agents is a thing a human can read.

### Phase 4 — delegation: a failing test is the work order

Agent A's task is to write a test that **fails**, describing a bug it found. Agent B's
task is to make it pass. Nothing is exchanged but the branch.

```
agent-A  --edit-scope 'zig/src/**_test.zig'   → commits a red test → branch exp/A/red
agent-B  --edit-scope 'zig/src/**' (not tests) → makes it green    → branch exp/B/green
```

**Why this is the right shape.** The handoff is a git commit whose content *is* the
specification, and the acceptance criterion is mechanical: `zig build test` on B's
branch, red before, green after, with A's test unmodified. `--edit-scope` enforces the
division — B **cannot** edit the test to make it pass, which is the failure mode
(M88's moved-assertion, and jichi's `--strict-green` exists for it).

**Measures:** did B's fix pass A's test without editing it? Did the envelope catch an
attempt? A tainted verdict here is a *success* for the fence and a finding for the
model.

### Phase 5 — pair programming, without a conversation

"Pair programming" cannot mean two agents talking; there is no protocol for that and
the project decided there should not be. It means:

- **agent-W (writer)** implements on `exp/W/feature`.
- **agent-R (reviewer)** runs read-only with `--readonly`, reads the diff, and writes
  a verdict file: `review/<branch>.md`, with a **machine-readable first line** —
  `VERDICT: accept | reject | needs-work` — and prose beneath.
- The supervisor (a human, or a script) merges only on `accept`.

**Measures:** does R's verdict correlate with the gate? The interesting cases are the
disagreements — R rejects while the gate is green (R found something the tests do
not), or R accepts while the gate is red (R is not reading the gate, which is a prompt
defect). Log both; the second is the one to fix.

**Honest caveat.** `--readonly` **does not stop the shell**: measured today, a
read-only run executed `run_terminal_command` and `run_tests`. For a reviewer that is
desirable — it should run the gate. But "read-only" is not "cannot affect anything",
and `--strict-scope` alongside `--edit-scope` is what forbids the shell if you need
that.

---

## 6. Phase 6 — the queueing measurement

Only after 1–5. Four agents, two model slots, same task class. Measure the model-call
**wait** distribution from the telemetry:

```sh
grep '"event":"model_call"' t/agent-*.jsonl | \
  python3 -c 'import sys,json;[print(json.loads(l.split(":",1)[1])["latency_ms"]) for l in sys.stdin]'
```

**Expected:** median latency rises roughly linearly past two agents while throughput
plateaus. If it does, the fleet's useful width is `2 × (1 + tool_time/model_time)` and
that ratio is the number to publish — it is the only figure in this document that
would let someone else size their own fleet.

---

## 7. Running conditions

Non-negotiable, and each one is a rule that has already cost something:

| | |
|---|---|
| **Fences on** | `--edit-scope` per agent, `pathFence`, worktrees. The operator's uncommitted work sits in that tree. |
| **Caps off** | no `--deadline`, no `--budget-tokens`. A cap that fires manufactures a different answer — measured today: an 84s "stall timeout" that was 119s of honest work at `timeouts.stall: 1800`. |
| **Diagnostics ON** | **never `-q`.** An empty-answer run today went unexplained because I had silenced the warning jichi prints for exactly that case. |
| **`setsid`** | a supervisor's timeout SIGTERMs the process group and kills the run being measured. |
| **`timeouts.stall: 1800`** | the default is tuned for hosted models and is too tight for a local 9B on a large repo. |
| **Local models only** | `qwen3.5-9b` on the LAN endpoint. No priced model, per `CLAUDE.md`. |
| **One `git worktree` per agent** | never the main tree, and never two agents in one tree. |

## 8. Teardown

```sh
git worktree list                       # every exp/ worktree
git worktree remove ../../bf-wt/<name>  # per agent, after its branch is merged or dropped
git branch -D exp/agent-N/<task>         # only after the verdict is recorded
lms ps                                   # restore the model config if it was changed
```

Record the numbers in `docs/analysis/` before removing anything — a worktree deleted
with its journal inside it is a measurement nobody can check, which is the shape M533
and M536 both fixed in the product.

## 9. What would falsify the whole thing

Stated up front so the experiment can lose:

1. **Phase 0 finds most of the 156 tests failing.** Then basicfantasy's Zig port is
   less complete than the test count suggests, and the honest next step is repair by a
   human, not orchestration.
2. **Phase 2 comes out at ~2× Phase 1.** The model is the whole bottleneck; a fleet
   buys nothing and `fleet-run.sh` should be used for *independent* tasks only.
3. **Phase 4's agent B edits the test.** Then delegation-by-artifact needs a stronger
   fence than `--edit-scope`, and the finding belongs in `AGENT_COLLABORATION.md`.
4. **Phase 5's reviewer never disagrees with the gate.** Then the reviewer adds
   nothing a `make test` does not, and pair programming without negotiation is not
   worth its tokens on this model.

Any of those is a publishable result. None of them is a reason not to run it.

## Where this fits

- [`DISTRIBUTED.md`](../DISTRIBUTED.md) — the analysis this plan is the experiment for.
- [`AGENT_COLLABORATION.md`](../AGENT_COLLABORATION.md) — the no-negotiation decision
  that shapes §5.
- [`AUTONOMOUS_LOOPS.md`](../AUTONOMOUS_LOOPS.md) — the one-filesystem queue (Phase 2).
- [`scripts/fleet-run.sh`](../../scripts/fleet-run.sh) — the push topology (Phase 3).
- [`analysis/2026-08-22-headless-measurement.md`](../analysis/2026-08-22-headless-measurement.md)
  — where the latency, throughput and `-q` findings come from.
- [`TESTING_RUNBOOK.md`](../TESTING_RUNBOOK.md) — §0's "prove the gate can fail" is
  its step 1, applied to someone else's project.
