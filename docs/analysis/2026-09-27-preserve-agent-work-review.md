# Review — threadwork's "preserve agent work" plan

*Reviewer pass by the other machine on `threadwork/2026-09-27-preserve-agent-work`
(`docs/plans/2026-09-preserve-agent-work.md`), 2026-09-27. Method: read the branch; verified its
code claims against the tree at HEAD `b250591c`; and drove jichi itself (`jlu/qwen3.8-27b`, a
local gateway model — no priced model used) over the operator's local ebook/paper library to find
and extract how other systems solve this. The raw triage (a ranked shortlist of 22 sources and
verbatim quotes with page numbers from the five most relevant books) lives in a machine-local
research bundle and is not committed here; §3 cites the sources by title.*

> **Correction (threadwork, 2026-09-27, after this review):** R3's premise, that an existing
> sink already scrubs secrets from tool results, was measured and **does not hold for every sink
> that stores tool results**. The details are tracked privately until the fix lands, as
> `SECURITY.md` orders. R3's recommendation stands and is adopted: the transcript must be born
> redacted. See the plan's §12, which also records the adjustments to R2, R4 and R6.

---

## 1. Verdict

**Endorse, with refinements.** The plan is unusually good: it is code-grounded (every line/site
it cites is real — see §2), it picks the right chokepoint over the tempting per-call-site fix, its
central invariant ("nothing jichi does on its own initiative may destroy the only copy of work")
is correct, and its test matrix perturbs per check. The refinements below are about **(a) one
strategic gap the literature exposes — end-only vs continuous checkpointing, (b) three safety
holes — transcript redaction, resume idempotency, salvage idempotency, and (c) sequencing and
measurement**. None of them is a reason to hold P1.

## 2. What I verified against the tree (all confirmed)

| plan claim | check | result |
|---|---|---|
| `jc_snapshot_worktree_remove` has exactly 4 callers (pool, improve, assignment, workflow) | grep source | **Confirmed** — `jc_tool_parallel.c:1038`, `main.c:10244/10628/11009`; current signature is 2-arg `(mgr, path)`. |
| the pool teardown removes *every* worktree then `rmdir`s the base | read `jc_tool_parallel.c:1035–1043` | **Confirmed** — unconditional loop, then `rmdir(wtbase)`. The "quarantine that destroys what it holds" is exactly as described. |
| `jc_snapshot_preserve` / `preserve_before_destroy` / `refs/jichi/discarded/` exist | grep source | **Confirmed** — `jc_snapshot.c:508/653/672`; the envelope's rollback preserve is at `jc_agent.c:843–855`. |
| **`jc_disk_free_mb` is 0 for sub-1024-byte fragments and can't be a guard** | read `jc_platform_posix.c:734–748` | **Confirmed, and worse than stated.** `return f_bavail * (bsize/1024UL) / 1024UL;` → **0 whenever `f_frsize < 1024`** (ZFS commonly 512), and it returns `0UL` for *both* "full" and "statvfs failed". On a 32-bit build the `unsigned long` product also wraps. G5 is real and the plan's fix (measure into `double`, return a status not a sentinel) is the right one. |

The plan's technical claims are trustworthy. The rest of this review builds on that.

## 3. How other agentic & distributed systems solve this

*(Full quotes with page numbers in `notes-from-sources.md`. Sources by short title below.)*

**The dominant pattern in agent frameworks is continuous, per-step checkpointing of the whole
state object — not end-of-run preservation.**

