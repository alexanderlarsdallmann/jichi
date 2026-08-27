# The unit suite assumes `/tmp` exists — findings, implications, and the plan

> **Status: BUILT at M457.** The ordering clause in *what would make this plan wrong*
> fired: the crash class measured 19 sites on a narrow pattern and 43 on a broader one,
> so Stage 3 landed first and alone, then Stages 1--2. Kept as written, with this note,
> because a plan that predicted its own re-ordering is worth more intact than tidied.
>
> *Originally:* designed, not built. Found by M452 (an Android tablet), written the same
> day, deliberately not executed in that session — the reason is in *Why this was not
> done when it was found*, below.
>
> **Scope note.** This is a **test-suite** defect. jichi itself has no `/tmp` dependency
> and ran correctly on the platform that exposed this. Nothing here is a product bug,
> and the one product-adjacent item (`TMPDIR` honouring in the shipped binary) is
> explicitly listed as *out* of scope with a reason.

## The finding, in one paragraph

35 of jichi's 123 unit-test files write their fixtures to **literal `/tmp/…` paths** —
**158 sites** — and nothing in `tests/` consults `TMPDIR`. On a system without `/tmp`,
`fopen` returns `NULL`, the fixture is never written, and everything that depends on it
fails. Android has no `/tmp`. So `make check-target` — the tier `PLATFORMS.md` calls
*"the one that matters on a new platform"* — cannot run there, **even where jichi runs
fine**, which on that device it demonstrably did (`--version`, `describe`, `context`,
`map`, `doctor` all worked).

## The numbers, measured

| Quantity | Value | How obtained |
|---|---:|---|
| Unit-test files total | **123** | `ls tests/test_*.c \| wc -l` |
| Files with a literal `/tmp` path | **35** | `grep -rl '"/tmp' tests/*.c` |
| Literal `/tmp` sites | **158** | `grep -rho '"/tmp[^"]*"' tests/*.c` |
| Test files consulting `TMPDIR` | **0** | `grep -rn 'getenv("TMPDIR")' tests/` |
| `src/` references to `TMPDIR` | **0** | `grep -rn TMPDIR src/ include/` |
| Test file where the run died on Android | `test_config.c`, the **4th** of 123 | M452 |

## Four implications, in the order they matter

### 1. The portable-gate claim is too strong, by this project's own rule

`docs/plans/2026-07-hardware-testing.md`'s finding table says outright:

> `make check-target` needs a tool the target lacks (`timeout`, `time -v`) → **finding** —
> the portable-gate claim is too strong

This is that finding, for a *directory* rather than a tool. It applies to any target
without a writable `/tmp`: Android, a minimal initramfs, a hardened or distroless
container, some embedded images. `PLATFORMS.md` currently invites a newcomer to validate
a platform by running `make check-target`; on such a target that instruction fails for a
reason that has nothing to do with jichi.

### 2. It stayed invisible because every earlier target had `/tmp`

The boards run Debian; the Tier V VMs run Debian or Alpine; the containers inherit a
`/tmp` from their image. Android is the **first target in the project's history without
one**, which is why a 158-site assumption survived 450 milestones unnoticed. That is an
argument *for* the device rows, not against them: the assumption was only ever going to
surface when the platform axis moved far enough, and it did so within an hour of the
first genuinely different userland.

### 3. Fixing it **moves a published measurement**

This is the reason the fix is not a mechanical find-and-replace.
`docs/LOW_MEMORY.md` already documents one instance and its consequence:
`tests/test_bounds.c` writes a **64 MiB + 4 KB** fixture (`JC_READ_FILE_MAX + 4096`) to a
hardcoded `/tmp`, and **where `/tmp` is tmpfs those pages are charged to the cgroup**.
That single fact is the whole explanation for the project's two most divergent floors:

| Bench | `/tmp` | `units` floor |
|---|---|---:|
| threadwork (M430) | tmpfs | **72 MB** |
| this bench (M446 session) | ext4 | **14 MB** |

Relocating fixtures changes *where those pages land*, so it can move the `units` floor,
the `smoke` floor, or both — on every bench. **Any fix must re-measure both floors and
re-stamp the affected `LOW_MEMORY.md` rows**, or it silently invalidates published
numbers. That is a milestone's worth of work on its own, not a cleanup.

### 4. The sharper defect is the crash, not the paths

`JC_CHECK` **records and continues** — deliberately, so one red does not hide the next.
The cost is that a missing fixture does not merely produce reds. In `test_config.c` a
failed `m0 != NULL` was followed on the next line by `m0->roles`: a null dereference,
`Aborted`, at the **4th of 123 test files**. The other ~119 reported nothing at all.

So an environment failure upstream converts an *informative* red into **total information
loss**. This is the M265 lesson recurring — that milestone fixed a test that segfaulted on
git < 2.5 for exactly this shape. M452 fixed the site it found, re-ran, and **the suite
aborted again at a different site of the same shape**, which is the evidence that the
class is present in more places than the two observed.

## Why this was not done when it was found

Stated plainly, because a reader is entitled to ask:

- It is a mechanical edit across **a third of the test suite**, and mechanical edits at
  scale are exactly where a tired session introduces a silent behaviour change.
