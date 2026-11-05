# M956 — search_code's pattern-error classification, made honest on non-GNU grep

**Status:** shipped. The `search_code` tool, when the model writes a broken
extended-regex, tells the model *the pattern is the problem* (so it can fix it)
rather than a generic "grep failed." That worked on GNU grep and the four BSD
greps the matrix had. The 2026-10-15 platform campaign met a **fifth** grep —
Alpine's, which is **busybox** — and it broke. This page records the measured
mechanism, why the fix is what it is, and the instrument design, written for a
junior or self-learner who has never touched grep's exit-code contract.

## 1. The symptom, in the words the model received

A model sends `search_code {"pattern":"unbalanced("}`. On GNU grep it gets:

> error: the pattern is not a valid extended regular expression (grep -E):
> Unmatched ( or \( -- put a backslash before a literal ( ) | + ? { or [

Good. The model can act on it. On **Alpine (busybox grep)** it used to get:

> error: search failed -- grep exited with an error reading the path
> (**the pattern itself is valid**)

That is not just "no help" — it is an **affirmative mis-telling**. The tool told
the model its pattern was fine when the pattern is the entire problem. A model
that believes that will retry the same pattern, which is exactly the loop the
error message was written to prevent (M731: a model repeated a warned-on search
thirteen times).

## 2. The exit-code contract, and where busybox breaks it

grep's portable contract: **0 = matched, 1 = ran fine but no match, ≥2 = grep
itself failed.** jichi's failure path keys on `≥2`: when a search returns nothing
and grep exits `≥2`, it asks a probe "is the *pattern* the thing that failed, or
was it the *files*?" and says so differently in each case.

The probe, before this fix, was:

```
GREP_OPTIONS= grep -E -e '<pattern>' /dev/null 2>&1
```

i.e. ask grep to compile the pattern against an **empty** file. The reasoning was
sound for every grep jichi had tested: `/dev/null` cannot match, so a *valid*
pattern exits 1 and an *invalid* one exits 2 with grep's own diagnostic. GNU,
FreeBSD, NetBSD and OpenBSD all report the compile error even when the input is
empty.

busybox does not. **It defers the "bad regex" error until it has actually read
input.** Measured on this box (the matrix, reproduced verbatim):

| form                                | GNU grep                    | busybox grep       |
|-------------------------------------|-----------------------------|--------------------|
| `grep -E -e 'unbalanced(' /dev/null`| rc=2  `Unmatched ( or \(`   | **rc=1  (silent)** |
| `grep -E -e 'unbalanced(' <empty>`  | rc=2  `Unmatched ( or \(`   | **rc=1  (silent)** |
| `printf x \| grep -E -e 'unbalanced('` | rc=2  `Unmatched ( or \(` | **rc=2  `bad regex 'unbalanced('`** |
| `grep -rnIE -e 'unbalanced(' <dir>` | rc=2  `Unmatched ( or \(`   | **rc=2  `bad regex 'unbalanced('`** |

The trigger is **whether grep read readable content**, not the flag set (`-E`
vs `-rnIE`) — cases 3 and 4 both fire busybox, cases 1 and 2 both stay silent.

So on Alpine the probe exited 1, `pattern_error()` read that as "pattern is
fine," and the tool fell through to the "the pattern itself is valid" message.

## 3. Why the handoff that opened this was wrong, and why that is fine

The handoff note from the other agent that started this milestone (in
`docs/internal/2026-10-15-platform-tests-handoff.md`) described the mechanism as:
*"the tool takes the WARNING path, breaking both the `is_error` check and the
substring."* That prediction was **off**, and this session's first job was to
measure rather than trust it. What actually happens:

- `res.is_error` **is** 1 on Alpine (the main search `grep -rnIE … <dir>` exits
  2 once it touches a real file — so the *search* does surface the error; only
  the *probe* misses it).
- The tool does **not** take the warning path. It reaches the `else` branch
  ("reading the path, the pattern is valid") because `pattern_error()` — fed by
  the blind `/dev/null` probe — says the pattern is fine.

**Lesson, written down because it cost a re-plan:** a handoff is a *report*, not
a *measurement*. The one that matters (the probe's exit on Alpine) was not in it.
Every "the fix is X" that a handoff offers is a hypothesis until this box runs
the probe and prints its rc. The two handoff fix options were re-derived from the
measured table, not the handoff's — and both turned out to be worse than the
minimal fix the table actually supports (below).

## 4. The fix: give the invalid-probe readable input — and keep M732's probe empty

The obvious one-line fix is "make the probe read a file instead of `/dev/null`."
**That is not safe**, and the reason is the second, less-obvious use of the same
probe. `pattern_probe` is called from **two** places with **opposite** needs:

1. **Invalid detection** (`pattern_error`) — needs a *readable* target so busybox
   reports its compile error (rc 2).
2. **M732 warning detection** (the `"(no matches)"` path) — needs a *cannot-match*
   target, because it interprets rc 1 as "accepted, no match, maybe warned."

The M732 need is the subtle one. Consider a pattern that **both** makes GNU warn
**and** matches content: `.*` matches any line (including an empty one).

```
printf x | grep -E -e '.*' /dev/null   -> rc=1   (empty, cannot match: honest)
printf x | grep -E -e '.*' <one line>  -> rc=0   (it MATCHED the line)
```

If the M732 probe read a line, `.*` would return rc 0, and the check
`pattern_probe(...) == 1` would be false — the "grep warned" feedback would be
**lost for every pattern that also matches**. `/dev/null` keeps rc 1 honest for
M732.

So the two needs genuinely cannot share one target on busybox. The fix is to
**split the instrument at the call site**, parameterizing the probe's target:

```c
/* with_content == 1:  printf x | grep  -- readable; invalid-detection */
/* with_content == 0:  grep /dev/null    -- cannot match; M732 warning  */
static int pattern_probe(const char *pattern, int with_content, char *msg, jc_size cap)
```

- `pattern_error()` (invalid detection) calls it with `with_content = 1`.
- The M732 `"(no matches)"` check calls it with `with_content = 0` — **unchanged
  behaviour on every platform.**

That is the whole product change: a parameter, two call sites, no new branches in
the loop, no platform detection, no keyword matching against grep's message
vocabulary. The classifier it feeds (`rc ≥ 2` = pattern error) is the one that
was already correct on GNU and the BSDs; we only changed *what the probe reads*
so busybox stops being blind to it.

**Why not the alternatives** (each rejected with the measurement that killed it):

- **Keyword set on the main search's stderr** (the first draft of this plan):
  works, but builds a classifier that must be maintained against every grep's
  error wording ("Unmatched…", "bad regex…", and whatever the next BSD says).
  The probe-target fix reuses the existing `rc ≥ 2` classifier that both greps
  already agree on. Fewer moving parts, no vocabulary to keep in sync.
- **A runtime "which grep am I" probe**: a third popen per search, and it keys on
  a string match against grep's banner — more fragile than the rc contract, and
  it is exactly the "branch on the platform" shape this codebase avoids.
- **Fix M732 to also fire on busybox**: there is nothing to fix. busybox does not
  warn on `(?:...)` at all — it accepts it silently (and, per M817's BSD finding,
  some greps *match* on it). That is a property of busybox, not a jichi defect.
  It is documented below as GNU-only, not hidden.

## 5. What the user and the agent now see, per platform

Honest, because the behaviour genuinely differs by grep and the differences are
now *stated* rather than accidentally produced:

- **Broken pattern, any grep** (GNU, BSD, busybox/Alpine): the same message —
  `the pattern is not a valid extended regular expression (grep -E): <grep's own
  line>`. The quoted line is grep's wording (differs by platform), the framing is
  jichi's (identical). This is the fix.
- **Warning-only pattern like `(?:alpha|beta)`, GNU:** `(no matches)` plus
  `grep warned about the pattern: ? at start of expression…`. Unchanged (M732).
- **Same pattern, busybox/Alpine:** a bare `(no matches)`. busybox neither warns
  nor errors on `(?:...)`; the honest statement is "no matches," which is true.
  The model gets less help on *this one* pattern shape on Alpine than on GNU.
  That is the documented cost, not a defect — there is no signal in busybox's
  exit code to surface.

The unit test pins the **portable** part (the jichi-composed framing) and lets
grep's own wording vary, so it holds on all of the above.

## 6. The test instruments, and why each is shaped the way it is

The rule this project lives by: **a test that has never been observed failing has
never been observed working** — and **make the fixture behave like the real peer,
do not special-case the sentinel.** Both shaped the instruments here.

### 6.1 The unit test (`tests/test_tool.c`, the `unbalanced(` case)

Asserts `res.is_error == 1` and that the content contains jichi's own substring
`"not a valid extended regular expression"` — **not** grep's wording. That is the
deliberate choice: grep's message differs by platform (and is not something a
model needs), but jichi's framing is the contract. It was the **red check** on
Alpine (failed at line 758 under a busybox-grep PATH shim) and is green on every
platform post-fix. Its comment now says *why* it is portable.

### 6.2 The standing driver (`tests/smoke/search_grep_dash_i.sh`)

This driver already stood in for the **first** non-GNU grep (illumos, which
rejects `-I`) with a PATH shim. M956 extends it — same driver, same family ("grep
is not one tool") — with three checks for the **second** (busybox, which is blind
to a bad pattern on `/dev/null`):

- **5 — peer fidelity:** the busybox shim *really* reports `bad regex` (rc 2)
  when it reads a pipe. Without this, check 7 could pass while the shim is a
  shell that happens to echo the right string. The shim **delegates to the real
  `busybox` binary on PATH** rather than emulating its vocabulary — because an
  emulation that special-cases `unbalanced(` would pass by construction.
- **6 — control:** under the busybox-shaped grep, a *valid* pattern still finds
  its match. Proves the shim does not break normal operation (a shim that made
  everything fail would let check 7 pass for the wrong reason).
- **7 — the property:** under the busybox-shaped grep, a *malformed* pattern
  returns the pattern-invalid error, not the "pattern is valid" mis-telling.

The driver is **proven red-first**: run against a jichi built *before* the fix
with check 7 red (`not ok 7 … the /dev/null probe is blind on it`), and green
after (all 7 `ok`). On a host without busybox, checks 5–7 emit `t_skip_one` ×3
(counting against the plan, so the denominator stays honest) and 1–4 still run —
the illumos coverage is preserved wherever busybox is absent.

**Why extend this driver rather than add a new one:** the driver-count is pinned
in lints across EN + ja + README + BUILD.md; a new file costs a sweep. The
subject (search_code under a non-GNU grep) is already this driver's theme, and
keeping all the "grep is not one tool" checks in one place is easier to audit.

## 7. What is deliberately NOT covered (honest limits)

- **The BSD greps (FreeBSD/NetBSD/OpenBSD)** are expected to work (they report a
  bad pattern on readable input, like GNU) but are **not tested by this
  milestone** — no BSD host is on this box. They are a future row, not a claim.
- **M732's warning is GNU-only by nature** (see §5). Not a gap to close; there is
  no signal on busybox.
- **A grep that reports a bad pattern on *no* input form** (not just not on
  `/dev/null`) would still be blind to invalid-detection. None is known; check 5
  would catch it as red if one joined the matrix.

## 8. The one-line summary for the next person

> grep's exit contract is 0/1/≥2; busybox only *uses* ≥2 for a bad pattern once
> it has read real content, so a probe against `/dev/null` is blind there. Give
> the invalid-pattern probe a readable line; keep the warning probe on `/dev/null`
> (a readable line would let a matching-but-warned pattern exit 0 and hide the
> warning). Two callers, two targets, one parameter.
