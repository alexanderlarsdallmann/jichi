# OpenBSD — the porting history

*Moved out of [`PLATFORMS.md`](../PLATFORMS.md) at M780 so the matrix stays short and the [install page](../install/) stays a lean walkthrough. This is the developer/porting record — the defects each row found and how they were fixed — kept verbatim; `git log -S` on any phrase names the commit that wrote it.*

## The OpenBSD row before it was Verified: the M472 hardening break (M479)

*Written at M479, when OpenBSD sat in the table above as **Partly verified --
207 of 209 smoke drivers**, and severed from that row the same way. OpenBSD has
since been Verified and Driven, and its current row says so; this is kept
because the break it describes shipped over six milestones and the reason is
worth having. The sentence began in the row's own cell, so it starts here with
the subject restored in brackets.*

**[The 207-of-209 figure was measured]** against the working tree after seven milestones had landed on top of the M471 figure
(199 of 201, 1068 checks), which is how the M472 build regression below was found.
**The row did not build at first**: M472's hardening probe asked whether the compiler
*accepts* a flag, and OpenBSD's clang 19 accepts `-fstack-clash-protection` and then
ignores it, so every TU failed under the build's own `-Werror`
(`argument unused during compilation ... [-Werror,-Wunused-command-line-argument]`).
Six milestones shipped over that break because `make ci`'s clang stage runs on Linux,
where the flag is genuinely supported. Two failing drivers remain and they are the
same two as before (`sessions_footprint`, `turn_scratch`); `ask_unattended` failed in
one of the four runs and **passed standalone**, which the tier's own retry classifies
as cross-driver load rather than a platform defect. The first run to completion reported 178/947 with 23 failures, all failing standalone too (the tier's own M201 re-classification, so this is not cross-driver load). Every earlier row reported **one** failure, because the tier was fail-fast: the platform's failure set had never been enumerated, only its first element, and `accessible` — the stop this row was named for — had never actually run. **Two harness defects accounted for 20 of the 23** — see the three families below. Needs `gmake`


## What OpenBSD found that FreeBSD did not (M461)

The row was run to answer one question — *do the seven FreeBSD fixes generalise,
or were they fitted to one libc?* The build answered it in 14 seconds: **clean
first try**, so the `#ifdef` guards and capability probes were genuinely portable
rather than FreeBSD-shaped. Then it found five more things, and **two of them are
bugs on every platform jichi has ever run on**:

| Finding | Reach |
|---|---|
| `search_code` shelled out `grep --color=never`; BSD grep rejects it, exits 2, and the tool's `2>/dev/null` hid the message — so **every search returned "(no matches)"** | OpenBSD only, but the tool *lied* rather than failed, which is worse than a crash |
| Children inherited jichi's **ignored SIGPIPE**, so a pipeline producer spins on `EPIPE` instead of dying. Measured on Linux/dash: **6.7 s and 59 MB of spew** vs 0.002 s | **every platform.** GNU coreutils check their write result, which is the only reason Linux never showed it |
| A timed-out capture killed only its direct child, **orphaning the rest of the pipeline**. Found as 8 `yes` processes at load average 9, the oldest spinning 3h50m after its run ended | **every platform** |
| GNU grep's `include`/`exclude` file-filter flags in four coverage lints. BSD grep reads them as *filenames* and keeps searching without the filter — inverting one lint's every answer (146 of 153 flags "documented nowhere") | the lints, on any BSD |
| GNU BRE alternation (`\|`) in four drivers. POSIX BRE has no alternation, so BSD tools read a literal pipe and the pattern silently never matches | the lints, on any BSD |

The two "every platform" rows are the argument for this row existing. Neither
was a BSD bug; both were **jichi** bugs that only a different userland could
make visible — the first because BSD `yes(1)` does not check its write result,
the second because nothing on Linux made the survivors observable.

**EXPLAINED (M464, 2026-08-17), and it is the harness rather than jichi.** Full
measurement:
[`analysis/2026-08-17-the-lost-first-send.md`](../analysis/2026-08-17-the-lost-first-send.md).

`enter_raw` is called from two places and they are not the same call.
`jc_term_read_key` — the `[y]/[n]/[a]/[e]/[v]` approval — flushes as a **safety
property** (M254: stray type-ahead must not answer a y/n). `jc_term_readline` flushes
**once per prompt**, which discards whatever was typed before that prompt existed.
Reproduced on Linux by shrinking the driver's pre-send delay: at **1 ms the mock
receives 0 requests**; at 100 ms and above, 1. The window on this bench is under
100 ms, which is why no Linux run had ever shown it, and the 1 ms transcript has the
same shape as OpenBSD's — the tty's own echo of the line, then the banner, then the
prompt drawn after it.

That also dissolves the "so it is not a race" argument that made this look deep: it
**is** a race, and neither delay won it. If jichi needs longer than 5 s to reach its
first prompt on that guest, 1,200 ms and 5,000 ms fall on the same side of the window
and produce byte-identical output. The discard itself is deliberate, and the reasoning
is already recorded in `tests/smoke/typeahead.sh` as M257's: *input you cannot see is
input you cannot correct*, which is why mid-turn type-ahead is opt-in. A fixed `delay`
is therefore a bet on startup time — and the failing arm runs with colour **on**, which
is precisely why an `expect` on the prompt is hard to write for it (the earlier attempt
produced a different failure: 0 bytes, jichi not reaching its banner).

**The one part that still looks like a defect is the silence.** Mid-turn type-ahead has
four notices; the startup discard has none, so a user watches their line appear and
vanish with no explanation. Recorded in [`DEFERRED.md`](../DEFERRED.md) rather than guessed
at.

**Re-run on the platform (M466, 2026-08-17), and what it does and does not settle.**
The row was re-run three times. Two facts came out of it. First, the reason this stop
had resisted for three sittings was not depth: the tier is **fail-fast**, so
`accessible` had *never been reached* — a lint failing earlier in the list ended the
run, and the row could only report "did not print its OK marker". Second, with
`JC_SMOKE_KEEP_GOING=1` there is finally check-level evidence:

| check | result |
|---|---|
| 1 — both sessions exit 0 | **pass** |
| 2 — working-indicator counts | **fail**: `default=0 accessible=0` (wanted `accessible==2 < default`) |
| 3 — role labels in the transcript | **fail**: missing |
| 4–8 — erase-below counts, mode-gated fast path, `NO_COLOR`+C locale, the UTF-8 pair, clean headless stdout | **pass** |

`default=0` is the load-bearing number: **neither** session showed a working indicator,
so neither ran a model turn, so neither prompt was submitted — which is what the
lost-first-send predicts and what a rendering bug would not. Checks 4–8 passing say the
terminal work itself is sound on ksh.

That is **consistent with** the Linux measurement and is not the same thing as verifying
it: nothing here observes the `TCSAFLUSH` on that platform, only its consequence. The
stop stays open, now with a measurement rather than a marker. FreeBSD's `setup_keyfile`
check 6 — the other half of this sentence for two weeks — was closed at M465.

## OpenBSD's failure set: 23 drivers, three families, 20 of them one harness away (M466 measured, M467 fixed)

The fourth run used `JC_SMOKE_KEEP_GOING=1`, and the honest number is **178 of 201
drivers / 947 checks**. This is not a regression — it is the first row that was allowed
to finish. Every previous OpenBSD row stopped at its first failure, so *"smoke reaches
`accessible`"* described how far the tier got before exiting, not how much passed.
**All 23 also fail standalone**, so none of it is cross-driver load or PTY exhaustion.

| Family | Drivers | Symptom |
|---|---|---|
| **A — FIXED (M467). The PTY/TUI path produces nothing** | 21: `tui_basic` `tui_context_views` `tui_learn` `tui_learn_apply` `tui_model_name` `tab` `editor` `ghost` `advice` `paste` `paste_special` `typed` `typeahead` `typeahead_live` `accessible` `prefix_churn` `undo_note` `setup_keyfile` `sessions_footprint` `turn_scratch` `learner_flow` | **62 expect timeouts, every one with `transcript tail (0 of 0 bytes)`** — the child emitted zero bytes and exited. `accessible`, `paste_special` and `prefix_churn` are partial exceptions: they *do* produce output and fail on content instead |
| **B — FIXED (M467), and it was never jichi's** | `parallel_abort` | *"parent did not exit within 15s of SIGINT — abort/reaping deadlocked"* — **word-for-word the Guix System failure** filed at M458 as isolated to that platform |
| **C — FIXED (M467)** | `stop_reason_capped` | `ok 1`–`ok 5`, all five, then the driver is reported failed. The signature `ac166d5` fixed for FreeBSD (an unreaped mock making `timeout` return non-zero long before any deadline), so that fix is either incomplete or a second instance exists |

**The two that remain (M471), recorded as undiagnosed rather than explained.**
`sessions_footprint` and `turn_scratch` still report *"could not read the /context
arena gauge (before='' after='')"*. The `\xNN` fix that closed `setup_keyfile` did
**not** close these — predicted to be one cause for all three, measured as one cause
for one. Their pipelines look POSIX-clean (`grep -o 'Arenas: session [0-9]* KB'`
against text `jc_context.c` really does emit), so the likeliest explanation left is
that the line is absent from, or hard-wrapped inside, the PTY transcript — **a
hypothesis, labelled one.** The next step is to dump `$tmp/fp.log` from the guest and
look, rather than reason further.


**Family B is the valuable one.** The Guix `parallel_abort` deadlock was filed as
needing a Guix rig before it could be reproduced — and Guix is the one platform whose
row cannot rebuild itself. It reproduces on **OpenBSD**, which has an unattended rig
(`scripts/tier-v-openbsd.sh`) that installs from an 11 MB ISO and re-runs in one
command. The blocker is gone, and the failure can now be iterated against with
`sh tests/smoke/run.sh parallel_abort` on a platform this project can rebuild at will.

**Family A is undiagnosed and deliberately not guessed at.** What is known: jichi runs
on the platform (11,643 unit checks green; `--version`, `doctor`, `describe` and
`context` all answer), so this is specific to the PTY path — and *some* PTY sessions do
produce output, which rules out a blanket "no PTY works". Two cheap discriminators for
the next sitting, in order: run `ptydrive` against a trivial non-jichi child (`cat`,
`sh -i`) to separate the harness from the agent, and if the harness is sound, check
whether jichi exits before its first write rather than failing to write.

## Measurement log (moved from the PLATFORMS.md cell, M781)

OpenBSD **7.9** amd64 under KVM, installed unattended via `autoinstall(8)` over the serial console.
clang 19.1.7, libcurl 8.16 from `pkg_add`. Its unique axis is the **shell**: OpenBSD's `/bin/sh` is
**ksh**, so ~200 POSIX-sh smoke drivers run under an implementation nothing else in the matrix
exercises.

- **M461 (2026-08-17)** — first row. `gmake WERROR=1` clean in **8 s**, c89 strict, **first try** —
  the three `#ifdef` capability guards FreeBSD forced generalised to a second independent non-Linux
  libc with no further change. **12,415 unit checks / 0 failures** (Linux 12,422; the seven-check gap
  is environment-gated).
- **M482a (2026-08-19)** — promoted to Verified: `smoke: OK — 209 of 209 drivers, 1,110 checks, 0
  failures`, once the last two red checks were diagnosed (`sessions_footprint`, `turn_scratch` on a
  nullable `grep -o` pattern — solved by difference against NetBSD, which ships GNU grep). The payoff
  was the assertions themselves: session arena grew **0 KB** (limit 128), turn scratch **5 KB**
  (limit 64) — M197/M218's memory-lifetime guarantees verified on a third kernel. Account:
  [`analysis/2026-08-18-openbsd-nullable-grep.md`](../analysis/2026-08-18-openbsd-nullable-grep.md).
- **M481 — DRIVEN 2026-09-19:** both live turns passed, twice (`TIER-V-2C6E97`, then `TIER-V-D34EF1`
  on the re-run). Smoke 301 of 303 drivers, 1,723 checks (290 → 294 → 301 as its own findings were
  fixed). Six drivers decline (the `FAULT=1` tier; `child_fds` — no procfs; `pdf`/`docs_pdf` — the
  7.9 package set could not install `poppler-utils`, and the rig reports that rather than assuming).
- **M685 (2026-09-20) — RED, THEN GREEN, AND THE RED IS THE POINT.** The first run failed with
  `smoke_lint.sh: syntax error: ';;' unexpected` after nineteen of its own checks passed — a `case`
  pattern inside `$( )` left a paren unbalanced, and ksh closed the substitution there; introduced a
  day earlier by an OpenBSD fix, undetected because nothing had run here since. **This row is why the
  project keeps a kernel it does not ship on.** Fixed (`525a8f90`): `13,383 checks, 0 failures`,
  `smoke: OK (312 drivers, 1,783 checks)`, 8 drivers declining. Offline only; the Driven verdict
  rests on the 2026-09-19 runs. (The same session lost 25 minutes to an unbounded `pkg_add` — see
  ANECDOTES #86.)
