# Windows (Cygwin, WSL2) — the porting history

*Moved out of [`PLATFORMS.md`](../PLATFORMS.md) at M780 so the matrix stays short and the [install page](../install/) stays a lean walkthrough. This is the developer/porting record — the defects each row found and how they were fixed — kept verbatim; `git log -S` on any phrase names the commit that wrote it.*

## The Cygwin row: 209 of 209, and two tests that assumed pid 1 (2026-08-18)

M477. The headline is dull in the best way: **Cygwin needed no change to jichi.** The
POSIX emulation carried the whole program. Everything that had to be fixed was either
the *build system's* probes (M476) or the *test suite's* assumptions about its host.

**Two tests asserted that pid 1 exists.** Cygwin has no pid 1 at all -- `kill(1, 0)`
gives `ESRCH`, `/proc/1` is absent, and a Cygwin shell reports `PPID 1` for a parent
that is not a process. Both tests said so in a comment, as fact:

- `tests/test_lease.c:115` -- *"pid 1 always exists on a POSIX system, and belongs to
  root -- so this also covers the EPERM branch"* -- then `JC_CHECK(jc_lease_pid_alive(1)
  == 1)`.
- `tests/smoke/lease.sh` -- *"pid 1 always exists, so this is a live holder on any POSIX
  system"* -- then a lease record naming `"pid":1`.

The smoke failure is the instructive one, because of which checks failed. jichi read the
record, asked whether the holder was alive, was told no, and correctly took the lease as
stale. So checks 4-6 ("a live holder is refused") failed while check 7 (**"a STALE lease
does not block a run"**) passed -- in the same driver, in the same run, on the same host.
A suite disagreeing with itself like that is naming a fixture, not a defect: both
verdicts are correct for a host where pid 1 does not exist.

**The comment hid a second error, and it is the more interesting one.** Both claimed to
cover `jc_lease_pid_alive`'s EPERM branch (*"exists and belongs to someone else"*), which
is true only for a **non-root** user: as root, `kill(1, 0)` **succeeds** and the ordinary
branch is taken. This project's WSL2 and container rows run as root, so on the platform
where the check passed most reliably it was not testing what it said. The fix keeps the
pid-1 case, gated on both conditions and skipping loudly via `t_skip_one` otherwise,
rather than deleting a branch's only coverage to make a portability problem go away.

The `test_meminfo.c` failure is the same species. `jc_meminfo_parse` zeroes its `hwm`
out-param and fills it only `if (hwm_found)`, so **`hwm == 0` is a documented sentinel
for "this host has no `VmHWM:`"** -- which Cygwin's `/proc/self/status` does not. The test
asserted `hwm >= rss` unconditionally, i.e. something the module never promised. Fixed to
honour the sentinel.

**What a learner should take from this row.** Three of the four things that broke were
*tests and probes*, not product code, and every one of them had written its assumption
down in a comment beside the assertion. That is the good case: the assumption was
visible. What none of them did was mark it *conditional*. "pid 1 always exists on a POSIX
system" is not a fact about POSIX, it is a fact about the hosts the author had run on --
and a new platform is exactly the instrument that tells them apart. If you are writing a
check that depends on the shape of the machine, say which shape, and make the check skip
where the shape differs instead of asserting through it.

## The WSL2 row, and the four defects it found (2026-08-18)

M475. Windows 11 Pro 25H2 (build 26200.9168), WSL2 with Ubuntu 24.04, kernel
`5.15.167.4-microsoft-standard-WSL2`, 12 cores / 7.6 GiB. gcc 13.3.0, clang 18.1.3,
Valgrind 3.22, libcurl 8.5. Dependencies exactly as `BUILD.md` lists, plus `clang`
and `valgrind` for the gate.

**The build was the easy half, which is the point.** `make` clean on the first try;
`make test` **12,418 checks / 0 failures in 13 s**; `make smoke` **209 drivers /
1,104 checks**. All five configure probes resolve identically to a native Linux box
(`c89 (strict)`, curl, vsnprintf, malloc_trim, `clock_gettime` in libc) — there is no
WSL-specific configure degradation.

**The pty layer is real, and needs no timeout multiplier.** `BUILD.md` warns the
smoke tier's deadlines assume a quiet box (the Pi-class rows run 19–28). WSL2 holds
them at **`JC_SMOKE_TIMEOUT_MULT=1`**: `/dev/ptmx` present, `devpts` mounted,
`openpty()` allocating real slaves. Of the three risk areas M400 predicted — `/mnt/c`
semantics, the terminal, `$HOME` interop — **the terminal is not one.** Do not add a
multiplier here pre-emptively; that would mask a regression rather than compensate
for slow hardware.

**Keep the checkout on ext4, not `/mnt/c`.** `PREPARE_AND_BUILD.md` already says so,
and the measurement is blunt: the same commit reads **0 modified** on ext4 and
**1,639 modified** through `/mnt/c` (v9fs), because line-ending and permission
translation presents as edits. `stat -f -c %T .` answers which you are on —
`ext2/ext3` (ext4 reports this) versus `v9fs`.

**`make ci` failed four times before passing, and every cause was pre-existing.**
None is WSL-specific; all four reproduce on bare-metal Ubuntu 24.04. WSL2's
contribution was being the first environment where someone ran the *whole* gate with
clang 18 and **without emacs**.

| Stage | Defect |
|---|---|
| 2 (`WERROR=1 CC=clang`) | Three GCC-only warning flags handed to clang unconditionally; `-Werror` promoted `-Wunknown-warning-option` to fatal. Now probed per compiler — and the probe **must** carry `-Werror`, because clang exits 0 for a flag it does not implement |
| 3 (`SAN=1 CC=clang`) | `test_sb_reserve_bounds` asks `realloc` for `SIZE_MAX/2+2` on purpose and needs NULL back; ASan aborts instead, so the check was unreachable |
| 5 (valgrind) | The same call logs a "fishy value" = one valgrind error; a forked child **inherits** the count, so `--error-exitcode=1` overrode a later `test_pdf` child's `_exit(127)` with 1 — **a failure in `test_pdf.c` caused by a test in `test_str.c`** |
| e2e | `elisp-compile`/`elisp-test`/`slides` printed `skipping` and then ran the tool anyway: `exit 0` ends one recipe *line's* shell, not the recipe |

This is the **third** instance of the shape M447 and M189 already document: four `ci`
configurations are compiled by `make ci` and by nothing else, so anything added
between full gate runs breaks them invisibly. All four defects trace to M472, whose
additions were measured against gcc, unsanitized, on a machine with emacs. *The cure
is not more care; it is running the other configuration.*

**Two defects found only by running as a non-root user, and reported not fixed** (see
[`DEFERRED.md`](../DEFERRED.md)):

- **`jc_eventlog_open` chmods a parent directory it does not own.**
  `make_parent_dir()` sets the log path's parent to 0700 even when it already
  existed; the guard catches `/` and nothing else. `--log /tmp/jichi.jsonl` **as
  root** — every container, most CI — turns `/tmp` into root-only machine-wide
  (measured: 1777 before `run_tests`, 700 after). Non-root is inert only because the
  `chmod` fails with EPERM and the return is discarded. `jc_platform_posix.c` reasons
  about exactly this hazard for the state root, with an `lstat` owner/mode guard;
  `make_parent_dir` is two files away and has none.
- **A failed session precondition crashes the suite.** With `/tmp` at 0700 a non-root
  `make test` gives `FAIL test_session.c:759`, then four more FAILs reading a struct
  that was never populated, then `free(): invalid pointer` / SIGSEGV. `JC_CHECK` is
  non-fatal by design, so a failed precondition is followed by code dereferencing its
  output — and the crash then discards the block-buffered FAIL lines that name it.

**Live model.** Verified against an OpenAI-compatible local server (LM Studio on the
LAN) from inside WSL2 — NAT needs no port forwarding. `doctor` 0 problems, chat,
reasoning, **tool use** (agent loop → `read_file`), embeddings via a `roles:["embed"]`
model, and the model-gated e2e drivers `headless_model` and `acp_terminal` both green.
Two numbers worth keeping: jichi's built-in system prompt is **~754 tokens** (~3.9k
with the 18 built-in tool definitions) — the ~14.2k figure you get running `jichi
context` *inside jichi's own repo* is mostly its 130 KB `CLAUDE.md` as project rules —
and the OpenAI-compatible `/v1/models` does **not** report context size, while LM
Studio's native `/api/v0/models` reports `loaded_context_length`, which is the number
that actually binds. Declare `contextLength` to match it and jichi trims to fit;
omit it and jichi budgets against its 32000 default and the server rejects the result.

One thing to expect from a small local model, so a blank screen is not mistaken for a
broken setup: asked *"Reply with exactly: SETUP_OK"* this one spent **49 output tokens
and emitted no answer text** — empty stdout, exit 0 — while a plainer *"Say OK"*
answered immediately. The budget went to reasoning. **`--output json` is the honest
diagnostic here**: it shows `text`, `tokens` and `stop_reason`, so "the model answered
nothing" is distinguishable from "the call failed". Bare stdout cannot tell you which.

**The newcomer path was verified too, not just the gate.**
[`PREPARE_AND_BUILD.md`](../PREPARE_AND_BUILD.md)'s WSL walkthrough was executed
literally by a **non-root user with sudo** (what `wsl --install` actually produces)
against **pristine HEAD**: the documented `apt` line, the tool check, the archive
route, `make` (exit 0), `make test` (**12,417 checks / 0 failures**), `./jichi
--version`, and `./jichi doctor` — 20 ok, 8 warnings, **0 problems**, printing the
stderr hint the page promises. `make install` and `make install-check` also pass, the
installed binary runs live inference from outside the build tree, the M170 aliases
resolve, and `man jichi` renders.

The **interactive setup wizard** needs no separate test: `tests/smoke/setup_keyfile.sh`
already drives it through `ptydrive` and contributes **28 checks**, all green here —
including *"no menu prompts without showing its default"* and *"small-local is offered
under the machine question"*. Worth knowing before building a rig for it: the wizard
reads the terminal rather than stdin, so a `script`/`yes` pipeline delivers nothing and
the prompt appears to loop forever. That is the harness, not the wizard. **The project's
own pty driver is the tool; look for it first.**

The full session write-up — including six false verdicts from my own instruments, and a
seventh where I nearly filed two defects against this wizard from a rig that was never
sending input — is anecdote 62 in [`ANECDOTES.md`](../ANECDOTES.md).

## Cygwin measurement log (moved from the PLATFORMS.md cell, M781)

Cygwin **3.6.10** on Windows 11 (build 26200), gcc **14.4.0**, `/home` on NTFS. `uname -s =
CYGWIN_NT-10.0-26200`. Reproduce: `scripts/tier-v-cygwin.sh --ref-secs <bench>` — a LOCAL rig
(this layer is installed on the machine running it).

- **M477 (2026-08-18)** — `make WERROR=1` clean, full feature set, **but only after M476**: before
  it, every feature probe linked to `-o /dev/null`, which Cygwin's linker refuses, so all eight
  answered "no" and produced a silently degraded binary (gnu89, networking out, no hardening).
  **12,418 unit checks / 0 failures**, smoke 209 / 1,081 — after fixing two tests that assumed a
  pid 1 (the platform needed no product change).
- **M697/M698 (2026-09-22) — re-measured and DRIVEN:** `13,438 unit checks / 0 failures`; tier 317
  drivers (`13 ok, 1 failed` through the rig). Driven via **the HRZ gateway over TLS** (LM Studio is
  not installed here), agentic nonce `TIER-V-cygwin-3C2C3F`. The **fork penalty** is the number to
  plan around: **~7–9×** warm (45–59 s vs WSL2's 6.2–6.5 s), so a single `JC_SMOKE_TIMEOUT_MULT` is
  stated as a range, not one figure. A `setup_keyfile` failure at multiplier 13 was **the rig, not
  jichi** (it exported the gateway key, so the wizard correctly declined to store one already set);
  with the variable unset the driver passes 28/28. `pdf`/`docs_pdf` resolved `pdftotext` to a native
  Win32 Git-for-Windows binary that cannot open a Cygwin path — the drivers now use a functional
  probe. Full account: [`../analysis/2026-09-22-tier-durations-cygwin.txt`](../analysis/2026-09-22-tier-durations-cygwin.txt).

## MSYS2 measurement log (moved from the PLATFORMS.md cell, M781)

MSYS2 **3.6.10**, gcc **15.3.0**, the **MSYS** environment (`msys-2.0.dll`); MINGW64 is a separate,
unmeasured row, and `scripts/tier-v-msys2.sh` refuses to run under it. `uname -s = MSYS_NT-10.0-26200`.

- **2026-08-19** — `make WERROR=1` clean in 94 s, fully featured (M476's `/dev/null` defect does not
  recur on a second emulation layer). **The finding:** the root is mounted `noacl`, so **`chmod`
  returns success and changes nothing** — the API key file, the daemon socket and the audit log are
  left world-readable. Same machine/NTFS/user as Cygwin, whose `chmod 600` yields 600. The fix is an
  `acl` mount for **both** `/tmp` and `/home` (`C:/msys64/home /home ntfs binary,acl 0 0`); the
  installation root is handled specially and cannot be remounted this way. `doctor` probes the real
  state root and names the mount, the files and the fix. Also `MSYS=winsymlinks:nativestrict` is
  needed or `ln -s` copies directories; `diffutils` is absent by default.
- **M697/M698 (2026-09-22) — DRIVEN:** unit 13,428–13,438; tier 317 drivers (`14 ok, 2 failed`
  through the rig), agentic nonce `TIER-V-msys2-D1A043`, via the HRZ gateway over TLS. Build ~98 s,
  about **0.78× Cygwin's 125 s** on the same machine — the first clean like-for-like of the two
  emulation layers. The three killed drivers are the same three Cygwin kills, at a tighter deadline,
  on an independently maintained runtime — which attributes the cost to *emulating POSIX over Win32*
  rather than to either implementation. Full account:
  [`../analysis/2026-08-19-msys2-first-row.md`](../analysis/2026-08-19-msys2-first-row.md).
