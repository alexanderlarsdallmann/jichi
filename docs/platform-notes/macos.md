# macOS / Darwin — the porting record

*Moved out of [`PLATFORMS.md`](../PLATFORMS.md) and [`BUILD.md`](../BUILD.md) at M833,
verbatim, so the matrix rows stay under the 200-character table-row rule that
`docs_table_rows_lint` holds (the rows had been let past it by raising that page's
floor, which only falls). `git log -S` on any phrase names the commit that wrote it.*

## The verdict (PLATFORMS.md, the Partly verified table)

**Partly verified, and DRIVEN — first verified build 2026-09-26 (folded in M817).** `make ci` has never run here, which is what *Partly* names; and the changes are not built on any other platform yet.

`uname -s = Darwin` (25.5.0 arm64), macOS 26.5.1, MacBook Air M4, Apple clang 21, libcurl 8.7.1 from the Xcode SDK (no Homebrew). `make WERROR=1` clean after one Darwin-only build fix (`_DARWIN_C_SOURCE`, [the finding below](#the-finding-that-made-this-page-m400)); the `HW_MEMSIZE` branch reads the true 16384 MB. Unit suite **13,899 checks / 3 failures on v0.12.0 — all three test-tolerance issues, not product bugs** (TMPDIR trailing slash; BSD grep accepting `(?:…)`; `/var`→`/private/var` symlink), **fixed in M817**. `smoke: OK (329 drivers, 1,921 checks)` with M817's `with_deadline` watchdog fix (macOS has no `timeout(1)`). **DRIVEN** — HRZ gateway, `jlu/qwen3-coder-next`, tool calling "native" (`MAC-50FFBE`). First verified and driven by **Dima Mellnik**. Intel Mac would be its own row.

## Driven (PLATFORMS.md, the Driven register)

**yes** -- by hand

both turns, **2026-09-26**, by Dima Mellnik on a MacBook Air M4: `jlu/qwen3-coder-next` over the **HRZ gateway**, tool calling observed "native", agentic nonce `MAC-50FFBE` (turn 1 "OK" in 0.97 s; turn 2 the phrase in 1.47 s; ~12k input tokens). No rig script yet; a hand run, per *script the rig only after a hand run*

## The build notes (BUILD.md) — as they stood, stale

**Never compiled** — no verified build (two users report a 2026-09 compile, no evidence sent). Expected to build (BSD/POSIX), with **one** Darwin-specific code path — `jc_mem_total_mb`'s `sysctl(HW_MEMSIZE)`, which was un-compilable under this project's own C89 flags until M400 found it. "No Darwin-specific code" was this page's own claim, and it was wrong.

native

*That BUILD.md cell still said **Never compiled** after M817 verified the build: the row was not updated when the platform was onboarded. Corrected at M833.*
