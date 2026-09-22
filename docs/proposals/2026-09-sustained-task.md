# A second driven task: what happens after the loop closes once (design)

*Written 2026-09-21, from a limit `scripts/_rig_live.sh` states about itself in
its own header. **Design only — nothing in `src/` or `tests/` changes on this
page.** It proposes a second task, a second verdict word and three claimable
rungs, and it says plainly which of its own recommendations are grounded in a
measurement and which are judgement.*

## 0. The limit this exists for, in the existing rig's own words

`_rig_live.sh` does not hide what a `Driven` row means:

> **WHAT A PASS MEANS, stated so a row is not over-read:** request build, SSE
> framing, a native tool call, tool execution, and a second turn that consumes
> the result. It does **NOT** mean the platform runs long sessions, survives
> compaction, or handles concurrency. It means the loop closed once, here.

Three named things a `Driven` row explicitly does not claim. As of 2026-09-21
the matrix has a great many `Driven` rows — WSL2, FreeBSD, NetBSD, OpenBSD,
illumos, both Pis, the 160 MB Debian VM, the 96 MB busybox guest and nineteen
emulated architectures — and **not one of them claims any of the three.**

## 1. The first design decision: this must NOT extend the existing task

`tests/smoke/rig_live_lint.sh` check 3 refuses a rig that restates the driven
task, for a reason its header gives: *"Two rows driven with different prompts
are two anecdotes."* The same argument applies across **time**. Every row above
was earned against a two-turn task. Adding a third turn to that task would
silently re-define what those rows claim, and nothing in the register could say
which rows were measured under which definition — the page would become
internally incomparable in a way no lint could detect, because every row would
still be spelled `yes`.

**So: a second task, in its own file, with its own verdict word and its own
lint clause.** This is the whole reason the proposal is shaped this way, and it
is the one part I would not compromise on.

## 2. The word

Recommendation: **`Sustained`**. `Driven` says the loop closed once; `Sustained`
says it kept closing under pressure. Alternatives considered and rejected:
*Endured* (reads as suffering, and the rig is not a stress test), *Exercised*
(too close to "tested", which is the word the whole verdict ladder exists to
replace), *Driven+* (a superscript verdict invites the reading that a plain
`Driven` row is deficient).

**The word must not shame a row that will never earn it.** A 96 MB busybox
guest with 1,668 KB of RSS headroom should not be asked to hold a twenty-turn
session, and its `Driven` row is not weakened by saying so. The register should
therefore carry `n/a — not asked` as a first-class value, distinct from `no`.

## 3. "Long session, compaction, concurrency" is three properties, not one

### 3a. Long session decomposes again, into three

| | claim | what could actually break |
|---|---|---|
| **(i) many turns in one process** | the loop runs N times without degrading | arena reuse, descriptor totals, RSS drift, the session store growing without bound |
| **(ii) resumed across processes** | `--continue` / `--session <id>` reload and continue | store format, id resolution, locking between two instances |
| **(iii) context grows until something fires** | — | this is compaction; see 3b |

(iii) is not a third thing. It is the trigger for 3b, and folding it in is how a
"long session" test becomes an untestable mood. **Recommendation: the task
covers (i) and (ii) explicitly and hands (iii) to the compaction rung.**

> **Measured 2026-09-22 — and (i) asks the wrong question**
> ([`analysis/2026-09-22-long-session-rss-measured.md`](../analysis/2026-09-22-long-session-rss-measured.md)).
> *"Does the loop run N times without degrading"* is already answered by history:
> **eight sessions reached 20+ turns, one reached 76**, with no failure
> attributable to length. Building a rung to re-establish that would confirm what
> the corpus states.
>
> What the corpus does show is worth a rung. `turn_end` records `rss_kb`, and
> across those eight sessions **four are flat** (14–32 MB over 23–75 turns) while
> **four step**: flat for 15–18 turns, one spike to 332–608 MB, then a **new
> plateau ~10× the starting floor**, held to the end. The peak is transient; the
> floor is not. Tool output cannot explain it — the largest output corpus-wide is
> 185 kB against jumps of 322–591 MB.
>
> **So S1 measures the FLOOR, not survival**: RSS at turn N against turn 1. Three
> consequences for the rung's shape:
> - **N ≥ 20, with real tool use.** The steps land at turns **16, 19 and 20**; the
>   "~12–20" below would have missed all of them.
> - **Report per session, do not fail a platform.** Half the long sessions are
>   flat. A step reflects the workload at least as much as the platform.
> - **The regime is rare: 211 of 236 sessions ran exactly ONE turn** (median 1,
>   p90 2). The rung tests a tail that 5% of sessions reach — which is precisely
>   where drift hides, but it must be claimed as the tail and not as typical use.