- **LangGraph** (O'Reilly, *Building Applications with AI Agents*, ch.6/7) checkpoints the entire
  typed application state — *including memory contents* — at each graph step, so "long-running
  agents can persist and resume exactly where they left off" and "withstand failures … within the
  same unified graph framework." Resume reads the last committed step and never re-executes
  completed work.
- **Temporal** (same book, ch.7) is the heavyweight version: "automatically checkpoints workflow
  progress, supports deterministic replay, and … resumes from the last successful step even after
  crashes." Durable execution = the engine owns the checkpoints, the code is written as if crashes
  don't happen.
- **Designing AI Agents** (Manning, ch.4.4) frames agent checkpointing as a "checkpoint chain"
  and explicitly traces its lineage to **database write-ahead logging (Gray)** and
  **Chandy–Lamport distributed snapshots**. Two rules it states that bear directly on this plan:
  *flush after every status change* ("it writes immediately after every status change, so if the
  process crashes between two calls, the last completed task is always recorded", p.143), and
  *capture what was done and why* ("a good checkpoint captures both", p.146) — with a measured
  resume cost of **500–1,200 tokens vs 12,000–18,000 without a checkpoint** (p.146). It also names
  the cost curve: "checkpoint too rarely and recovery cost dominates; checkpoint too often and I/O
  overhead dominates" (p.278).
- **Kafka** (Manning, *Kafka in Action*) is the concrete WAL blueprint: an append-only log, a
  *committed offset* as the "last safe position" a restarted consumer reads (p.49), **idempotent
  producers** that dedupe by (producer-id, sequence) so retries are safe (p.68), **exactly-once =
  idempotent write + atomic offset-and-output commit** (p.72), and **log compaction so the log
  doesn't "grow ad infinitum"** (p.121) — i.e. the store must have eviction.
- **Think Distributed Systems** (Manning) supplies the vocabulary the plan is missing a word for:
  a `kill` is **crash-stop** for the agent process but **crash-recovery** (state may be lost) for
  what it owned (p.27–28); the reachable target is **exactly-once *semantics*** = at-least-once
  replay + **idempotent** steps, deduped by an **idempotence key / UUID** (p.55–63).
- **Elixir/OTP** (*Elixir in Action 2e*, ch.8–9) is the orphan story: a **supervisor** "detects
  possible terminations of any child, and can restart it" via links/monitors/exit traps (p.211),
  and — crucially for this design — recovery is **clean-slate**: a restarted worker "starts with
  new state, which should be consistent" (p.247), rebuilt **from durable storage, not from a
  captured process heap** (p.247).

**What this means for the plan.** Two validations and one challenge:

- *Validation 1 (the durable-artifact choice).* The plan preserves the git **worktree** (a durable
  artifact) and explicitly does **not** try to snapshot the child's conversation/heap. OTP's
  "rebuild from durable state, not a captured heap" and the crash-stop/crash-recovery distinction
  say this is the correct call: volatile in-process state is gone by definition; only files and the
  log survive. Good.
- *Validation 2 (the "why", not just the "what").* The plan's end record (`reason`, `repeated`,
  `last call`, `verify`) and the prepended predecessor record are exactly "capture what *and*
  why," and the token-cost figures above are the quantified argument for why it pays off. Good.
- *The challenge — end-only vs continuous.* Every framework above checkpoints **per step**; the
  plan preserves **only at teardown** (P1) and defers per-step WIP commits to an optional,
  off-by-default P6. The incident that opened the plan (two children, ~3 hours each, one in a
  165-call loop, one wedged in a dead HTTP stream) is precisely the case end-only preservation
  still serves — the parent reaps and preserves — **but only if the parent lives**. If the parent
  is also killed mid-batch, teardown never runs, and P3's salvage recovers only what is *on disk in
  the worktree*, which for an agent that has not committed is its last `edit_file`, not its last
  *reasoning*. See R1 in §4.

## 4. Recommendations — design

**R1 — Raise WIP checkpointing from optional (P6) to a measured decision, and reframe it as the
journal, not extra git work.** The literature is unanimous that the unit of preservation is a step,
not a run. The plan already builds the per-step *transcript* (P2) — an append-only JSONL journal
flushed per line. That journal **is** the Kafka log; the missing piece is a *committed offset*: the
transcript should record, per step, enough to answer "what had been done" so a successor doesn't
need the worktree's git state to know where the predecessor was. Concretely: keep P1's end-of-run
git preservation as the cheap common path, but treat the P2 transcript as the crash-recovery record
of record, and let `from:` (P5) resume from **transcript + tree** rather than tree alone. Then P6's
WIP git commits become a pure optimization (fewer lost edits), not the only continuous mechanism.
This closes the "parent dies before teardown" hole without paying git-commit cost on every child.

**R2 — Make `from:` resume idempotent, and say so.** §4.4 applies a predecessor's squashed diff with
`cherry-pick --no-commit` and refuses on conflict — good, but "build upon" also means a successor
may *redo* actions the predecessor already did (re-run a migration, re-create a file, re-send a
side-effecting shell command). *Think Distributed Systems* and *Kafka* both make the same point:
safe re-execution needs **idempotence keys**. Recommendation: the predecessor record should include
a compact list of *side-effecting actions already taken* (from the transcript), and the successor's
prompt should instruct "these ran already; do not repeat them." This is cheap (it is text) and it is
the difference between "build upon" and "double-apply." Add it to §4.4.

**R3 — Redact transcripts like sessions, not just permission them like sessions.** §4.2/§4.8 make
the transcript `0600` "as sensitive as a session," but a transcript logs **tool results**, and a
child's `run_terminal_command` output can echo an API key, a token, or `env`. A session already goes
through jichi's secret scrubbing; the new transcript path must too. Recommendation: route every
transcript line through the same redaction the session/telemetry sinks use (`jc_priv` /
the existing secret registry), and add a test (see T-new-1). This is a genuinely new exposure
surface — per-child, per-step, on disk — and it is the one security gap in the plan.

**R4 — Throttle the disk probe; keep the checks that matter unthrottled.** §4.6 samples at *every*
tool boundary on up to three filesystems. `statvfs` is microseconds, but the disk does not fill in
one tool call, so this is measurement for its own sake on the hot path. Recommendation: sample on a
cheap throttle (every N tool calls or every T seconds, whichever the envelope already tracks), while
keeping the **unthrottled** checks at the three moments that are actually decisions: run start,
before `spawn_parallel` allocates N worktrees, and before each preservation. This preserves the
guarantee and removes the per-call tax the plan's own "fence in the `connect` class" argument
implies should be near-free.

**R5 — Make salvage idempotent and manifest-first.** §4.3's salvage runs at every `spawn_parallel`
start *and* in `gc`. If preserve succeeds but the subsequent `rmdir` fails (disk full, EBUSY), the
next salvage re-preserves the same base → duplicate `parallel-orphaned` refs. Recommendation:
salvage skips a base already preserved (dedupe by the tree's content sha, or a `.salvaged` marker
beside the lock), and the manifest `batch.json` is written **before** the worktrees are created, so
an orphan is discoverable even if the crash lands during worktree creation. Add to §4.3.

**R6 — Surface the `repeated` counter live, not only in the record.** §4.2 computes consecutive
identical failing tool calls to *record* the loop. That same counter, surfaced to the model at the
tool boundary ("you have called X with the same arguments and it failed N times"), is a direct
mitigation for the 3-hour loop in run 0198 — the incident this plan exists for. It is a few lines on
top of a counter the plan already adds, and it partially addresses §7's "does not unblock the lead."
Recommend adding it as a small P2 rider (behind the budget-notice idiom it already cites).

## 5. Recommendations — implementation

- **Land the telemetry-attribution fix first, on its own.** §4.2 bundles "telemetry names the child"
  into P2, but it is an independent correctness fix (depth-1 records currently carry the *lead's*
  sid and model). It blocks nothing, and P4a *measures per-tool-call disk from telemetry* — if child
  telemetry is misattributed, P4a's population is muddier. Ship it before P4a. (My memory note
  [telemetry corpus is an instrument] applies: 40% of a recent corpus was mislabeled probes;
  don't add another mislabeling.)
- **Keep the signature change as the enforcement spine.** §4.1's "change the signature so the
  compiler enumerates every caller" + the T5 lint is the strongest structural idea in the plan;
  implement it exactly, and let `JC_WT_MERGED` be the *only* skip. This is the "audit the universe,
  not the result" discipline in code form.
- **The `rmdir`→kept-tree interaction is a real edit, not a footnote.** §3 notes that once P1 can
  keep a tree, the two `rm -rf`-of-base-directory sites (`main.c:10272`, `:10631`) and the pool's
  `rmdir(wtbase)` must not delete what the chokepoint kept. Make this part of P1's diff and its T5
  lint universe, not a later cleanup.
- **Order:** P1 + (telemetry fix) + P4a in parallel → P2 → P3 → P4b → P5 → (P6 only if R1's
  measurement says the end-only path loses too much).

## 6. Recommendations — testing (additions to the plan's T1–T12)

- **T-new-1 (R3):** a child whose tool output contains a canary secret leaves a transcript in which
  the secret is redacted. Red: disable redaction on the transcript path, canary appears. This is the
  security test the plan is missing.
- **T-new-2 (R2):** a `from:` resume of a task whose predecessor ran a side-effecting command does
  not repeat it — assert the successor's prompt carries the "already ran" list and (with a mock
  side effect counter) the effect fires once.
