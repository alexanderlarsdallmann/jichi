# What jichi's cost & routing surface is missing

*Analysis 9 of 12. Read-only survey of jichi at HEAD `1b71247d` (M898 era),
2026-10-05. Companion to the capability, UX, debugger, curriculum,
presentations, editor, platform, orchestration, MCP, secrets, daemon, and
snapshot analyses. Same method: the finding is the one feature "definitely
lacking", evidenced against the record, with every inference flagged.*

**Seam type:** program flow (routing/fallback decisions) + data flow
(token → cost conversion → budget enforcement).

---

## 1. TL;DR

jichi's *measurement* of money is the most thoroughly paranoid accounting
seam in the tree: costs are computed per model call with cache-read/write
rates (`jc_config_cost`), the **absence of pricing is a named doctor finding
(M194: "All 4471 model calls in the dogfood log reported
`cost=$0.0000` … indistinguishable from 'this run was free'" —
`src/main.c:12393`)**, half-priced configs get their own warn (":12413"),
cache pricing has a deliberately narrower warn (M31c), a lint guarantees no
test harness ever spends money (M544, `priced_model_lint.sh`), and the
envelope records *which* budget stopped a run (`tripped`, M97).

And then the enforcement half does not exist: **jichi has no cost budget.**
`include/jc_envelope.h:52–58` — the exhaustive enum of budgets an unattended
run is bounded by — is `NONE / TOKENS / DEADLINE / TOOLCALLS / READS`.
There is no `JC_BUDGET_COST`. Everything the docs promise about money —
`/cost`, the exit total, the telemetry cost column, the ambient panel
(M431f) — is a *readout* of accumulated tokens converted through whatever
prices are configured. Under tiered routing (cheap model first, escalate to
a strong model on stall/error/context-fill, `docs/ROUTING.md`) and provider
fallback, **a token budget is not a dollar budget**: the same
`--budget-tokens 500000` can cost $0.30 or $25 depending on which tier
handled which turns. The M194 source comment names this exact seam — it
warns that the zero-cost readout defeats "any cost-based budget" — and the
register contains no row saying a cost-based budget is planned, rejected,
or absent. It is simply not there.

## 2. What the surface *does* have (for precision)

- **The cost engine.** `jc_config_cost` (`src/config/jc_config.c:2404`):
  input/output/cache-read/cache-write at per-1M-token rates; cached reads
  fall back to the input rate "when unpriced (the common case)". Prices
  live in the config (`inputCostPer1M` / `outputCostPer1M` / cache rates).
