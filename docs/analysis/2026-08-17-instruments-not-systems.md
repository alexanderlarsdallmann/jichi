# One session, fourteen findings — and eleven of them were instruments, not systems

*2026-08-17, threadwork (Threadripper PRO 7955WX, 32 cores / 246 GB, Ubuntu, kernel
7.0.0-29). Milestones M463–M466 — the first eleven findings were written up as
M463–M465 and three more landed the same day, in the addendum at the end; the title
counts all fourteen. This is the consolidated record: every finding, its
evidence, its solution, and what is left. The two deep dives are
[`the-lost-first-send`](2026-08-17-the-lost-first-send.md) and
[`the-assignment-that-did-not-travel`](2026-08-17-the-assignment-that-did-not-travel.md);
this page is the index, the pattern, and the honest remainder.*

## The pattern worth naming

Of fourteen findings, **three were about jichi and eleven were about the things that
measure jichi** (eight below, three in the addendum). Every one of the eleven was
invisible from a green gate:

- a currency lint whose ground truth was the file that had stopped moving
- a deadline knob that reached three of four layers
- a capture that summarised away the one line that mattered
- a shell portability rule that made five tests pass without testing
- a rig with a bench's constant compiled in
- a `printf` that truncated a measurement into agreement with the previous answer
- a guard that matched anything ending in a digit
- a grep anchored where the output is indented
- a word-boundary escape that matches nothing outside GNU — in a lint written that day
- a tier that stops at the first failure, so a remote platform reports one defect
- a rig that ships `HEAD`, so an uncommitted fix cannot be tested where it matters

`docs/TEST_INTEGRITY.md` already says *prefer a lint to an audit*. Today's addition
is narrower: **when a finding survives more than one sitting, suspect the
instrument's output before the system's behaviour.** Two findings here had been open
since M460 not because they were hard but because the harness was deleting the
evidence — in one case a `tail -5` that discarded the failing check for two
sessions.

## The findings, and what was done

### Product (3)

| # | Finding | Evidence | Solution |
|---|---|---|---|
| 1 | **FreeBSD passes the full gate** — first non-Linux kernel to do so | 11,643 unit checks + `smoke: OK (201 drivers, 1068 checks)`, 0 failures, on the platform | promoted in `PLATFORMS.md`; the 1068-vs-1081 delta is twelve environment-gated skips, stated |
| 2 | **Startup type-ahead is discarded silently** | at a 1 ms pre-send delay the mock receives **0** requests; at 100 ms, 1. The transcript shows the tty's echo, then the banner, then the prompt drawn over it | *not* fixed. The discard is M257's deliberate rule (*input you cannot see is input you cannot correct*); the **silence** is the defect — mid-turn type-ahead has four notices, this path has none. `DEFERRED.md` row |
| 3 | **A failure message asserts a diagnosis the check cannot establish** | `setup_keyfile` check 6 says "the key did not reach jichi" for a check that cannot distinguish that from "jichi never ran" | noted in `PLATFORMS.md`; the message is now reachable only in the true case, but the general shape is worth watching |

### Harness and record (8)

| # | Finding | Evidence | Solution |
|---|---|---|---|
| 4 | **Every currency check trusted the one file nobody checked** | 43 commits and four milestones (M459–M462) shipped while all three checks reported zero drift, because each resolves to `grep '^### M…' docs/ROADMAP.md \| tail -1` | `milestone_currency_lint.sh` — ground truth is the highest milestone the *other* reference pages cite. Born red; git rejected as a basis because the shipped tree is not a repository (M451) |
| 5 | **`with_deadline` — the fourth deadline layer — never scaled** | 210 call sites across 124 drivers, all fixed bounds, against `tt_mult.c`'s "or the knob is a lie" | scales now, clamped so a malformed knob cannot *shorten* a deadline; `smoke_lint` 10b, tested functionally and two-sided |
| 6 | **An assignment prefixed to a shell function is not exported on FreeBSD** | `FOO=bar f` → child env: dash **yes**, bash **yes**, FreeBSD `/bin/sh` **no** | six sites rewritten as `with_deadline N env NAME=value cmd`; `smoke_lint` 12 forbids the shape outright, so the rule is total and needs no exception list |
| 7 | **Five tests passed while testing something else** | two lost `HOME` isolation, one lost a fault-injection variable, two lost `TERM`/`LC_ALL` — all green on FreeBSD | same fix as 6. The one test that *failed* is the only reason the five were found |
| 8 | **A rig baked another bench's constant in** | `tier-v-openbsd.sh` computed `(secs + 6) / 6` under a comment claiming "both halves are recorded, so the row survives a change of bench" | `scripts/_rig_mult.sh`, one implementation, `--ref-secs` required, refuses rather than guesses |
| 9 | **A rig computed no multiplier at all** | `tier-v-bsd.sh` ran `gmake smoke` bare, i.e. at the tightest possible deadlines | same |
| 10 | **A rig discarded its own diagnosis** | smoke failures captured as `tail -5`: for a 28-check driver, four *passing* checks and `Error 1` | logs in the guest, pulls back every failing check plus the tier's standalone re-classification |
| 11 | **The register advertised work that did not exist** | three `## Open` sections with zero rows, a row reading "CLOSED at M459" under `## Open`, 16 lines of orphaned prose, a licence row 7 days stale | cleaned; open rows 67 → 66, exactly the one that declared itself closed |

## Four errors of my own, and what each cost

Recorded because the failure modes are more transferable than the fixes.

