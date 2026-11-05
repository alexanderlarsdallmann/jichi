# illumos / Solaris — the porting history

*Moved out of [`PLATFORMS.md`](../PLATFORMS.md) at M780 so the matrix stays short and the [install page](../install/) stays a lean walkthrough. This is the developer/porting record — the defects each row found and how they were fixed — kept verbatim; `git log -S` on any phrase names the commit that wrote it.*

## The illumos row: one line of grep, and the ten drivers it hid (M661b–M683)

*This was the tail of the illumos row above until 2026-09-22. M661b appended to
that cell and let the text run past the end of the line, so the cell was cut off
mid-word and everything below rendered as a paragraph; M678, M681 and M683 then
appended more. Nothing here is changed but its placement -- `git log -S` on the
first phrase names the commit that wrote it.*

behaviour.** The mechanism recorded here at M678 was **wrong as stated**, and
the correction is kept rather than overwritten because it changed the size of
the work by a factor of thirty. It said *"`grep -o` prints only the first match
per line"*. Measured on a live guest at M681: `printf "item1 item22 item333\n" |
grep -o "item[0-9]*"` returns **3** on illumos, the same as GNU. The trigger is a
line with **no trailing newline** — the same input without the `\n` returns
**1**, on a pipe and on a file alike. So the hazard is not "many matches on one
line", it is "an unterminated last line", and the predicted fix (rewrite every
lint that flattens a file) was not the fix: eleven one-character edits were.
It is **not** the nullable-pattern hazard `posix_utils_lint` check 17 covers;
that one is about a pattern matching empty. **All ten remaining drivers were
diagnosed and closed at M683** — five fixes, because one of them (`mockmodel`
writing a capture that ends mid-line) accounted for three. Every one was a
defect in this project's own test tooling rather than in jichi, and five of the
seven needed a live guest because they are kernel, libc and utility behaviour
rather than parsing: a pty master that discards a write when no slave is open, an
`env` with no `-u`, an `awk` that reads a multi-character `RS` as its first
character, a `sed` that terminates the last line as POSIX requires, and a `tar`
that materialises `git archive`'s pax global header as a file. The mechanisms are
tabulated in [`DEFERRED.md`](../DEFERRED.md) §1b.
**RE-MEASURED 2026-09-22, and the whole row is green for the first time:
`19 ok, 0 failed`** — `smoke: OK (317 drivers, 1,839 checks)` against 317 / 1,852
on the bench (the 13-check gap is platform skips), unit suite **13,458 / 0**,
`WERROR=1` clean in 10 s, and **both driven turns** (`google/gemma-4-12b` over the
reverse tunnel, agentic phrase `TIER-V-2B095E`). **Coverage debt 0.** Getting
there took four boots and cost two defects, one of them in `src/`: `uname()`
returns a POSITIVE value here and four call sites tested it for zero, so
`jc_platform_describe` failed on a call that had worked — which made M695's
platform-verdict table dead on this row and `doctor` say *"host platform not
recognised"* where it now says `SunOS 5.11 (i86pc)`. The other was
`pax_global_header` arriving through `_rig_ship.sh`'s clean ship path, the site
M683's fix at `make-snapshot.sh` could not reach. Both are written up in
[`analysis/2026-09-22-illumos-rerun-uname-and-pax.md`](../analysis/2026-09-22-illumos-rerun-uname-and-pax.md),
including one discrepancy with the M683 figure below that is **not** resolved.
The multiplier denominator was re-measured on the bench (median 7.44 s, not the
6.19 s a row recorded earlier) because it is a ratio.

**WHOLE TIER GREEN AT M683, 2026-09-20:** `smoke: OK (311 drivers, 1,801 checks)`
and `13,387 checks, 0 failures` in the unit suite, against 312 / 1,819 and 13,390
on the bench the same day — the gap is platform skips, not failures. Every one of
the ten was a defect in this project's own test tooling rather than in jichi, and
`preprompt_discard` now **runs** here rather than skipping, because the fixture
that could not place type-ahead was measuring the write's return value instead of
whether the bytes arrived. **Still Partly verified**: `make ci` has never run on
this kernel, and the verdict names the gate, not the tier.

## Measurement log (moved from the PLATFORMS.md cell, M781)

OmniOS CE r151058 cloud image under KVM (4 vCPU / 4 GB), gcc 14.3.0, GNU Make 4.4.1,
`/bin/sh` = ksh93. Reproduce: `scripts/tier-v-illumos.sh --ref-secs <this bench's seconds>`
(written after the row ran by hand). Build with **`gmake WERROR=1 CC=gcc`** — both are
load-bearing: illumos's own `make` is Sun make, and with no `cc`/`c99` every capability probe
answers "no" and you build a silently degraded binary. It needs `-D__EXTENSIONS__` and
`-lsocket -lnsl`, both probed (`make info` shows them); **no source conditional was added** — the
M460/M461 pattern held for a fifth platform.

- **M658 (2026-09-18)** — first compile on a non-Linux, non-BSD kernel: builds clean, unit suite
  green, smoke 249 of 301 (238 hand-driven before the rig learned to `git init` the shipped tree).
- **M661** — pty cluster closed (all 19): a pty slave on a STREAMS system is not a terminal until
  `ptem`/`ldterm` are pushed (`isatty` 0 before, 1 after), so `ptydrive` handed jichi a non-tty and
  it correctly took the non-interactive path. Smoke 279 of 301, 1,588 checks.
- **M703 (2026-09-22) — fully driven and green:** `19 ok, 0 failed`; `smoke: OK (317 drivers,
  1,839 checks)`, unit 13,458 / 0, coverage debt 0; `doctor` 23 ok, 8 warnings, 0 problems (the
  `! host platform not recognised` is the generic non-Linux note M503 chose over a per-platform
  line). Driven through the rig: `TIER-V-52FE94`. `make ci` has never run here — that is what
  *Partly* names, the gate, not the tier.
- **The procfs prediction, measured:** `/proc/self/stat` and `/proc/self/exe` are absent
  (`/proc/self/path/a.out` is the equivalent), and `/proc/self/status` exists as a 1,136-byte binary
  `pstatus_t` with zero `VmRSS:` — so `jc_meminfo_parse` reports *no data*, not *wrong data*, as
  `test_binary_status` pins.
