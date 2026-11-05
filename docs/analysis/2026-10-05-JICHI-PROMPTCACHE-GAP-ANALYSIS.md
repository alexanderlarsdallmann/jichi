# jichi gap analysis: prompt caching (data flow)

*Read-only survey of `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey), HEAD `1b71247d`.
Method: find the one thing the area is definitely lacking; verify every
load-bearing claim against source; flag inferences; appendix reproducible.*

## TL;DR

The one thing lacking: **an affirmative test that the prompt-cache prefix is
byte-stable across turns — and any detection of a *one-time* break.** jichi
knows its biggest structural cost is an uncached prefix (`doctor_cache.sh`: "0%
over 1.24 billion input tokens"), and it built two real defenses — the M73
deadband (stops calibration jitter from moving the system-prompt truncation
byte) and the `jc_prefix_watch` sentinel (warns on 3 *consecutive* system-prompt
changes). But the sentinel fingerprints **only `system_msg`** (not the tools
array, not the tail) and fires only on a *chronic* pattern, and the
`prefix_churn.sh` test drives exactly that chronic case — not a stability
proof. The dominant cache-killer, a single turn that changes the *structure*
of the prefix (tools array, a mode switch, compaction replacing the
conversation), has no test asserting it doesn't happen and no alarm when it
does: PROMPT_CACHING.md:158 tells the user to "verify the system prompt and
tool list aren't changing between turns" — by hand.

## 1. What the area claims

`PROMPT_CACHING.md` documents a feature with a *cost* story, not just a
mechanism: "On a multi-turn session this cuts both cost and latency: a cache
read bills at roughly 0.1× the normal input rate." Two backends — Anthropic
(explicit `cache_control` breakpoints, M31b) and OpenAI-compatible (automatic,
server-side; jichi only *reports* counts, M31a). The Anthropic path rests on a
named precondition: "This relies on the rendered prefix being **byte-stable
across turns**, which jichi already guarantees: the system prompt injects only
cwd + platform (no timestamp), and the repo map, rules, memory, skills
catalogue, and tool array are session-stable and built deterministically."

The register treats the guarantee as load-bearing and has already found it
broken once: "a live violator: the M77 calibration ratio moves a little on
every model call … **every ratio wobble moved the truncation byte**. The system
prompt churned each turn; the cached prefix … re-billed forever, silently."
The fix was the M73 deadband + the `jc_prefix_watch` sentinel.

## 2. The crux: the sentinel covers one chronic, one-scoped case — not the guarantee

Verified in source:

- **Scope.** The sentinel hashes exactly one input: `jc_prefix_hash(opts.system_msg)`
  (`jc_agent.c:5219-5220`). `jc_prefix_hash` takes a single string
  (`jc_prefix.c:8`). The tools array — which the doc says is cached *in the
  same breakpoint* as system ("this single marker caches the tools + system
  prefix together") — is not in the hash. A turn that changes the tools array
  breaks the cache exactly as a system-prompt change would, but the sentinel
  never sees it.
- **Pattern.** `jc_prefix_watch_track` fires only on
  `JC_PREFIX_CHURN_STREAK` **consecutive** changes (`jc_prefix.c:23`), and the
  doc states why: "one change is normal life (a memory write, a mode switch,
  midnight moving the date line)." The alarm is deliberately slow to accuse a
  *chronic* pattern — and therefore silent on the *one-time* structural break,
  which is the one that resets the cached prefix and re-bills the whole
  conversation from that point on.
- **Test.** `prefix_churn.sh` drives "four turns that each `remember` a new
  note … the sentinel fires exactly once" and a phase that "must stay silent."
  It is a test of the *alarm*, not of *stability*: it never asserts that the
  prefix is byte-identical across two plain turns. The `jc_promptcache_plan`
  unit test (`tests/test_promptcache.c`) checks breakpoint *placement* (pure),
  not *stability* of the bytes the breakpoint rides on.

## 3. Why this is "definitely lacking"

1. **No affirmative stability test exists.** The doc's central precondition —
   "byte-stable across turns, which jichi already guarantees" — is upheld, per
   the doc itself, "by every system-prompt section author by hand, with no
   runtime checker" (the sentinel being the runtime checker *for* the chronic
   system-prompt case only). There is no driver that renders the request on
   turn 1 and turn 2 of a plain session and asserts the prefix (system **and
   tools** **and** the cached tail) is byte-identical. The register's own
   doctrine is that a guarantee is only as good as a red-first test; this one
   was found by *symptom* (0% hit-rate over billions of tokens) and fixed
   reactively.
2. **The one-time break is the dominant cost, and it is untested *and* unalarmed.**
   A single turn that changes the tools array (a `--tool-profile` switch, a
   mode change that adds/removes tools, a new MCP server), or compaction
   (analysis 18) replacing the conversation, invalidates the cache for every
   subsequent turn. The sentinel's "one change is normal life" rule means no
   `[prefix]` warn fires — the user only finds out that the hit-rate line
   "collapsed" (PROMPT_CACHING.md:281) *after* the fact, by checking `/cache`
   — which the doc itself reduces to a manual step.
3. **The manual step is named in the doc and not mechanized.** "verify the
   system prompt **and tool list** aren't changing between turns"
   (PROMPT_CACHING.md:158). The system prompt has the sentinel; the tool list
   has the sentence. The asymmetry is the gap.

## 4. Alternatives considered (why this is the gap)

- *The OpenAI path (no explicit control).* Documented: jichi only *reports*
  server-side counts there (M31a). No stability claim jichi is responsible for.
- *The chronic system-prompt churn.* The one case that **was** a live bug —
  calibration jitter moving the truncation byte. Fixed by the M73 deadband,
  watched by the sentinel, driven by `prefix_churn.sh`. Closed.
- *Breakpoint placement.* Pure and unit-tested (`test_promptcache.c`). Closed.
- *Per-session vs aggregate hit-rate (M592).* The "8.0% describes no session
  that ever ran" trap is fixed and tested. Closed.
- The residual: the **affirmative stability proof** (nothing renders turn 1 and
  turn 2 and asserts the prefix — system *and* tools — is unchanged) and the
  **one-time break** (a single structural change that resets the cache), neither
  covered by any test or by the sentinel.

## 5. The register's honesty about the rest

- `doctor_cache.sh` (M326w) — the "0% over 1.24 billion input tokens" symptom;
  the three-check teeth that doctor and the audit "reach the same verdict from
  the same arithmetic."
- `PROMPT_CACHING.md:254-281` — the live-violator story, the deadband, the
  sentinel, and the "one change is normal life" rationale.
- `PROMPT_CACHING.md:158` — the residual manual step: "verify the system prompt
  **and tool list** aren't changing between turns." Named; not mechanized.

## 6. Implications

- **The cost guarantee is only as strong as its untested precondition.** When a
  one-time structural change breaks the prefix, the whole conversation re-bills
  at full input rate for every subsequent turn — the "single largest structural
  cost" (`doctor_cache.sh`) — and nothing catches it short of the hit-rate line
  collapsing, which the doc leaves to the user to notice.
- **The tools array is a blind spot by construction.** It is in the same
  Anthropic breakpoint as system, hence *as cache-critical*, yet the sentinel
  hashes only `system_msg`. Any turn that changes the tool set (profile switch,
  MCP connect, mode) silently resets the shared prefix.
- **It interacts with every other analysis.** Compaction (18) rewrites the tail;
  routing (9) switches the model; the daemon (7) keeps the prefix warm. All are
  legitimate resets, all invisible to the sentinel because it is a *chronic
  system-prompt* alarm, not a *prefix stability* assertion.

## 7. Recommendation

1. **Affirmative stability test (mock, offline).** Render the Anthropic prefix
   on turn 1 and turn 2 of a plain session and assert the bytes — system *and*
   the tools array — are identical. Red if any section nondeterministically
   rebuilds; green today, and the guard for the "by hand" contract.
2. **Widen the sentinel to the shared breakpoint.** Hash a canonical form of
   the tools array too, and emit a distinct `prefix_reset` telemetry event on a
   one-time change (not the loud `[prefix]` churn warn). Mechanizes the
   "verify the tool list isn't changing" half of :158.
3. **One line in PROMPT_CACHING.md** beside the sentinel: it covers chronic
   *system-prompt* churn; a one-time break of the shared prefix is a
   legitimate reset currently visible only in the collapsed hit-rate.

Rejected: *a real tokenizer to prove stability* — byte-identity is a `cmp` of
two rendered prefixes, no tokenizer needed. *Alarm on every change* — the doc
is right that one change is normal life; step 2 separates the quiet *reset*
event from the loud *churn* alarm.

## 8. Limitations and honest counterarguments

1. **The one-time break is often legitimate, so detection ≠ prevention.**
   Routing, compaction, a new MCP server *should* reset the prefix. The gap is
   that a *legitimate* reset and a *buggy* nondeterminism are indistinguishable
   without the hit-rate collapsing; step 2 makes both observable as distinct
   events.
2. **The chronic case — the only silent bug found — is fixed and tested.** I
   am not claiming the sentinel is absent or the deadband weak; I am claiming
   the guarantee they serve is under-tested (chronic-system only) and the
   sibling structural case is un-covered.
3. **I have not driven a two-turn byte diff.** §2's claims are from the source
   (the single-string hash, the streak logic) and the test's scope; the
   affirmative-stability gap is an absence verified by reading the suite, not by
   a diff that fails. Step 1 converts it.
4. **On the automatic (OpenAI) backends, stability is the server's concern.**
   The byte-stability contract is specifically the Anthropic explicit-breakpoint
   path, where this gap lives.

## 9. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

```sh
# the guarantee (admitted upheld by hand until M73)
sed -n '254,258p' docs/PROMPT_CACHING.md      # "by hand, with no runtime checker"
sed -n '20,30p'  docs/PROMPT_CACHING.md       # "byte-stable … jichi already guarantees"

