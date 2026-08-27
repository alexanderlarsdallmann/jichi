# What opencode and Continue do that jichi does not — and two things worth taking

*A comparison of jichi against `opencode` (~2,700 TS files, HEAD 2026-08-08) and `continue`
(~1,400 TS files, HEAD 2026-07-20), the latter being the tool jichi was reimplemented from.
Written because the first pass was done by grep and was wrong in places; the corrections are
kept visible.*

---

> **Revised 2026-08-21 (M522).** Three things, all corrections to this document
> rather than to the tools:
>
> 1. **The convergence counts in §2 are not reproducible**, because this document
>    never recorded the command that produced them. Re-measuring MCP in opencode
>    today gives **169** files with `grep -rl mcp . --include='*.ts'` and **436**
>    with `grep -ril mcp .`; the figure below says 221, which sits between them.
>    Nothing is wrong with either count — they answer different questions — but a
>    count whose command is unrecorded is not evidence, which is §1's own lesson
>    applied to §2. State the command with the number.
> 2. **Claude Code was never in scope here** despite being the third tool the
>    project is asked about. It is covered in [`../COMPARED.md`](../COMPARED.md),
>    limited to what can honestly be known: its config surface (which
>    `jichi-convert` parses exactly) and its published behaviour. Its
>    implementation is not readable, so nothing claims to describe it.
> 3. **The two recommendations in §5 have outcomes**, recorded there.
>
> The usable summary now lives in [`../COMPARED.md`](../COMPARED.md); this file
> stays as the measured, dated basis it was.

## 1. Method, and where it failed

Two passes. First a **grep for vocabulary** across both trees, then **reading** opencode's
`AGENTS.md`, `CONTEXT.md` and its permission tests.

The grep pass got two things wrong, in the same direction — it under-credited them:

- It reported no run-level caps. opencode has **`maxSteps` (18 sites) and `maxToolCalls`
  (17)**. A search for `budget` found only `thinkingBudget`/`budgetTokens`, which are the
  model *reasoning* parameter, and concluded there was no run bound. There was; it just had a
  different name.
- It reported 236 files "mentioning snapshot" and guessed test fixtures. In fact
  `packages/core/src/snapshot.ts` is a **real shadow-git snapshot system** —
  `global.data/snapshot/<project>/<hash(worktree)>`, a separate git dir whose work tree is the
  project. That is jichi's architecture, arrived at independently.

**The lesson is the one this project keeps relearning**: a grep for a name answers a question
about vocabulary, not about capability. It is a way to find where to read, not a substitute
for reading. Recorded here rather than smoothed over, because the first summary was sent to
the operator before the second pass corrected it.

## 2. Where all three converge

MCP (opencode 221 files, Continue 116), LSP (239 / 37), ACP (34 / 5), compaction (264 / 45),
prompt caching (25 / 12), subagents (127 / 21), headless mode (83 / 86).

More striking, opencode's permission model is jichi's **in the same words**:
`Effect = "allow" | "deny" | "ask"` and `Reply = "once" | "always" | "reject"` — which is
`jc_approval` and the TUI's `y/n/a` prompt, derived independently. Convergent design is
usually evidence the design was right.

## 3. Where opencode is ahead of jichi

Not "differently aimed" — **better**, on its own axis. From `CONTEXT.md`, which is a
controlled-vocabulary domain glossary (every term carries an explicit *"Avoid:"*), ~110
numbered invariants, an example dialogue, and flagged ambiguities:

| their concept | what it does | jichi's equivalent |
| --- | --- | --- |
| **Context Epoch** | The span during which one rendered system context is *the immutable provider-cache baseline*. Stored durably and "reused verbatim across process restarts"; ends only at compaction or a session move | A prefix-stability **guard test** (M31d). We assert the property; they model it |
| **Mid-Conversation System Message** | When a context source changes, emit a durable chronological instruction at a "Safe Provider-Turn Boundary" | M61 injects auto-context onto the *user* message specifically to avoid disturbing the cached prefix — a workaround where they have a model |
| **Managed Tool Output File** | Oversized tool output keeps a bounded preview in history and moves the complete text to a temp file **whose path the model is told** | jichi caps and truncates (`readMaxBytes` &c.); the remainder is gone |
| **Steer vs queue** | Prompts steer by default and promote at the next safe boundary; an explicit `queue` mode waits for idle | The control channel's `inject` (M159) — the same idea without the durable promotion semantics |
| **Per-agent permission algebra** | The same assertion returns `allow` globally and `deny` under a `reviewer` agent, by rule composition | Agent profiles carry a `tools:` allow-list — a fence, not an algebra |
| **Authorization pinned to the issuing turn** | "Local tool authorization retains the effective agent of the provider turn that issued the call; a later agent switch cannot change that call's policy" | Not applicable yet: jichi does not switch agents mid-turn, so it has never had this race |

Their permission tests are also strong on their own terms: Effect-based, no mocks, testing the
real service, and one assertion worth copying — **reading a managed output file is permitted
without granting external-directory access.** A capability carved out narrowly rather than a
directory unlocked.

## 4. Where no counterpart was found, after reading

One thing, and everything downstream of it: **the verify-gate loop.** A run that must end with
a green verifier or have its work reverted to the last known-good checkpoint, bounded by
budgets that stop it, fenced to the paths it may edit, and recorded in a journal a supervisor
reads afterwards.