### 3b. Compaction — and here the design gets lucky

Compaction is **falsifiably observable today**, which is rare and worth using.
Two phases each emit a `compact` telemetry event:

- **Between-turn summarisation** — `src/chat/jc_agent.c:4032`, via
  `jc_compact_run`. The handler at `src/util/jc_telemetry.c:423` reads `dup`,
  `age` and the M326x `short` split.
- **Mid-turn elision** — `src/chat/jc_agent.c:3466`, which carries
  `"phase": "midturn"` plus `elided`, `before`, `after` and `target`, and the
  emitter's own comment says why those four are there: *"Calibrated real-token
  terms — the same units the trigger compares, so a reader can check the
  decision."*

That last clause is the design's best gift. **The assertion does not have to be
"it did not crash."** It can be arithmetic: `before > target`, `after <=
target`, `elided > 0`. A checker verifies the decision instead of trusting it.

> **Measured 2026-09-22, and this paragraph is now wrong in its strongest
> claim.** Against 313 real mid-turn passes from three workloads and 17 sessions
> ([`analysis/2026-09-22-mid-turn-compaction-measured.md`](../analysis/2026-09-22-mid-turn-compaction-measured.md)),
> **5 reached target.** 84% fired and changed nothing; 14% reduced and were
> still over. So those three assertions fail on **98%** of passes, and a rung
> that is red on every row is a rung somebody turns off.
>
> **The corrected shape: S2 RECORDS, and asserts almost nothing.** It reports
> `before`, `after`, `target`, `elided` and the outcome class (no-op / reduced
> but over / reached). The only invariant worth asserting is the one that held
> in **332 of 332** events: **`after <= before`** — compaction never grows the
> context — plus that the event fired at all when the trigger said it should.
>
> This is the discard criterion in §9 doing its job on the first try, and note
> what it discarded: not the rung, the *assertion*. "On this platform, N passes,
> k reached target" is worth having. "This platform fails compaction" would have
> been false on every platform including the bench.