# the sentinel: system_msg ONLY, 3-consecutive
sed -n '5219,5220p' src/chat/jc_agent.c       # jc_prefix_hash(opts.system_msg)
sed -n '8p'         src/util/jc_prefix.c      # hash takes one string
sed -n '23,45p'     src/util/jc_prefix.c      # streak logic
grep -n "JC_PREFIX_CHURN_STREAK" include/jc_prefix.h   # 3

# the test drives the chronic case (not a stability proof)
sed -n '1,12p' tests/smoke/prefix_churn.sh

# breakpoint placement is pure-tested; stability is not
grep -n "byte\|stable\|identical\|two turn" tests/test_promptcache.c   # (empty)

# the manual step (tool list un-mechanized) + the cost that motivated the audit
sed -n '156,160p' docs/PROMPT_CACHING.md
sed -n '5,10p'    tests/smoke/doctor_cache.sh # "0% over 1.24 billion … single largest structural cost"
```

**Verdict line for the register this could seed:** *jichi's prompt-cache cost
guarantee ("a cache read bills at ~0.1×") rests on the rendered prefix being
byte-stable across turns, a contract the doc admits was upheld "by every system-
prompt section author by hand, with no runtime checker" until M73 found its live
violator (calibration jitter moving the truncation byte). M73 + the
`jc_prefix_watch` sentinel close exactly that chronic system-prompt case, and
`prefix_churn.sh` tests the sentinel's alarm — but the sentinel hashes only
`opts.system_msg` (`jc_agent.c:5219`), not the tools array it shares the
Anthropic breakpoint with, and fires only on 3 *consecutive* changes, so the
one-time structural break that resets the whole cached prefix (tools, tail,
compaction, routing) is neither affirmatively tested for stability nor observed
as an event — PROMPT_CACHING.md:158 leaves it to the user to "verify the tool
list isn't changing between turns." Revisit when: a two-turn byte-diff asserts
the shared prefix (system + tools) is identical on a plain session, and a
one-time reset is emitted as a distinct `prefix_reset` telemetry event separate
from the chronic `[prefix]` churn alarm.*