- It **moves published measurements** (implication 3), so it is not complete without a
  re-measurement pass on at least two benches.
- The session that found it had already landed **seven milestones** (M446–M452).
- Fixing crash sites individually was **tried and rejected in-session** as treating the
  symptom — the suite aborted again at the next one.

## The plan

### Stage 1 — one helper, in the harness

Add to `tests/jc_test.h` a single accessor, and route every fixture path through it:

```c
/* The directory for test fixtures. Honours TMPDIR and falls back to /tmp.
 * The SMOKE tier has always done this (tests/smoke/_smoke.sh: smoke_tmp uses
 * "${TMPDIR:-/tmp}"); the unit suite never did, which is why the unit suite
 * cannot run on a system without /tmp and the smoke tier can. */
const char *jc_test_tmpdir(void);
```

The idiom is **not invented here** — it is copied from `smoke_tmp()`, which already gets
this right. That matters: the project's own tier already demonstrates the correct
behaviour, so this is bringing one tier up to another's standard rather than adding a new
convention.

**Design constraint:** the helper must return a *directory*, and callers compose paths
with `jc_snprintf` — never `sprintf` (`CLAUDE.md`). A helper that returns a full path per
call would need an allocator and would change fixture lifetimes; it should not.

### Stage 2 — convert the 158 sites

Mechanical, one file at a time, with the suite re-run after each file so a regression is
attributable. The count must not change: **11,595 checks / 0 failures** before and after,
on this bench.

Two sites need judgement rather than substitution:

- `tests/test_bounds.c` — the 64 MiB fixture. This is the one whose *location* is
  load-bearing for the RAM floors (implication 3). It should move with the rest, and
  Stage 4 exists because of it.
- Any site that deliberately tests behaviour *at* `/tmp` (an absolute-path fence case,
  for instance) must keep its literal path, with a comment saying why. **Grep alone
  cannot tell these apart** — each file needs a reading, which is the main cost of this
  stage.

### Stage 3 — the crash class, not just the crash

Audit for the shape `JC_CHECK(p != NULL)` followed by a dereference of `p`. The rule,
which belongs in `docs/TEST_INTEGRITY.md`:

> `JC_CHECK` records and continues. A null check must therefore be followed by a
> **branch**, never by a dereference — otherwise one environment failure turns a red into
> a `SIGSEGV` and takes every later test file with it.

Two instances are known (`test_config.c`, fixed at M452; one further site observed but
not identified). A grep for `JC_CHECK(<ident> != NULL)` followed within a few lines by
`<ident>->` will find most of them; the rest need the audit.

**Verification with teeth:** the fix is provable by construction — run the suite with
`TMPDIR` pointed at a **non-existent directory**. Before Stage 3 it aborts early; after,
it must produce honest reds and **reach the last test file**. That is a better test than
any assertion, because it reproduces the exact failure Android produced.

### Stage 4 — re-measure, and re-stamp

Because Stage 2 moves where fixtures land:

1. Re-run `tests/measure/ram_floor.sh --workload units` and `--workload smoke` on this
   bench (ext4 `/tmp`), and on any bench with a tmpfs `/tmp` that is available.
2. Add the new rows to `docs/LOW_MEMORY.md` **stamped, never overwriting** — the M259
   rule. The old figures remain true of the old layout.
3. If the floors move, say so in the milestone and explain which half of the change is
   jichi and which is the fixture's new home. If they do not move, say *that*, with the
   numbers.

### Stage 5 — close the loop on the platform claim

- Re-run the Android row and record whether `make check-target` now completes there.
- Update `docs/PLATFORMS.md`: today the Android row says the suite cannot run and why.
  If Stage 2–3 land, that row becomes a real result or a differently-blocked one.
- If the portable-gate claim needs qualifying even after this (a target with no `mktemp`,
  no writable filesystem at all), qualify it in `PLATFORMS.md` rather than leaving the
  invitation to run `make check-target` unconditional.

## Explicitly out of scope

- **Making the shipped binary honour `TMPDIR`.** `src/` never consults it either, but
  that is a *product* behaviour change with its own users and its own compatibility
  question, and no one has reported wanting it. Nothing in M452 requires it: jichi ran on
  Android without touching `/tmp` at all. If it is ever wanted it should be its own
  proposal, decided on its own evidence.
- **Making the suite run on Android as a goal in itself.** The goal is that the suite
  stops asserting a property of its host. Android is the instrument that revealed the
  assumption, not the target being served.

## What would make this plan wrong

Recorded up front, per this project's habit:

- If the 158 sites turn out to include many deliberate `/tmp` tests, Stage 2 is not
  mechanical and the estimate is wrong. **Check by reading before converting.**
- If the floors move a lot, Stage 4 grows into its own milestone and Stage 2 should
  probably land separately from it so the two effects can be told apart.
- If the crash class turns out to be widespread (say, >10 sites), Stage 3 is the real
  work and the `/tmp` conversion is the smaller half — in which case the ordering here is
  backwards, and Stage 3 should land first, alone, since it is valuable on **every**
  platform rather than only on ones without `/tmp`.