There is more. The mid-turn site fires when `nc > 0` **or** `mrep.pressed` — the
second being, in its own words, *"a workload whose history is many small tool
results gives this pass nothing to elide, so it fires every round and the
request still goes out over the configured limit."* That case is an **open
question in the register** (`DEFERRED.md`, *"Decide what jichi should DO when
mid-turn compaction cannot reach its target"*). A task that can induce
`pressed` on purpose converts an open design question into a measurement, on
whatever platform it runs. **Recommendation: make inducing `pressed` an
explicit, separately-reported outcome of the compaction rung rather than a
failure** — it is the interesting case, and a rig that scores it as red will be
turned off.

> **And it did not need inducing.** 2026-09-22: this machine's telemetry already
> held **308 passes in 313** that went out over the configured limit — 264 with
> nothing to elide, 44 that elided and still missed. The rung was designed to
> manufacture a case that is the overwhelmingly common one. That does not remove
> the recommendation above, it strengthens it: `pressed` must be *reported*,
> never scored red, because on this evidence it is the normal outcome.

### 3c. Concurrency — and the honest part

Most of it is **already covered, offline, on every platform that runs the smoke
tier**: `parallel_merge` (isolated git worktrees, file-level merge),
`parallel_abort` (abort propagation and reaping) and `parallel_hang` (the
per-child watchdog) are part of `make check-target`. Guix has run them; the BSDs
have run them; `parallel_abort` deadlocking under `guix shell -C` is a recorded
open finding rather than an unknown.

So concurrency is **not** an untested axis in general. Exactly one thing is
untested: **parallel children each making a real model call.** That is the only
content a live concurrency rung would add — and it is the most expensive thing
on this page, because it multiplies the model cost by the child count on the
most constrained hardware in the matrix.

**This is the recommendation most likely to be unwelcome, so it is stated
plainly: build the concurrency rung last, or not at all.** Its unique claim is
narrow and its cost is the highest. A row that runs the offline `parallel_*`
drivers and is `Driven` has already shown the fork pool works there and that
model calls work there; what remains unproven is only their interaction.

## 4. The rungs, and why the task must not be monolithic

| rung | claim | rough cost on the bench |
|---|---|---|
| **S1 — endurance** | RSS floor at turn N vs turn 1, then resumed in a second process and continued | N × a turn; **N ≥ 20** — measured, see §3a; the observed steps are at turns 16/19/20 |
| **S2 — compaction** | one induced compaction, with its own numbers checked | one turn plus a small fixture — see below |
| **S3 — concurrency** | K parallel children, each a real call, merged | K × a turn, plus the worktree churn |

A single pass/fail over all three would make every constrained row a `no`, and
*"the matrix came to look emptier than the work actually done"* is a mistake
this project has already made once and written down. **A row claims the rungs it
ran: `Sustained S1,S2`.**

**S2's cost lever, and its honest price.** Do not build a large fixture to reach
the context limit. Set a **small `contextLimit` in the task's own config** so
compaction triggers at a fixed, tiny size. This makes the rung affordable on a
Pi and tests the mechanism rather than the model's appetite — but it must be
said in the row: *it proves the mechanism fires and its arithmetic holds; it
proves nothing about behaviour at a real 128k context.* A row that claims the
latter from the former would be exactly the over-reading the verdict ladder
exists to prevent.

## 5. What makes an assertion evidence here (carried over, not re-derived)

Five rules this project has already paid for, applied to this task:

1. **A per-run nonce, not a quoted sentence.** The existing task's rule, for the
   existing task's reason: a token minted this second can only be produced by
   having read the file.
2. **A floor of zero cannot validate.** "N turns completed" must be counted from
   the journal, not inferred from exit 0. A session that died at turn 2 and
   exited cleanly must fail this.
3. **A positive control for S2.** A run with the threshold raised must emit
   **no** `compact` event. Without it, "compaction happened" may be a misread of
   an unrelated event, and the rung is green because it cannot see.
4. **One nonce per child in S3.** K children that all report the same phrase is
   indistinguishable from one child that ran K times.
5. **Report the effect, never the attempt.** A rung that could not run says so
   in its own words, the way `jc_rig_live_skip` already does.

## 6. Where it lives

`scripts/_rig_sustained.sh`, beside `_rig_live.sh`, holding the one definition
of this task; `rig_live_lint` gains a clause so no rig restates *it* either.
Rigs call its task functions the way `tier-v-arch.sh --drive` calls
`_rig_live.sh`'s — directly, with no transport assumptions.

**On the page: a separate register table, not a column.** A column implies every
row should have a value; most rows legitimately will not, and a table of
fourteen `n/a`s teaches nothing. A second small register, listing only the rows
that were asked, keeps the absence honest and the page readable.

## 7. What a `Sustained` row still will not claim

Multi-day sessions. Real 128k contexts (see §4). Network partitions mid-turn.
Disk-full, clock jumps, signal storms. Anything about a *machine* on an emulated
row — `qemu-user` has no cache coherency and no timing, and its RSS figure is
the emulator's.

And the epistemic status is the same as `Driven`, one rung along: **the
mechanisms held for one bounded run, on one day, on that platform.** Not a
guarantee — an observation, with a date and a number beside it.

## 8. Recommendation

**Build S1 and S2. Defer S3.** Both are observable through events that already
exist, and both were re-shaped by measurement before a line was written: S2
records rather than asserts (§3b), S1 watches the memory floor rather than
survival (§3a). That is two of the three rungs corrected by the corpus at a cost
of one evening and no code. S3's
unique content is narrow, its cost is the highest on this page, and the offline
tier already proves the fork pool on every row that can run it.

If only one thing is built, build **S2** — it is one turn, it has the best
observability in the codebase, and inducing `pressed` would answer a question
the register currently has open.

## 9. Discard criteria

Discard this design if any of these turn out to hold:

- **The operator decides the matrix should not grow a second verdict word.** Two
  registers cost maintenance, and a page nobody trusts to be current is worse
  than a page that claims less. This is a judgement call and it is theirs.
- ~~**S1/S2 pass everywhere the offline tier passes.**~~ **Partly answered
  2026-09-22, before any rig was built.** S2 does not "pass" anywhere as first
  specified — its assertions fail on 98% of real passes — so the question was
  never whether the rung is redundant but whether its *shape* was right. It was
  not; see §3b. The cheap check stands for S1, and for S2 in its corrected,
  recording form.
- **The mid-turn compaction question closes with a redesign.** The open row may
  change what the `compact` event carries; S2's assertions are derived from
  those fields and would have to be re-derived, not ported.
- **The cost turns out to dominate.** If S1 on the slowest row takes longer than
  the whole offline tier there, the rung is not affordable on the hardware this
  project cares most about, and a cheaper claim should be designed instead.