- **T-new-3 (R5):** salvage run twice over the same orphan base produces one ref, not two.
- **Strengthen T11 (the disk arithmetic) with a real platform, not only a synthetic fragment size.**
  The G5 bug is *live* on ZFS. Assert on the illumos and FreeBSD rigs that `doctor`'s free-space
  line is non-zero on a non-full disk (it may read 0 today — that is the bug). This ties the fix to
  the platform matrix I just extended (M796–M799); the rigs already run `doctor` offline.
- **Confirm G3's read-only fallback before P3 (the plan says "not yet observed").** A one-boot
  container repro (`--init`, lead pid 2, a leftover base) is the red-first step for P3; do not build
  the lock-based salvage on an unconfirmed failure mode.

## 7. Implications of the design

- **Storage.** Kept states are git deltas (cheap); the risk is un-`.gitignore`d build output. The
  `preserveMaxFileMB` cap (default 16) handles the per-file case; add a per-preservation *total*
  warning too, and re-measure the 5 KB/state figure on a Zig tree (the plan commits to this). The
  store now has three growth sources — discarded refs, attempt transcripts, orphan bases — and gc
  must bound all three (the plan says so; make it a T5-style enumerated set so a fourth source
  can't be forgotten).
- **The disk-full corner is genuinely hard.** §4.1 keeps the tree when preservation fails; §4.6
  keeps the tree at the stop floor. Both are right (never destroy the only copy), but a kept
  *worktree* still holds a multi-GB build cache, so on a full disk "keep the tree" can prevent the
  very recovery it protects. The plan correctly refuses to delete work to free space; the
  implication to make explicit is that **the operator must be the one to resolve a full disk**, and
  `doctor` must make that loud and actionable — name each kept-unpreserved tree, its size, and the
  `jichi recover` handle. This is an operability requirement, not a code path.
- **Security.** The new per-child transcript is the one new exposure surface (R3). Everything else
  writes only to the shadow repo and `~/.jichi.d`, as the proposal's §8.4 requires.
- **Cross-platform.** The disk guard's correctness *is* a portability problem (the G5 bug is a
  statvfs-semantics difference), so its "unknown → never stop" rule is load-bearing, and its
  measurement must be checked on every non-Linux row. This dovetails with the rig work: add the
  free-space assertion to the offline rig checks.