> **Corrected 2026-08-22 (M531): this list under-credited them a THIRD time, by
> the same method, in the section that lists what they lack.** §1 above says a
> grep for a name answers a question about vocabulary, not about capability — and
> then §4 searched for `editScope` and concluded there is no edit-scope fence.
> **opencode ships one and documents it**: `permission.edit` takes a
> `{glob: allow|deny|ask}` map whose *key order is the precedence*
> (`packages/core/src/v1/config/permission.ts`, enforced in
> `packages/core/src/tool/write.ts` via `Wildcard.match`, last match wins,
> default `ask`), documented at `packages/web/src/content/docs/permissions.mdx`.
> Continue designed one too (`extensions/cli/src/permissions/permissionChecker.ts`)
> though its argument-name map is wrong for several tools, so it does not fence
> what it claims to. Also corrected: `maxToolCalls` is **codemode's** interpreter
> limit, not a run bound — opencode's run bound is per-agent `steps`, which
> defaults to unbounded. What is left of the original claim is narrower and
> stands: only jichi ties a fence to *out-of-scope reversion with a provenance
> rule* and to an *end-of-run verdict that can roll the work back*.

Searched by name and absent in both: `editScope` (0 files each), `verifyCommand` (0 each),
`unattended`/`autonomous` (0 each), a "run the gate, revert if red" loop (not found). opencode
caps `maxSteps` and `maxToolCalls` — but **capping is not verifying**, and a cap says nothing
about whether the work was any good.

Nor is there anything resembling the gate-integrity checks: hollow green (M86), verdict vs
evidence (M331), moved goalpost (M88), out-of-scope tree diff (M83), starved detection (M96),
preserve-before-destroy (M336). That absence is coherent — **those only become necessary once a
gate is deciding whether to keep work**, and neither tool has given a gate that authority.

The honest summary: **opencode is the better-engineered assistant; jichi is the more careful
unattended agent.** Their durability protects conversation state across crashes; jichi's
protects a workspace from an agent nobody is watching.

## 5. Two things to take

Recorded as candidates, not decisions. Neither is designed yet.

### 5.1 Managed tool output — keep the remainder, tell the model where it is

> **Outcome: taken.** Built in at M440; see [`../TOOL_OUTPUT_COST.md`](../TOOL_OUTPUT_COST.md).

jichi's tool caps (`readMaxBytes`, `runMaxBytes`, `searchMaxBytes`, `gitMaxBytes`,
`fetchMaxBytes`) truncate and discard. `docs/TOOL_OUTPUT_COST.md` argues correctly that
smaller tool output is the main lever on cost — and the truncated remainder is *also*
sometimes the thing the model needed, at which point it re-runs the command and pays twice.

opencode's shape: bounded preview in history, complete text in a temp file under a shared
directory, **the path handed to the model**, with three properties worth keeping:

- a storage failure "does not change a successful tool operation into a failed one" — the
  session records an explicitly lossy bounded output and the *operator* gets the diagnostic
- "raw oversized success is never published before a later correction" — bounding and
  publishing are one interruption-safe region
- the managed path is readable by ordinary tools without widening filesystem authority

For jichi this composes with the path fence rather than fighting it: a file under
`~/.jichi.d/tool-output/` is outside the workspace, so it needs an explicit narrow read
permission — exactly the carve-out their permission test asserts.

**Open question before designing it:** does this help or hurt on a backend with no prompt
caching? A path the model then reads is a second round trip; a truncated output the model
re-runs is also a second round trip. Which is cheaper is measurable and unmeasured.

### 5.2 Context Epoch — model prefix stability instead of only testing it

> **Outcome: half taken, and the half that shipped is the smaller one.** What
> exists is DETECTION, not a baseline: `tests/smoke/prefix_churn.sh` (M365) warns
> when the system-prompt hash changes on three consecutive turns. The named,
> durable epoch — rendered once, stored, reused verbatim across restarts,
> replaced only at compaction — was not built, so "what exactly is the cached
> prefix, and when did it change" still has to be reconstructed from telemetry.
> Recorded as unfinished rather than quietly dropped.

jichi asserts that successive `build_request`s are byte-identical (M31d) and warns when a
backend returns no cached tokens (M326w). What it does not have is a *named, durable* baseline:
the system context rendered once at the start of an epoch, stored, reused verbatim across
restarts, and replaced only at compaction.

The measured relevance: on the HRZ backend, 12,637 fixed tokens are re-sent per call — 14% of
one workload's spend — and prompt caching is unavailable. An epoch model does not fix an
absent cache, but it would make "what exactly is the cached prefix, and when did it change"
answerable rather than inferred, which is what M326w had to reconstruct by hand from 36,925
telemetry events.

## 6. What this comparison cannot tell you

It read three documents and grepped two trees. It did not run either tool, read their
session runtimes, or test their claims. Every "ahead of jichi" above is taken from their own
documentation, which is a statement of intent as much as of behaviour — the same caveat jichi's
docs carry, and the reason §1 exists.

Continue got the least attention here, largely because jichi already contains a reading of it:
it is the tool jichi was reimplemented from, and its shape is visible throughout
`docs/MIGRATION.md` and the converter. Its distinctive asset is what jichi most lacks — **VS
Code and JetBrains extensions**. jichi has Emacs and an ACP server, which is not a comparable
answer for someone who wants an assistant inside their IDE.