- **The measurement integrity work.** M194 (unpriced → everything reads
  `$0.00`, doctor now says so, `src/main.c:12389`); M31c (cache-pricing warn
  fires *only* on a model that IS priced — "checked before … which
  deliberately only fires on a model that IS priced"); `priced_model_lint`
  (M544: "no harness spends money by default" — a bench that declared a real
  model + real gateway as *defaults* was caught by the lint); the M282
  retry-cost accounting ("the cost of a retried attempt invisible in
  telemetry", `jc_agent.c:282`).
- **Routing.** `docs/ROUTING.md` (207 lines): fast-first, escalate on
  **stall / error / context-fill** — the three triggers, all *wire*
  triggers, none of them money. `route_stall.sh` (M23a) proves escalation
  against two mock servers and asserts the telemetry carries
  `reason=stall`.
- **Fallback.** Provider failover across configured servers (the envelope
  walks to a reachable server; `doctor` shows the routing state).
- **The budgets that DO exist.** `JC_BUDGET_TOKENS` (with a
  four-fifths "tell the model it is running out" nudge, `jc_envelope.c:273`),
  `JC_BUDGET_DEADLINE`, `JC_BUDGET_TOOLCALLS`, `JC_BUDGET_READS` (M98);
  `tripped` records which one stopped the run (M97); the ambient panel
  (M431f) shows them on cadence.

## 3. The gap, stated

> **jichi's cost & routing surface has no cost budget: the run is bounded
> in tokens, wall-clock, tool calls and reads — the four `jc_envelope.h`
> kinds — but never in dollars, while the machinery exists precisely to
> convert tokens into dollars and the docs' money readouts (/cost, exit
> total, telemetry) measure spend that no stop condition watches. M194's
> source comment names the seam — an unpriced model defeats "any
> cost-based budget" by making every cost read zero — and the register
> holds no row for a cost-based budget, planned or rejected: the absence
> is unrecorded, and the enforcement half of a rigorously built
> measurement is missing.**

## 4. Why "definitely lacking"

The case for the alternative readings, and why each loses:

- **"The token budget IS the cost budget."** It is a proxy that fails
  exactly where jichi's architecture makes the proxy worst: routing and
  fallback change which model serves which turn *mid-run*, so the token→dollar
  rate is not constant. A 500k-token budget on a cheap tier is not the same
  exposure as 500k on a frontier tier. jichi's own M194 comment reasons in
  dollars ("indistinguishable from 'this run was free'") and assumes a
  "cost-based budget" is a thing that could exist — the source names the
  seam; it is just never built.
- **"Costs are for readouts; stopping is for operators."** Fair for the
  interactive case (M431f's panel exists so a human can watch). But the
  envelope is the *unattended* story — budgets, deadline, verify gate — and
  it is the one place where "stop at $X" is structurally needed and
  structurally absent. The `tripped` enum (M97) even has a slot for
  "which budget stopped the run"; cost is not a value it can take.
- **"A dollar budget is unreliable, so it is right to omit it."** The
  strongest real argument — prices are set by the user and may be stale or
  wrong (the half-priced warn exists for a reason). But the surface already
  treats prices as a *user-supplied input the system audits* (doctor warns
  on unpriced/half-priced); extending that trust to a stop condition is the
  missing step, not the risky one. And the omission is unrecorded: there is
  no DEFERRED row, no ROADMAP note, no "we don't do money gates" decision —
  the seam is silent rather than argued.

## 5. Evidence

- **`include/jc_envelope.h:52–58`** — the budget enum, exhaustive:
  `NONE / TOKENS / DEADLINE / TOOLCALLS / READS`. No cost kind. (Verified.)
- **`src/chat/jc_envelope.c:246,273–274,320,343–349`** — every enforcement
  path keys on `e->tokens_used` vs `e->budget_tokens`, with a
  four-fifths pre-warning. Nothing reads a dollar figure. (Verified.)
- **`src/main.c:12389–12416`** — M194: "All 4471 model calls in the
  dogfood log reported `cost=$0.0000` … `/cost`, the session exit total, the
  telemetry cost column and **any cost-based budget** all read zero, which is
  indistinguishable from 'this run was free'." The fix is a *readout*
  warning; the budget the comment names is not built. (Verified.)
- **`src/config/jc_config.c:2404`** — `jc_config_cost`: the conversion the
  budget would need, already present. (Verified.)
- **`tests/smoke/route_stall.sh` (M23a)** — escalation is proven against
  mock servers with telemetry assertion; the trigger set (stall/error/
  context-fill, `docs/ROUTING.md`) is wire events, not spend. (Verified.)
- **`tests/smoke/priced_model_lint.sh` (M544)** — the money *discipline*
  exists for the harness: "no harness spends money by default". (Verified.)
- **grep, `docs/DEFERRED.md` + `docs/ROADMAP.md`** — no row implements or
  decides a cost-based budget. (The register *does* carry the incident such
  a budget exists to prevent — def-114, "The same craft A/B on a frontier
  model", 2026-08-10: two runs consumed the key's remainder, sixteen then
  failed on the gateway's own `429 budget_exceeded` which "named the key,
  the spend and the cap", and the completed session's results were lost with
  "the spend unrecoverable" — the gateway's per-key cap was the only
  dollar stop condition in play, and it stopped *everything*.)
  (Zero hits for a budget row verified.)

## 6. Implications

1. **The unattended dollar exposure is open.** A `--auto` envelope on a
   gateway whose strong tier is a frontier model, with routing escalation
   armed, has no way to say "stop at $5". The deadline is the nearest
   proxy, and it bounds *time*, not *spend* — a fast frontier model burns
   money faster than a slow one at identical token rates.
2. **M194's fix made the zero honest; it also exposed that the zero has no
   consumer.** The doctor warns that costs are meaningless when unpriced —
   but the meaningful case (priced models) still has nothing that *acts* on
   the number. Measurement without enforcement is the same shape as the
   daemon's claim without a mechanism, and the snapshots' preview without a
   checked destruction: the readout side is rigorous, the action side is
   absent.
3. **The register's silence is the tell.** jichi records deliberate
   absences (no gradebook: "it is someone else's software"; no cohort view;
   C++ gated by decision). A cost budget has no such row. Per the tree's own
   epistemic shape — the check reaches as far as a machine can run — the
   dollar is the one unit whose enforcement is a *policy* decision the
   register has not made or recorded.

## 7. Recommendation

1. **Add the missing enum value and the missing stop.**
   `JC_BUDGET_COST` with a `--budget-usd` flag (or `budgetUsd` in config):
   each model call already computes its cost through `jc_config_cost`;
   accumulate it in the envelope next to `tokens_used`, and trip the run at
   the same point the token budget trips (with the same four-fifths nudge,
   which is the mechanism that makes a hard stop survivable rather than
   mid-write). `tripped` (M97) gets its new value for free.
2. **Make it refuse-to-arm on unpriced models.** The guard is already
   written — M194's "every cost reads $0.00" is exactly the condition under
   which a cost budget would silently never trip. Reuse it: arming
   `--budget-usd` with `input_cost <= 0 && output_cost <= 0` should be an
   error, not a warning, because a budget that can't trip is worse than
   none. (The half-priced warn gets the same treatment.)
3. **Red-first driver, no money spent.** `priced_model_lint` (M544) shows
   the pattern: a mock server with *declared prices in the config* (the
   mock never bills) and an assertion that the run trips `JC_BUDGET_COST`
   with the right `tripped` value — offline, deterministic, zero spend.
4. **Record the decision either way.** If the operator judges a dollar gate
   too crude (prices are user-supplied, routing makes the rate vary), that
   is a DEFERRED row with the reason — the register's form — so the absence
   is a boundary, not a silence.

Rejected, in the register's spirit: *estimate cost from the token budget by
multiplying by the strong tier's rate* — it bounds the worst case but makes
the cheap tier a lie (every run looks maximally expensive), and jichi's own
measurement work exists to avoid exactly that kind of constant-rate
shortcut; *the deadline as a spend proxy* — it bounds time, and jichi's
fast-tier configuration is fast on purpose.

## 8. Limitations and honest counterarguments

1. **Policy-shaped, not defect-shaped.** Unlike the daemon (a stale claim)
   or the snapshot (an unchecked command), this is a *capability* that was
   never specified: one could argue the product decision "operators set
   their own stop conditions" is coherent, and the token/deadline pair
   covers the common local/free case where cost is literally $0.
2. **The proxy is rarely wrong in practice.** For a single-model
   configuration the token budget IS a cost budget at a constant rate; the
   gap bites only when routing/fallback changes the rate mid-run — the
   configuration jichi's own docs recommend, but not the one most local
   setups use.
3. **Absence-based on a zero grep.** "No cost budget" rests on the enum
   (exhaustive, verified) plus the register grep (verified empty); if a
   cost-stop exists under a name I did not search ("spendCap", "moneyLimit"),
   §3 weakens — the search covered the envelope header, the enforcement
   file, and both registers for `cost|usd|dollar|spend|price` + `budget`.
4. **Not executed.** No priced mock server was spun up here; the
   recommendation's driver is specified, not built. Everything is read from
   the record at HEAD `1b71247d`.

## 9. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey), at HEAD `1b71247d`:

```sh
# the budget universe (the exhaustive enum)
sed -n '52,58p' include/jc_envelope.h                     # NONE/TOKENS/DEADLINE/TOOLCALLS/READS — no COST

# the enforcement paths (all key on tokens_used)
grep -n "budget_tokens\|tokens_used" src/chat/jc_envelope.c | head

# M194: the source comment that names "any cost-based budget"
sed -n '12389,12416p' src/main.c

# the conversion a budget would need
sed -n '2404,2420p' src/config/jc_config.c                 # jc_config_cost

# routing triggers (wire events, not spend)
sed -n '1,40p' docs/ROUTING.md
grep -n "reason=stall\|mock" tests/smoke/route_stall.sh | head -4

# the money discipline that exists (harnesses)
head -12 tests/smoke/priced_model_lint.sh

# the register's silence (a budget row does not exist; def-114 is the incident, not a budget)
grep -n -i "cost budget\|usd\|dollar\|spend" docs/DEFERRED.md docs/ROADMAP.md | grep -iv "M194\|cost=\|priced\|budget_exceeded\|the spend" | head
grep -n "budget_exceeded" docs/DEFERRED.md   # def-114: the gateway cap incident the budget would have prevented

# the readouts that measure but never stop
grep -rn "budget_panel" src/main.c | head -3               # M431f ambient panel
grep -n "tripped" include/jc_envelope.h                     # M97: which budget stopped the run
```

**Verdict line for the register this could seed:** *jichi's cost & routing
surface measures spend rigorously (per-call cost with cache rates, M194's
"4,471 calls at $0.0000" finding, M544's no-harness-spends-money lint) but
enforces none of it: the envelope's budget enum is
TOKENS/DEADLINE/TOOLCALLS/READS with no cost kind, every stop path keys on
`tokens_used`, and under tiered routing a token budget is not a dollar
budget — while M194's own source comment names "any cost-based budget" as
the thing the zero-cost readout defeats. Revisit when: `JC_BUDGET_COST`
trips a priced-mock run offline (red-first, `priced_model_lint`'s pattern) —
or a DEFERRED row records the policy decision against it.*