- **Telemetry schema.** The attribution fix adds `task`/`task_run`/child-model fields; downstream
  readers (`insights`, `learn analyze`) must tolerate old records lacking them. Version or
  default-fill, don't assume presence.
- **What it deliberately does not do** (§7) is right: it does not unblock the lead, survive loss of
  the machine, or outlive the workspace. R6 is the one cheap partial mitigation for the first of
  these; the rest belong to the companion analysis the plan already points to.

## 8. Answers to the plan's open questions (§10)

1. **Disk-stop outcome:** agree with the plan's own recommendation — reuse `budget_exhausted` with
   the budget name `disk`, and carry the fence's words in the disposition (`stopped: disk low (work
   kept)`). A separate outcome is not worth a new code path; the disposition string prevents a
   dashboard from reading a fence as a fired cap.
2. **`from:` for `attempt`/workflow stages:** later, once the pool version has been used — agreed.
   `from:` is most valuable where children are most disposable, which is the pool.
3. **Transcript as JSONL vs rewritten JSON:** **JSONL, firmly.** It is the Kafka-log shape; it
   survives SIGKILL with at most the in-flight line lost; a rewritten-JSON-per-event pays a full
   rewrite every event for a guarantee JSONL already gives. The plan's instinct is correct.

## 9. One-paragraph summary for the commit / the operator

threadwork's plan is sound and code-accurate, including a real latent disk-measurement bug it
correctly diagnoses. The main strategic note is that every agent framework in the library
(LangGraph, Temporal) and the WAL/OTP lineage checkpoint *per step*, while this plan preserves *at
teardown*; the plan's own P2 transcript is already the per-step journal, so the recommendation is to
make that journal — not the end-of-run git tree alone — the record a successor resumes from, which
closes the "parent dies mid-batch" hole without per-child git cost. Three safety refinements:
redact transcripts (new exposure surface), make `from:` resume idempotent (safe re-execution), and
make salvage idempotent + manifest-first. Sequence the free telemetry-attribution fix ahead of the
P4a measurement. Ship P1 as written; it is the spine and it is right.
