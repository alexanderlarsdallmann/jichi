# Haiku — the porting record

*Moved out of [`PLATFORMS.md`](../PLATFORMS.md) and [`BUILD.md`](../BUILD.md) at M833,
verbatim, so the matrix rows stay under the 200-character table-row rule that
`docs_table_rows_lint` holds (the rows had been let past it by raising that page's
floor, which only falls). `git log -S` on any phrase names the commit that wrote it.*

## The verdict (PLATFORMS.md, the Partly verified table)

**Partly verified, and DRIVEN — first verified build 2026-09-28 (M830).** `make ci` has never run here (what *Partly* names); the changes are one Makefile line, built on Linux too.

`uname -s = Haiku`, gcc 13.3.0, libcurl 8.22.0 from the SDK (`/boot/system/develop/headers/curl`). `make WERROR=1` clean — **0 warnings** — after **one build fix**: Haiku's BSD socket API (`socket`/`bind`/`accept`/`inet_pton`) lives in **`libnetwork`**, not libc or libsocket, so the Makefile's socket-lib probe gained a `-lnetwork` fallback (same probe, different library; a no-op where libc has sockets). **No source `__HAIKU__` guard needed** — every TU compiled clean. Unit suite **14,119 checks / 0 failures**. `smoke: 364 of 370 drivers, 1,994 checks`; the six that fail are not product defects on Haiku: `reading_trace` (the trace fixture's `<WORKSPACE>` normalisation does not strip Haiku's `/boot/system/cache` realpath prefix, so a byte-compare drifts — jichi reports the correct cwd), `portability_lint` (its own `grep -oE` extraction comes up empty under Haiku's grep — a test-tooling gap, cf. the BSD-grep class), `setup_keyfile` (a PTY-drive test-infra failure), and `parallel_run_id_base`/`parallel_salvage` (the orphan-base discovery returns empty — a Haiku process/worktree difference to investigate). **DRIVEN** — HRZ gateway, `jlu/qwen3-coder-next`, tool calling "native" (`TIER-V-HAIKU-82a3a0`: a text turn returned "READY", a tool turn ran the shell and reported a nonce it could only read by invoking the tool), reached from the guest over qemu NAT'd TLS. Rig: the provisioned `haiku-base.qcow2` + headless ssh; a `scripts/tier-v-haiku.sh` and the `parallel-salvage` investigation remain.

## Driven (PLATFORMS.md, the Driven register)

**yes** -- by hand

both turns, **2026-09-28** (M830), from the provisioned qemu guest: `jlu/qwen3-coder-next` over the **HRZ gateway** (reached over qemu NAT'd TLS), tool calling observed "native", agentic nonce `TIER-V-HAIKU-82a3a0` (a text turn returned "READY"; a tool turn ran a shell command and reported a file's nonce it could only read by invoking the tool). No rig script yet; a hand run

## The build notes (BUILD.md)

**Partly verified, and Driven** (M830): R1~beta6 (hrev60160) under qemu. `make WERROR=1` clean — 0 warnings, gcc 13.3.0 — after **one Makefile line** (Haiku's sockets are in `libnetwork`, added as a probe fallback) and **no source guard**; unit suite 14,119 checks / 0 failures; `smoke: 364 of 370 drivers, 1,994 checks`; both model turns driven. Its make is GNU make: type `make`.

`pkgman install curl_devel git` · `make`
