# Running programs on DOS: a design for review (def-269)

*Written 2026-09-29, after M839. Nothing here is built yet. This page is the design for the
operator to decide on, and every fact in §1 was measured on FreeDOS 1.3 with DJGPP 12.2.0,
by probes booted through the rig ([`scripts/tier-v-freedos.sh`](../../scripts/tier-v-freedos.sh)).*

## Why this is next

79 of the 103 unit failures still open on DOS are one fact: jichi starts other programs
the POSIX way, and DOS cannot. They are the shell tool, background jobs, the verifier,
git, the search tools, hooks, user tools, grading, and `pdftotext`
([DEFERRED def-269](../DEFERRED.md#def-269)). M832 funnelled every program start through
one primitive, `jc_spawn`, with a `fork` backend and a `posix_spawn` one, so that a
fork-less row would be "a backend choice rather than a port". This page says what that
backend has to be on DOS, and what it cannot be.

## 1. What DOS offers, measured

| Probe | Result |
|---|---|
| `fork()` | always −1, `ENOMEM` |
| `spawnvp(P_WAIT, …)` | works: runs the child to completion and returns its exit code (7 when the child returned 7) |
| the child's stdout, redirected with `dup2` to a file opened `O_BINARY` | works; the child writes CRLF, because its own stdout is in text mode |
| `pipe()` across a spawn | the child wrote 2,000 lines; **0 bytes** could be read back |
| `pipe()` within one process | 640,000 bytes written, `select()` said ready, **0 bytes** read back |
| `system()` | runs `COMMAND.COM`; a failing command ("Bad command or filename") **still returned 0** |
| `popen(…, "r")` | works, synchronously (the library runs the command to completion first) |
| a shell | no `SHELL`; `COMSPEC` is FreeCOM. DJGPP ports of bash exist (bash 4.2.53 and 4.4.28 on delorie.com's `v2gnu`), not yet measured here |

Two more facts follow from how DOS works, and neither was a probe: **only one program
runs at a time**, so a parent waits while its child runs; and **a child that never exits
cannot be stopped** by its parent, so a timeout cannot be enforced.

## 2. The callers, sorted by what they need

| Caller | What it does with the child | On DOS |
|---|---|---|
| shell tool (`jc_app_run_command_ex`) | runs a command, reads its output, enforces a timeout | possible, **without the timeout** |
| the `jc_proc_popen` path (`jc_app.c`, `jc_command.c`, `search_code`) | reads a command's output to its end | possible |
| verifier (`jc_envelope.c`) | runs `--verify`, reads output, timeout | possible, without the timeout |
| git snapshots and git tools | run `git`, read its output | possible, if a DOS `git` exists |
| `jc_proc_capture` (hooks, user tools, grading, `pdftotext`) | runs, captures, caps the output | possible |
| background jobs (`jc_bg.c`) | a child that runs **while jichi goes on** | **impossible**: refuse |
| language servers (`jc_lsp.c`) | a long-lived child, spoken to through pipes | **impossible**: refuse |
| MCP stdio servers (`jc_mcp_stdio.c`) | the same | **impossible**: refuse |

The parallel pool and the daemon are not `jc_spawn` callers (they fork jichi itself,
def-34), and they are already refused on this row: the pool by fork failing, the daemon by
`JC_NO_SOCKETS` (M835).

## 3. The options

**A. A third `jc_spawn` backend that fakes a pipe: ruled out by measurement.** The hope was
that a synchronous backend could run the child to completion into a temporary file, then
copy the file into the caller's pipe, so that no caller would change. §1 closes that door:
a DJGPP pipe returns nothing even to the process that wrote it.

**B. One capture primitive, and every collecting caller converted to it (recommended).**
`jc_proc_capture` already runs a command and collects its output with a cap. The shell
tool, the verifier, git and `search_code` would call it (or a sibling with the same
contract) instead of reading pipes themselves. On POSIX it keeps today's pipe loop, with
the timeout and the process-group kill. On a system without concurrent processes it runs
`spawnvp(P_WAIT)` with stdout and stderr redirected to temporary files, reads them, and
strips the CRLF the child wrote. The caller sees the same result either way, plus a flag
saying the timeout could not be enforced. The conversion is worth having on POSIX too: it
removes several copies of the same read loop.

**C. The long-lived callers refuse cleanly.** Background jobs, language servers and MCP
stdio servers need a second program running at the same time. On DOS each would refuse,
at configuration time where possible, with a message in the style of M835: *"this system
runs one program at a time, so …"*. `doctor` would list what is unavailable, and
`describe` would mark it, as M836 did for the daemon.

**D. The shell.** A model's commands are POSIX shell. FreeCOM cannot run them and loses
exit codes. Three ways:

1. **DJGPP's bash** (4.4.28), shipped next to jichi on the guest. It emulates pipelines with
   temporary files, and whether it runs typical model commands — `grep -rn x . | head`,
   `cmd && other`, `$?` — is the first thing to measure.
2. **Refuse `run_terminal_command`** on DOS, keeping the tools that do not need a shell.
   jichi still reads, searches, edits and answers; it cannot build or test.
3. **FreeCOM**, with a warning that exit codes are unreliable. Not recommended: a verifier
   whose failures read as success is worse than no verifier.

## 4. Recommendation

B, C and D.1, measured in this order, each step committed only after the rig shows it:

1. **Measure DJGPP's bash on the rig**: a probe that runs five typical model commands and
   checks their output and exit codes. If it fails, choose D.2 instead, and say so.
2. **The capture primitive (B)** on POSIX first, converting the callers one at a time,
   with the unit suite and the smoke tier green after each. This step changes nothing
   about DOS, and it is where most of the risk is.
3. **The DOS path of the primitive**, plus the probe that selects it (see question 1),
   measured on the rig: the target is the 79 failures, less those for which the rig has no
   programs (`git`, `pdftotext`).
4. **The refusals (C)**, with `doctor` and `describe` saying what is unavailable.
5. **Then Watt-32 with a DJGPP libcurl** (def-268), and a driven model turn, which is
   what moves the row off **Never compiled**.

## 5. Questions for the operator

1. **How should the build know that the system cannot run two programs at once?** Nothing a
   compiler can check answers it: DJGPP links `fork()` and it fails only when run. The
   choices are a run-time answer (try `fork()` once at start-up, where a failure costs
   nothing), a Makefile probe for the thing DOS *has* (`spawnvp` with `P_WAIT` in
   `<process.h>`), or an explicit build knob like M832's `NOFORK=1`. The project's rule is
   to probe for a capability, not to key on the platform's name, and the first two respect
   it. My recommendation is the run-time check, because it answers the actual question.
2. **Is shipping DJGPP's bash beside jichi acceptable** (D.1), given the project's rule of
   "libcurl and nothing else"? It would be a separate program on the DOS guest, not a
   dependency of the build, in the same way that `/bin/sh` is on every other row.
3. **Is a shell tool without a timeout acceptable on DOS**, if `doctor` and the tool's own
   result say so? The alternative is refusing the tool there (D.2).
