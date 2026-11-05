# JICHI GATE_INTEGRITY.md — gap analysis (analysis 48)

## TL;DR

*Read-only survey of docs/GATE_INTEGRITY.md at HEAD 1b71247d (403 lines), cross-checked against src/chat/jc_envelope.c, jc_agent.c, tests/measure/strict_green_fp.py, 2026-10-05.*

*GATE_INTEGRITY.md is a first-class design+status document: the three incidents, the component table, §5.1 REFUSE-THE-GREEN (shipped M332), and §8b's retroactive false-positive measurement (0/21 green scoped runs, per tests/measure/strict_green_fp.py) all check out against code. The gap is the recurring one, in its purest form yet: **all three line-number anchors in the doc are stale** — and the doc is literally about the failure class that stale pointers are a member of (M492: "the pointer resolved; only the claim rotted").*

## 1. What the document is, and what I verified true

GATE_INTEGRITY.md is a design + status document (not a pure reference page):
three incidents, a component-by-component table of why existing guards miss
each one, the §5.1 REFUSE-THE-GREEN design and its M332 shipping status, and
§8b, a **retroactive false-positive measurement** ("0 downgrades in 21 green
scoped runs", GATE_INTEGRITY.md:212, produced by
`tests/measure/strict_green_fp.py` — file exists, checked).

Verified true against code:
- `--strict-green` / `--no-strict-green` / config `strictGreen`, default off
  (jc_config.c:1311-1312, main.c:320/336).
- The rule itself: `jc_env_refuse_green(strict_green, out_of_scope_seen,
  outcome)` = `strict_green && out_of_scope_seen > 0 && outcome == JC_ENV_OK`
  (jc_envelope.c:951-954); call site downgrades to `JC_ENV_SCOPE_TAINTED`
  (jc_agent.c:5543-5547) — matching the doc's "a green earned after an
  out-of-scope change is not a green".
- The doc's explicit caveat holds: refuse-green fires only for OUT-OF-SCOPE
  changes; §5.1 even tells users who want the agent to fix the gate to put it
  IN scope (line 101) — i.e. the documented configuration in which the
  protection is switched off. The doc names this; it is not hidden.

## 2. The gap: all three line-anchors are stale (the doc about rot is itself rotted)

This is the recurring class (#39 DEFERRED, #44 HOOKS) in its purest form, and
it is *self-referential*: GATE_INTEGRITY.md exists precisely to say "the
pointer resolved; only the claim rotted" (quoting the M492 lesson). Yet its own
line-number anchors have rotted, and no lint could catch them (M711 excluded
measured/dated records; the anchors are neither — present-tense code
locations that simply drifted).

All three cite `jc_agent.c`; none is correct at HEAD 1b71247d:

| Doc anchor | Doc claim | Real location (HEAD) | Drift |
|---|---|---|---|
| `:1476` | "completion verify passes → env->outcome = JC_ENV_OK" (lines 48, 62) | `:2478` (in-loop) | off by ~1,000 lines |
| `:2783` | "M83 out-of-scope diff runs here" (lines 48, 112) | `:5513` (`env_report_out_of_scope`) | off by ~2,730 |
| `:2797` | "journal end reports outcome: ok" (line 66, 112) | `:5543-5547` (refuse-green block) | off by ~2,750 |

The `:2783–:2797` range appears twice (line 112, the §5.1 "Cost: a few lines
at …" and line 48). The doc was last touched 2026-09-16 (M630); `jc_agent.c`
has since grown and shifted these call sites. The anchors were accurate when
M332 shipped and now point ~1,000–2,750 lines away from the code they name.

**Why nothing flags it (verified):** `reading_refs_lint.sh` — the lint that
forbids line-number anchors and enforces `file.c:function` (line 69: "LINE-
NUMBER anchor (forbidden, use file:function)") — scans **`docs/reading/*.md`
only** (line 27: `guides=$(ls $SMOKE_ROOT/docs/reading/...)`). GATE_INTEGRITY.md
lives at `docs/` top level: outside that fence. And its anchors are bare
`jc_agent.c:NNNN` with no function name, so even a top-level port of the lint
would need the function-name contract to be useful. The *prose* claims (the
rule, the outcome names, the 0/21 measurement) are all correct — this is the
M492 class: **the pointer resolved; the line number rotted.**

## 3. Corrections (ready to apply)

1. Replace the three stale anchors with function names (the tree's own
   contract): `jc_agent.c:1476` -> "the in-loop completion verify
   (jc_env_verify at :2478)"; `:2783` -> `env_report_out_of_scope`
   (:5513); `:2797` -> the refuse-green block (:5543). Function-anchored,
   never line-anchored, so M170-scale refactors stop rotating this doc.
2. One-line note at the top: "code locations in this doc were checked at
   HEAD <short-hash> on 2026-10-05" — the dated-record form M711 blesses.
3. (Optional, structural) extend `reading_refs_lint.sh`'s scan to `docs/*.md`
   top level, or move GATE_INTEGRITY.md's code-prose sections under
   `docs/reading/`. This is the M711-class decision: which top-level docs are
   "prose about code".

## 4. Appendix (reproducible, all from the jichi checkout)

    # the three stale anchors, as written
    grep -n "jc_agent.c:1476\|:2783\|:2797" docs/GATE_INTEGRITY.md

    # their real locations
    grep -n "env->outcome = JC_ENV_OK" src/chat/jc_agent.c | head -1   # 2478
    grep -n "env_report_out_of_scope(app" src/chat/jc_agent.c | tail -1 # 5513
    grep -n "jc_env_refuse_green(app" src/chat/jc_agent.c | tail -1     # 5543

    # the lint that would catch this, and its scan fence
    grep -n "forbidden, use file:function" tests/smoke/reading_refs_lint.sh  # 69
    grep -n "docs/reading/" tests/smoke/reading_refs_lint.sh | head -1       # 27

    # the §8b measurement and its tool
    grep -n "0 downgrades in 21" docs/GATE_INTEGRITY.md                        # 212
    ls tests/measure/strict_green_fp.py

## 5. Limitations (honest)

- I did not re-run `strict_green_fp.py` (it walks 138 journal files from the
  zigodot bench; the doc's 0/21 stands as a dated measurement, and M711's
  whole point is that such numbers are not stale-able).
- The in-loop `JC_ENV_OK` at :2478 vs the post-loop :5527: the doc's "the
  verify sets JC_ENV_OK, then the out-of-scope check runs" sequence matches
  the :5513→:5543 call order; I cite :2478 as the *first* set because that is
  the loop-internal completion the diagram names, and the diagram's sequence
  (verify → report → refuse-green) is exactly the 2478/5513/5543 order.
- Whether the doc *should* carry line anchors at all is a house-style
  question the tree already answered for `docs/reading/` (it should not);
  my correction #1 follows the tree's own rule rather than inventing one.

## Verdict

GATE_INTEGRITY.md is **accurate in every prose claim** and **stale in every
code location** — the two classes the tree has now measured five times. The
self-referential point is the real one: this is the document that argues
line-number rot is the M492 failure mode, and it carries three instances of
it, undetectable because the only lint that forbids the form scans a
subdirectory this document does not live in. Correction #1 (function
anchors) is the same fix the tree mandates elsewhere; #3 (widen the lint or
relocate) is the class fix, and it is a genuine design decision — which is
why I have not made it, only named it.