| What I did | Why it was wrong | How it was caught |
|---|---|---|
| Reported `busybox-static` absent | `apt-cache policy … \| grep Candidate` filtered out the `Installed:` line | reading the unfiltered output |
| Published a bench reference of "4.00 s" | `printf '%.2f'` under a German `LC_NUMERIC` **truncated 4.574 at the decimal point** — three runs, three identical values, coincidentally equal to the figure this bench published at M430 | re-measuring under `LC_ALL=C`: 4.38 s |
| Wrote a guard as `*[0-9])` | matches any string *ending* in a digit, so it accepted the decimal it existed to reject | running the guard against the input it was written for |
| "Confirmed" a hypothesis with `ptydrive_rc=2` | that is *usage / script parse error*: `delay 0` is refused, so the script never ran and no send was made | reading the exit-code table instead of assuming |

The second is the one to remember: **an instrument that fails by agreeing with the
previous answer is the worst kind.** Nothing in the output said "error"; it said
`4,00s`, three times, matching history.

## What is NOT closed

Stated plainly, because a session that only lists wins is a advertisement.

- **OpenBSD stays *Partly verified*.** Its `accessible` stop is explained (a fixed
  pre-send delay losing a race with startup) but **the row has not been re-run** with
  the six `env` fixes. `scripts/tier-v-openbsd.sh --ref-secs <n> --reuse` is the cheap
  confirmation and was not done.

  **Update, same day (M466):** it was done — three times — and the answer was not the
  one this list expected. See the addendum below; the row is re-measured, the stop is
  still open, and the reason it had resisted was that the driver *had never run*.
- **The Guix `parallel_abort` deadlock is untouched**, and needs `scripts/tier-v-guix.sh`
  — the one platform whose row cannot rebuild itself — before it can be reproduced
  cheaply. **Update (M466):** the machine definition no longer hardcodes another
  host's ssh key path, so it can at least be built somewhere; and the deadlock is now
  known to have been measured **34 commits before** the SIGPIPE-inheritance fix
  (`448616d`), whose signature — a child spinning on `EPIPE` instead of dying when its
  consumer exits — is a live candidate for it. Re-measure before debugging.
- **The lost-first-send mechanism is reproduced on Linux, not verified on OpenBSD.**
  The transcript shapes match; that is an explanation, not a verification.
- **"jichi needs >5 s to reach its first prompt on that guest"** is the inference the
  byte-identical result forces, not a measurement.
- **The startup type-ahead silence** is deferred, not fixed, and the reason is a real
  design question rather than effort: detecting that there *was* pending input needs a
  non-blocking tty probe the code does not do, and M257's principle argues for
  acknowledging the input rather than accepting it.
- **The multi-arch sweep, the WSL handoff and NetBSD** — planned this session, not
  started.
- **`with_deadline`'s scaling is exercised at mult 1 and 2 only.** No row has yet run
  at the multipliers a real slow board uses (11, 19, 28).

---

## Addendum, same day: M466, and the pattern held

Three more findings landed after this page was written. **All three were instruments.**
That makes the day's tally **fourteen findings, three about the product and eleven
about the things that measure it** — and the sharpest of the eleven was in a lint I had
shipped three commits earlier on this same page's recommendation.

| # | Finding | Evidence | Solution |
|---|---|---|---|
| 12 | **`\b` is a GNU regex extension**, and the M463 currency lint used it | on OpenBSD `grep -ohE '\bM[0-9]{3}\b'` matches nothing, so the ground truth became `''` | both sites tokenise with `tr -c` now; `posix_utils_lint` check 11 bans the family. **The floor is the whole story**: the site that had one failed loudly, the site that did not (`changelog_coverage_lint`) *degraded* and kept reporting a number — inside the green gate that promoted FreeBSD |
| 13 | **A fail-fast tier cannot enumerate a platform's defects** | OpenBSD's real stop had never been reached in **three** runs; a lint failing earlier in the list ended the tier, and the row could only say "did not print its OK marker" | `JC_SMOKE_KEEP_GOING=1` (both BSD rigs pass it) + a named-driver subset, so re-checking one failure is not a 201-driver sweep |
| 14 | **A rig that ships `git archive HEAD` cannot verify an uncommitted fix** | the only way to test the `\b` fix on the target was to commit it untested | `--dirty` ships the working tree and marks the row *NOT a commit*. The `\b` fix was verified on OpenBSD through it, before being committed |

Two more of my own errors, in the same shape as the four above:

| What I did | Why it was wrong | How it was caught |
|---|---|---|
| Grepped the guest's smoke log with `^1\.\.0` | `run.sh` indents nested TAP as `    \| 1..0`, so it matched nothing — **the mistake the comment six lines above warns about**, made while writing that comment | the capture came back empty; fixed by copying the log to the host, where a wrong pattern costs a retry instead of a VM boot |
| Read `accessible`'s new failure as a regression from my own edits | it was **masking**: the tier had stopped earlier before, so the driver had never run | checking `run.sh`'s dispatch (`\|\| exit 1`) instead of trusting the before/after |

**What the addendum does not change.** The remaining open items above stand: the
multi-arch sweep, the WSL handoff and NetBSD are still not started; `with_deadline`'s
scaling is still exercised only at low multipliers; and the startup type-ahead silence
is still deferred on design grounds rather than effort. One item improved without
closing: the Guix deadlock now has a named suspect (it predates `448616d` by 34
commits) and a machine definition that is no longer pinned to one host's `$HOME`.
