# Hardening review 6 — Platforms: what jichi asks of each system, and what each system could give it

*Part of the 2026-10-17 hardening and platform review of jichi; start with
[the overview](2026-10-17-hardening-00-overview.md). Drafted by reviewer E (Claude Opus 5.5), edited by the
review's lead. Commit `e5d832a4`; every `path:line` refers to that commit. Measurements were made on
an x86-64 workstation (Ubuntu 24.04, kernel 6.8, gcc 13.3, glibc 2.39) in private copies of the tree,
against the repository's own mock model; the review lead re-checked the central claims (cited as L-01
to L-15). Platform facts that were not measured here are marked [GK].*

---

## Who this is for, and what you will be able to do

**For** a self-learner or junior developer who wants to see how one C program is made to run on about
twenty operating systems -- Linux with four different C libraries, the three BSDs, illumos, macOS, Haiku,
GNU/Hurd, Cygwin and MSYS2 on Windows, FreeDOS and an Atari emulator -- and what each of those systems
offers that the program could use to protect itself. You do not need to know jichi.
[Chapter 1](2026-10-17-hardening-01-build-and-binaries.md) covers the build flags and the binary; read
it first if "PIE" or "RELRO" is new to you.

**After reading it you will be able to:**

- read a portability conditional and say whether it asks for a *capability* or for a *name*, and why the
  difference matters;
- explain what a signal disposition is, which ones a program inherits from whoever started it, and the
  two signal defects found here (E-01, E-03);
- say what changes when `long` is 32 bits wide, and where that bites jichi;
- choose, for a given platform, the next protection worth adding, using the matrix in section 7.

**You need first:** a C compiler, a built checkout of jichi (`docs/BUILD.md`), and `perl` for the one
demonstration in E-01.

---

## 1. The problem, and the vocabulary

**POSIX** is the standard that says what a Unix-like system provides: `fork`, `open`, `select`, terminal
control, signals. jichi is written in C89 (the 1989 C standard) and asks for POSIX.1-2001 by compiling
every file with `-D_POSIX_C_SOURCE=200112L` (`Makefile:217`). That macro is a *feature-test macro*: it
tells the system headers which interfaces to declare. On the BSDs, illumos and macOS it also *hides*
much of what POSIX.1-2001 does not contain (glibc and musl hide less) -- which is why some of the
platform facilities below need a second macro or a probe before jichi can see them.

Where systems disagree, a program has two ways to cope. It can ask **who** it is running on
(`#ifdef __FreeBSD__`, `uname -s`) or ask **what** the system can do (compile a tiny test program and see
whether it builds). The second is called a *capability probe*, and jichi uses it almost everywhere.

Two terms used throughout: **LP64** is the data model of 64-bit Linux, the BSDs and macOS, where `long`
and pointers are 64 bits wide; **ILP32** is the 32-bit model, where `int`, `long` and pointers are all 32
bits. A *fence*, in jichi's vocabulary, is a check that bounds what the agent's actions can reach (the
path fence keeps file tools inside the workspace; the shell fence keeps a parallel child's shell writing
only inside its own worktree).

## 2. What the tree asks of the platform: the inventory

All 70 conditional-compilation lines in `src/` and `include/` that are not header guards fall into three
groups.

**Capabilities the Makefile probes.** Positive macros (`JC_HAVE_*`) for something extra that is present:
`JC_HAVE_VSNPRINTF`, `JC_HAVE_MALLOC_TRIM`, `JC_HAVE_SET_DUMPABLE`, `JC_HAVE_PROC_TRACE_CTL`,
`JC_HAVE_POSIX_SPAWN`, `JC_HAVE_USE_LFN`, `JC_HAVE_CURL`. Negative macros (`JC_NO_*`) for something
normal that is missing: `JC_NO_CLOCK_GETTIME`, `JC_NO_NANOSLEEP`, `JC_NO_SYS_SELECT_H`, `JC_NO_MMAP`,
`JC_NO_SOCKETS`. `make info` prints every answer.

**Header facts tested directly.** `_SC_NPROCESSORS_ONLN` (CPU count), `_SC_PHYS_PAGES` (RAM),
`CLOCK_MONOTONIC`, `SIGCHLD`, `O_BINARY`, `PATH_MAX`, `LIBCURL_VERSION_NUM`, and the Landlock
syscall numbers. If the header does not define the name, the fallback branch is compiled.

**Operating-system names, only for per-OS facts.** Seven names appear, in five places, and each guards
something that really differs per system:

| condition | where | what it guards |
|---|---|---|
| `__APPLE__` | `src/platform/jc_platform_posix.c:11, 54, 747` | the Darwin header switch, and total RAM via `sysctl(HW_MEMSIZE)` |
| `__MINT__` | `jc_platform_posix.c:68` | a 512 KB fixed stack on FreeMiNT, which does not grow a stack |
| `__FreeBSD__` | `src/util/jc_proc.c:12` | making `<sys/procctl.h>` visible in one file |
| `__OpenBSD__`, `__NetBSD__`, illumos | `include/jc_proc.h:272` | the in-place environment scrub that hides API keys |
| `__linux__` | `src/util/jc_landlock.c:16, 24` | Landlock, a Linux-only kernel facility |

The fourth is worth reading, because it shows the rule written into the code (`include/jc_proc.h:264-274`,
the last sentence of the comment left out):

```c
/* OpenBSD, NetBSD and illumos have no dumpable/trace-control flag; the in-place
 * environ scrub (jc_proc_harden_secret_env) is their partial substitute. Pure
 * libc, so no Makefile probe -- gated on the compiler's own platform macro. Extend
 * the condition only for a platform MEASURED to (a) expose env via `ps`, `pargs`
 * or /proc/<pid>/environ and (b) hide it after the scrub, as OpenBSD (M796),
 * NetBSD (M798) and illumos (M799) were. ... */
#if defined(__OpenBSD__) || defined(__NetBSD__) || (defined(__sun) && defined(__SVR4))
#define JC_HAVE_ENVIRON_SCRUB 1
#endif
```

A name is used here because the *question* is about a name: "is this a system where the scrub was
measured to work?" -- and the comment says how to earn a place on the list.

**Which branches does anything actually compile or run?** The gate (`make ci`) has a stage,
`fallbacks-compile` (`Makefile:1451-1483`), that compiles every negative-macro branch, and -- through a
loop -- every file that names a `JC_HAVE_*` macro with that macro removed. So nearly every "other"
branch is at least compiled on every merge. The exceptions:

- **Never compiled by any gate or rig:** the three `__APPLE__` blocks. One contributor's Mac built them
  (M817); no script does.
- **Compiled only on a platform rig, never by `make ci`:** the FreeBSD `procctl` branch, the
  environment-scrub branch (it is defined in a header from OS names, so the removal loop never sees
  it), the MiNT stack size, the non-Linux Landlock stubs, the DJGPP long-file-name branch.
- **Compiled but never run:** the C89 fallback formatter in `src/platform/jc_snprintf.c`, used only where
  the C library has no `vsnprintf`. It has no field widths or precision and then reads the wrong
  arguments (DEFERRED def-75, open).

**Where a fallback degrades without saying so:**

- FreeMiNT: whole-second clocks without `clock_gettime` (def-96), and predictable identifiers without
  `/dev/urandom` (def-74).
- A C library without `vsnprintf`: the formatter above; `make info` warns, the binary does not.
- Possibly the BSDs: total RAM is read through `_SC_PHYS_PAGES` and `_SC_PAGE_SIZE`
  (`jc_platform_posix.c:776`); if strict POSIX hides either name there, the function returns 0, and 0
  means "unknown, assume a normal machine", so the low-memory profile never switches on [GK -- the
  `doctor` machine-profile line on a BSD rig would settle it].
- Linux in a container or under `taskset`: the CPU count is the host's (finding E-11).

**Where it degrades and says so (good):** the CPU count on FreeBSD and NetBSD reads 1 and `doctor` warns
(def-132); without `/proc` the memory watchdog is reported unavailable; without Landlock a warning line
and a telemetry event record that a child ran unfenced; without key protection `doctor` warns; without
sockets the daemon refuses with a message.

## 3. Design decisions

**Ask for the capability, never the name** (DECISIONS M658). *Decision:* the first illumos build needed
`-D__EXTENSIONS__` and `-lsocket -lnsl`; both became probes in the style of the existing clock probe --
compile the question, and only if it fails ask again with the extra piece. *Rejected:* `ifeq ($(shell
uname -s),SunOS)` -- "a platform name is a proxy for the question, and the proxy is what rots"; the
Darwin branch had been uncompilable for many milestones because nothing ever asked it anything (M400).
*Implications:* illumos, Haiku (one line: sockets live in `libnetwork`) and DJGPP each arrived as one
more probe and no source change. The cost is a 1,654-line Makefile with about thirty probes, run again on
every `make` (about one second of compiler runs per invocation on the workstation), each copying the
build's flags by hand -- and a probe that asks a slightly different question from the build answers
wrongly without a word (recorded in the project's internal portability analysis of 2026-10-15, section 5.3). *Lesson:* test for the
property you need; a name is evidence about the property only until the platform changes.

**Absence gets a negative macro** (DECISIONS M326u). *Decision:* `JC_NO_CLOCK_GETTIME` rather than
`JC_HAVE_CLOCK_GETTIME`. *Rejected:* the positive form, which "would make anyone compiling by hand --
without this Makefile -- silently drop every latency measurement to one-second resolution".
*Lesson:* choose the default that is right when nobody configured anything.

**Every fallback is compiled by the gate** (M723, M835). *Implication:* a branch that only FreeDOS takes
cannot stop compiling unnoticed. *The limit:* compiled is not tested -- the fallback formatter compiles
on every merge and is still wrong at run time. *Lesson:* "the gate compiles it" and "the gate tests it"
are different claims; write down which one you have.

**Key protection is chosen per operating system, and reported by its effect** (DECISIONS M770; M771,
M796, M798, M799). *Decision:* on Linux, `prctl(PR_SET_DUMPABLE, 0)` makes `/proc/<pid>/environ` and
`/proc/<pid>/mem` unreadable to the model's own commands; on FreeBSD, `procctl(PROC_TRACE_CTL)` does the
same for `ps -e`; on OpenBSD, NetBSD and illumos, which have neither, jichi overwrites its own copies of
the key variables in place. Each mechanism is read back, and `doctor` reports what is in force.
*Rejected for Linux:* overwriting the environment block, which hides `environ` but not the copy jichi
keeps, nor `/proc/<pid>/mem` -- one flag covers all three. *Implications:* good -- an honest "partial"
on the scrub platforms; a gap -- macOS, DragonFly, Haiku, the Hurd and the Windows layers have no
mechanism (def-30 records macOS). *Lesson:* report the effect, never the attempt.

**One way to start a program** (M832). *Decision:* every child goes through `jc_spawn`
(`src/util/jc_spawn.c`), with a `fork` backend (the default) and a `posix_spawn` backend for systems that
cannot fork. Both apply the same ritual: new process group, wiring of the three standard streams, the
key scrub, closing every other descriptor, resetting `SIGPIPE`. *Implication:* a fork-less platform needs
one backend, not a dozen call-site rewrites, and a smoke driver runs the same fences against both
backends. *The gap this review found:* the funnel owns the start of a child's life but not its end --
each caller reaps its own child, and two of them trust a status they never checked (E-01). *Lesson:* a
chokepoint should own both ends of a life cycle.

**A fence that is present but cannot be applied refuses the work** (M852). The Landlock shell fence for
a parallel write child: no Landlock on the kernel means the child runs unfenced *with a warning*; Landlock
present but the set-up failing means the task is refused. That is the right policy, and it is why the
32-bit defect E-02 fails closed rather than open.

## 4. Signals: what a program inherits, and two defects

A **signal** is an asynchronous notice from the kernel or another process: `SIGINT` (Ctrl-C), `SIGTERM`
("please stop"), `SIGHUP` (the terminal went away), `SIGCHLD` (a child process ended), `SIGPIPE` (you
wrote to a pipe nobody reads). Each signal has a **disposition** in each process: the default action, a
handler function, or *ignore*. Two rules are easy to forget:

1. Across `exec`, handled signals go back to their default, but **ignored signals stay ignored** [GK].
   So a program inherits every "ignore" its parent set.
2. A system call interrupted by a handled signal returns `-1` with `errno == EINTR` unless the handler
   was installed with `SA_RESTART`.

jichi installs every handler through one function (`src/platform/jc_platform_posix.c:231-247`):

```c
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    sa.sa_flags = 0;
    rc = sigemptyset(&sa.sa_mask);
```

`sa_flags = 0` means: keep the handler after the first delivery and do not restart interrupted calls --
jichi's loops check `EINTR` and the abort flag themselves. The function exists because the older
`signal()` call means different things on different C libraries; under jichi's flags glibc gave the
one-shot System V behaviour, so the second Ctrl-C killed jichi without any clean-up (M918, found by the
2026-10-08 review as R03-F1). The handlers only set flags, as `CONTRIBUTING.md` requires.

Two things are not normalised at start-up. jichi never sets `SIGCHLD` back to its default, and it has no
handler for `SIGHUP`. Findings E-01 and E-03 below show what each costs.

## 5. Thirty-two bits

On an ILP32 system `long` holds at most 2,147,483,647 (2^31 - 1). Two more types matter: `off_t` (file
sizes and offsets) and `time_t` (seconds since 1970). On 32-bit glibc both are 32 bits unless the program
asks for 64 with `-D_FILE_OFFSET_BITS=64` and, from glibc 2.34, `-D_TIME_BITS=64` [GK]. A 32-bit `off_t`
cannot describe a file above 2 GiB; a 32-bit `time_t` ends on 2038-01-19. jichi sets neither macro
(the project's internal portability analysis of 2026-10-15 measured `off_t` and `time_t` at 4 bytes under `gcc -m32`).

What the review found and measured on 32-bit:

- The unit suite builds with no warning and passes, 15,398 checks and 0 failures, as an ILP32 build on
  the workstation (L-09). That is a good result -- and it also shows the suite has no check that reaches
  the 32-bit defects below.
- The JSON printer turns every whole number between 2^31 and 10^15 into `-2147483648` on ILP32 and is
  undefined behaviour there (measured by the lead, L-04), and the diff renderer's table size can overflow
  a 32-bit `long` (A-03). Both are described fully in
  [chapter 2](2026-10-17-hardening-02-input-and-memory.md).
- The platform layer's own interface is typed `long` where it carries sizes and times, so the two
  macros alone will not fix it (E-09).
- The Landlock shell fence's set-up is not reliable on 32-bit Linux (E-02).

The bench has run 32-bit hardware: two Raspberry Pi Zero 2 W rows in `docs/PLATFORMS.md` are armhf
(`LONG_BIT=32`). On Raspberry Pi OS those are glibc systems [GK -- the row does not record the C library].

## 6. Findings

Severity follows the 2026-10-08 review: **high** = data loss, a crash, a security boundary or a fence
that does not hold; **medium** = a wrong result or a misleading report in a plausible case; **low** = an
edge case or a maintainability cost; **note** = worth knowing. Marks: [V] verified by reading, [M]
measured, [R] read but not traced, [GK] general knowledge.

### E-01. Started with `SIGCHLD` ignored, jichi reports a failing verifier as passing

- **Severity:** high, conditional on how jichi is launched -- a fence that does not hold. **Mark:** [M]
  (reproduced independently by the lead, L-14); [V] for the code path. **Status:** new.
- **The concept.** When a parent process ignores `SIGCHLD`, POSIX lets the kernel reap its children
  itself; `waitpid` then has nothing to collect and fails with `ECHILD` [GK]. Ignoring `SIGCHLD` is a
  common way to avoid zombie processes in Python and Perl supervisors -- and by rule 1 above, a program
  such a supervisor starts inherits the "ignore".
- **What happens.** jichi resets `SIGCHLD` only in its daemon (`src/main.c:16018-16020`). Two callers of
  `waitpid` start with `status = 0` and do not check whether `waitpid` succeeded
  (`src/chat/jc_envelope.c:1107, 1192-1197`):

  ```c
      waitpid(pid, &status, 0);
      if (killed) {
          return JC_VERIFY_TIMEOUT;
      }
      if (WIFEXITED(status)) {
          return WEXITSTATUS(status);
  ```

  A `status` of 0 decodes as "exited normally with code 0". So the verifier -- the command an autonomous
  run must pass before its work is kept -- reads as passing when it failed. The same shape in
  `src/chat/jc_app.c:712, 821, 858` makes a failing shell command report exit 0 when a run timeout or a
  memory budget is set (that is the path `run_terminal_command` takes then); without either it reports
  -1. Measured in a scratch directory (I ran these commands in exactly this form, with `jichi` on the
  `PATH`):

  ```sh
  mkdir -p ~/sigchld-demo && cd ~/sigchld-demo
  printf "Make the tests pass.\n" > brief.md
  jichi brief-check brief.md --verify "exit 1" < /dev/null | grep "baseline exit"
  perl -e '$SIG{CHLD}="IGNORE"; exec @ARGV' jichi brief-check brief.md --verify "exit 1" < /dev/null | grep "baseline exit"
  ```

  ```text
    baseline exit  : 1 (RED)
    baseline exit  : 0 (GREEN)
  ```

  (On a machine with no model configured, jichi first prints a short notice about that on standard
  error; it does not affect the result. `brief-check` makes no model call.) A gate for an unattended run,
  started from such a supervisor, keeps a broken tree and records it as green. A narrower route to the
  same wrong answer: a `SIGINT` or `SIGTERM` arriving during those blocking `waitpid` calls interrupts
  them (`EINTR`), and the unchanged 0 is read the same way [V by reading].
- **Recommendation.** Two lines of defence. First, early in `main()`, set `SIGCHLD` back to its default
  and unblock the signals jichi relies on (an inherited signal *mask* is the same class of problem).
  Second, make every blocking `waitpid` retry on `EINTR` and treat any other failure as "unknown", never
  as a status to decode. The project's own rule already says this: "a classifier's else-branch must not
  be a finding ... make the fallback say *unknown*" (`CLAUDE.md`).
- **Test that goes red first.** A unit test that ignores `SIGCHLD`, calls the new start-up function, then
  runs the verifier on `exit 1` and expects 1 (0 today); and a smoke driver using a small C launcher that
  ignores `SIGCHLD` (the shell's `trap '' CHLD` did not reproduce the defect in a measurement, so it is not
  a usable launcher).
- **Effort:** S.

### E-02. On 32-bit Linux the Landlock shell fence's set-up is unreliable

- **Severity:** medium (the fence fails closed: a parallel write task is refused, the fence never becomes
  weaker; 32-bit systems only). **Mark:** [M] on a 32-bit build on the workstation; [V] for the code.
- **Status:** already recorded, by reading only, in an internal report of the 2026-10-08 review (the
  utilities slice, finding 16; that report was paused before completion); still open at `e5d832a4`; no row in `docs/DEFERRED.md`.
- **The class.** A kernel-interface structure is filled from a C type whose width is smaller on 32-bit
  systems, so part of the structure is not initialised before it is passed to the kernel. Whether the
  kernel accepts the fence then depends on memory contents. A comment in the file states that the
  interface exists only on 64-bit Linux; it also exists on 32-bit Linux. File:
  `src/util/jc_landlock.c`, the rule-set set-up in `jc_landlock_write_fence_ex`.
- **Recommendation.** Zero the structure before filling it, and place the 64-bit value by byte order
  rather than by the host type's width; correct the comment; add a `DEFERRED` row until it lands.
  Compiling with `-ftrivial-auto-var-init=zero` (chapter 1, Recommendations step 3) masks this class of
  defect everywhere, which is a reason to adopt it -- but not a fix.
- **Test that goes red first.** A pure unit test of the byte packing, which runs on every platform
  without a kernel; and the existing Landlock unit test preceded by a call that leaves non-zero values on
  the stack, run in a 32-bit build.
- **Effort:** S.

### E-03. `SIGHUP` is not handled: a closed terminal kills jichi without clean-up, and its background jobs keep running

- **Severity:** low-medium. **Mark:** [M] for the orphaned processes; [V] for the missing handler; [GK]
  for which processes a real terminal hang-up signals. **Status:** new.
- **What happens.** `SIGHUP` occurs nowhere in `src/` [V]; its default action ends the process [GK]. The
  graceful path that `SIGTERM` takes -- among other things `jc_bg_mgr_free` (`src/chat/jc_bg.c:396-420`),
  which stops background jobs -- does not run. Background jobs (`run_terminal_command` with
  `run_in_background`) live in their own process group (`jc_bg.c:305`), and a terminal hang-up signals
  only the *foreground* group [GK], so they are not reached that way either. Measured against the mock
  model, with the signal arriving during a model call after the first reply had started a background
  `sleep`: after `SIGTERM` jichi exited 143 and the `sleep` was gone; after `SIGHUP` jichi exited 129 at
  once and the `sleep` was still running, re-parented to init. A development server the model started
  keeps its port after the terminal that started jichi is closed. Sessions are saved after every turn
  (`src/tui/jc_tui.c:5378-5381`), so little conversation is lost; what is lost is the clean-up and the
  end-of-run records.
- **Recommendation.** Install the existing handler for `SIGHUP` as well, treating it like `SIGTERM`, and
  make sure nothing then blocks on writing to a terminal that no longer exists. A run that should survive
  its terminal is what `nohup`, `tmux` or jichi's daemon are for; say so in `docs/DEPLOYMENT.md`.
- **Test that goes red first.** A smoke driver shaped like `tests/smoke/signals.sh`: start a background
  job with a unique command, send `SIGHUP`, expect exit 129 and the job gone. Red today.
- **Effort:** S.

### E-08. The descriptor clean-up before every child is 4,093 system calls; one call would do

- **Severity:** low (performance on small boards). **Mark:** [M] for the cost, [V] for the code.
  **Status:** the cap is recorded as a note in [chapter 3](2026-10-17-hardening-03-processes-and-fences.md)
  (B-11); the cost measurement is new.
- **What happens.** A child must not inherit jichi's open files (the run journal, the provider socket).
  jichi marks its descriptors close-on-exec when it creates them and, as a backstop, closes descriptors
  3 to 4095 one by one in every forked child (`src/util/jc_proc.c:463-482`); the `posix_spawn` backend
  sweeps the same range in the parent with two `fcntl` calls each (`:445-461`). Measured as fork, exec
  `/bin/true` and wait, 2,000 times: 0.56 ms without the loop, 1.29 ms with it, 0.57 ms with a single
  `close_range` call. The loop also stops at 4096, so a library's descriptor above that would cross.
- **Recommendation.** Probe for `closefrom` (the BSDs, illumos, glibc 2.34 and later [GK]) and
  `close_range` (Linux 5.9, FreeBSD 12.2 [GK]); keep the loop as the fallback. For the spawn backend,
  `close_range` with its close-on-exec flag (Linux 5.11 [GK]) replaces the parent sweep.
- **Test that goes red first.** `tests/smoke/child_fds.sh` with one more case: a descriptor opened
  without close-on-exec at number 5000; red today.
- **Effort:** S.

### E-09. Thirty-two bits: the platform interface carries sizes and times in `long`

- **Severity:** low today, rising as boards age. **Mark:** [V]; [GK] for C-library behaviour.
  **Status:** recorded in part -- the project's internal portability analysis of 2026-10-15 (5.2.2)
  found that no large-file or 64-bit-time macro is set; the interface part is new.
- **What happens.** `long jc_file_size(const char *path)` (`include/jc_platform.h:285`) returns
  `(long)st.st_size` -- with 64-bit offsets switched on, a 3 GiB file still becomes a wrong, negative
  size on ILP32. `jc_read_file` measures with `ftell`, which returns `long`, and maps every `fopen` failure
  to "not found" (`src/platform/jc_platform_posix.c:302-305`), so a large file on a 32-bit system without
  64-bit offsets is reported as missing [GK for the `EOVERFLOW`]. `jc_localtime_checked(long t, ...)`
  (`jc_platform.h:429`) receives `(long)time(NULL)` and will be wrong after 2038 even with a 64-bit
  `time_t`. And `readdir` without 64-bit offsets can fail on file systems with large inode numbers, which
  `jc_list_dir` would report as a short but successful listing [GK].
- **Recommendation.** Probe `-D_FILE_OFFSET_BITS=64` (and `-D_TIME_BITS=64` where glibc allows it); carry
  sizes and times through the interface as `double` -- the tree already does this for `jc_now_seconds`,
  `jc_file_mtime` and `jc_disk_space`, precisely to avoid `long long` in C89 -- and map `fopen` errors by
  `errno`.
- **Test that goes red first.** In a `-m32` build: a sparse 3 GiB file must have a size above 2^31 and
  `jc_read_file` must say "too big", not "not found".
- **Effort:** M.

### E-10. Four documents and a code comment deny an OS-name conditional that has existed since M796

- **Severity:** low (drift in the pages a porter reads first). **Mark:** [V]. **Status:** new.
- **What happens.** `docs/BUILD.md:273-283` says there is no `__NetBSD__` or `__OpenBSD__` branch at all;
  `docs/BUILD.md:41, 396`, `docs/PLATFORMS.md:124` and `docs/platform-notes/illumos.md:66` say no
  source conditional was needed for illumos; `src/platform/jc_platform_posix.c:709-714` calls a BSD
  conditional something that "would add this tree's first". `include/jc_proc.h:272` has had all three
  since M796, M798 and M799.
- **Recommendation.** Say "one per-OS conditional, for a per-OS feature, and otherwise capability probes",
  and let `portability_lint` count the OS-name conditionals against a number the page states.
- **Effort:** S.

### E-11. On Linux the CPU count ignores the process's CPU affinity and container limits

- **Severity:** low (bounded by a ceiling of 8 parallel children, `src/tools/jc_tool_parallel.c:60`).
  **Mark:** [V]; [GK] for the container behaviour. **Status:** new (def-132 covers the BSDs).
- **What happens.** `jc_cpu_count` is `sysconf(_SC_NPROCESSORS_ONLN)`
  (`src/platform/jc_platform_posix.c:721-726`): the host's online CPUs, not the CPUs this process may use
  (`taskset`, cpusets) nor a control-group quota [GK]. It sizes the parallel pool and the daemon's
  workers. A container limited to 2 CPUs on a 64-core host runs 8 parallel children.
- **Recommendation.** On Linux use the affinity mask (`sched_getaffinity`), capped by `cpu.max` when
  readable; keep `sysconf` as the fallback. On FreeBSD and NetBSD, `sysctl(HW_NCPU)` closes def-132.
- **Test that goes red first.** `taskset -c 0` around a jichi command that prints the core count must
  show 1.
- **Effort:** S.

### E-12. `jc_disk_free_mb` is unused and still carries a known bug

- **Severity:** note. **Mark:** [V]. **Status:** the bug is recorded in its own header
  (`include/jc_platform.h:369-389`: integer division reads 0 MiB on ZFS); that nothing calls the function
  any more is new. **Recommendation:** delete it, so the next caller picks `jc_disk_space`. **Effort:** S.

### E-13. Signal handlers write `volatile int`, not `volatile sig_atomic_t`

- **Severity:** note (every platform jichi runs on makes the two the same width [GK]). **Mark:** [V].
  **Status:** recorded by the 2026-08-17 source-hardening audit (§4); still open:
  `include/jc_app.h:584`. The C standard only promises that a handler may assign a
  `volatile sig_atomic_t`. **Recommendation:** change the two flags' type, or record the exception.
  **Effort:** S.

## 7. The platforms, one by one, ranked by what is worth doing

What each platform offers is general knowledge unless it says otherwise; "jichi today" is verified in
the tree. The value column is about protection or robustness for that platform's users; the effort is
for jichi's maintainers.

| platform | offers (beyond POSIX) | jichi today | worth doing next | value / effort |
|---|---|---|---|---|
| **Linux, glibc and musl** | Landlock (ABI 1-6: write rights; TCP connect/bind at 4; signal and abstract-socket scoping at 6 [GK]), `PR_SET_NO_NEW_PRIVS`, `PR_SET_PDEATHSIG`, `PR_SET_CHILD_SUBREAPER`, `close_range`, seccomp | non-dumpable with read-back (M770); Landlock write fence for parallel write children only, write rights up to ABI 3; no-new-privileges only inside that fence | (1) E-01 and E-03; (2) parent-death signal in every child, so a crash or hang-up never orphans one; (3) no-new-privileges for every model-issued child -- a model's `sudo` then fails even inside a cached sudo timestamp (an operator decision); (4) `close_range` (E-08); (5) the shell fence for the top-level shell, with signal scoping at ABI 6 (the open half of def-149) | high/S for 1-3; medium/S for 4; high/M for 5 |
| **FreeBSD** | `procctl` (trace control, parent-death signal, reaper, no-new-privileges since 14.0 [GK]), `closefrom`, Capsicum | trace control with read-back (M771); CPU count reads 1 (def-132) | parent-death signal; `closefrom`; the CPU count via `sysctl` or a visibility macro in the one platform file (`jc_proc.c:12-14` shows the pattern) | medium/S. Capsicum: poor fit -- it is all-or-nothing for a process that must open arbitrary paths [GK] |
| **OpenBSD** | `pledge(promises, execpromises)` -- the second argument restricts the programs jichi starts -- and `unveil` [GK]; `closefrom` | environment scrub, partial (M796) | an opt-in `pledge` of jichi itself; `execpromises` for the model's shell only after measuring what real toolchains need | medium/M, only if users ask: the row is a defect detector, not a deployment target |
| **NetBSD** | `closefrom` [GK] | environment scrub (M798); `/proc` file descriptors and memory stay readable, recorded in `docs/HARDENING.md` §2 | `closefrom` | low/S |
| **DragonFly BSD** | similar to FreeBSD, without trace control [GK] | never compiled here: its image has no serial console for the rig | a seeded rig image first; add it to the scrub list only after a measured row, as the header requires | low/M |
| **illumos** | fine-grained process privileges [GK] | environment scrub (M799) | drop the privileges that let the model's shell inspect or signal other processes [GK] | low/M |
| **macOS** | an Apple `posix_spawn` flag that closes every descriptor not named, and a ptrace deny flag [GK] | nothing for the key (def-30 open); built and driven once by a contributor (M817), no rig | measure whether the environment scrub hides the key from `ps` there; if so, add `__APPLE__` to the list; use the spawn flag in the spawn backend | medium/S with a Mac |
| **GNU/Hurd** | -- | partly verified and driven (M767); `PATH_MAX` is undefined there and handled (`src/util/jc_path.c:25`) [GK] | a rig; nothing new to add | -- |
| **Haiku** | single-user by design: every program runs as the same user [GK] | partly verified and driven (M830); no key protection is possible between processes of one user | one line in the platform notes saying so | note |
| **FreeDOS (DJGPP), FreeMiNT** | no `fork`, no `/dev/urandom`, 32-bit `time_t` | no-sockets build, DOS long-name check, broken-time-zone repair, 512 KB MiNT stack pinned by a lint | the synchronous spawn backend (def-269); the `gettimeofday` clock (def-96) | the rows' own milestones |
| **Cygwin, MSYS2** | POSIX on Windows; `fork` is emulated and costs 7-9x (`docs/BUILD.md`) | partly verified and driven; MSYS2 needs an `acl` mount before file privacy holds | measure the `posix_spawn` backend there -- if Cygwin's implementation avoids the full fork emulation [GK: not known here], it is the largest speed-up available on any row | possibly high/S to measure |
| **32-bit (ILP32) Linux** | the large-file and 64-bit-time macros | none set; suite passes (L-09) | E-02, then E-09 and the JSON printer's whole-number path; a `-m32` stage in the gate | medium/S-M |
| **Small boards** (Pi Zero 2 W 512 MB, LicheeRV Nano 256 MB, Pi 5 with 16 KB pages) | -- | the page size is read, not assumed (`jc_proc.c:698`); lite profile switches on below 1 GiB | build the boards with `OPT=-O2` or `SIZE=1` (the parsers take half the CPU at `-O2`, chapter 1); measured board numbers are in chapter 7 | medium/S |

Across all platforms, ranked once more:

1. E-01 (reset `SIGCHLD`, check every `waitpid`) -- high value, small effort, every platform.
2. E-03 plus a parent-death signal -- no orphaned children after a hang-up or a crash.
3. E-02 -- a one-line fix to a fence on 32-bit Linux.
4. No-new-privileges for model-issued children (Linux, FreeBSD 14) -- an operator decision.
5. `closefrom`/`close_range` (E-08).
6. The top-level shell fence on Linux (def-149) -- the largest protection gain, a design decision.
7. The macOS scrub measurement; the Cygwin spawn measurement.
8. `pledge`, illumos privileges, seccomp, Capsicum -- low value for effort today.

## 8. Recommendations, in order

1. **Normalise inherited signal state at start-up (E-01).** Reset `SIGCHLD` to default and unblock
   `SIGCHLD`, `SIGINT`, `SIGTERM` and `SIGHUP` before anything starts a child; make every blocking
   `waitpid` retry on `EINTR` and return "unknown" on any other failure. Add the unit test and the
   launcher-based smoke driver.
2. **Handle `SIGHUP` like `SIGTERM` (E-03)** and, on Linux and FreeBSD, give every forked child a
   parent-death signal.
3. **Fix the Landlock packing (E-02)** and add the `DEFERRED` row until it is merged.
4. **Add a `-m32` unit-suite stage** where multilib exists (the lead built and ran it in one command,
   L-09), with the tests E-02, E-09 and the JSON printer need -- otherwise a green 32-bit run proves
   little.
5. **Replace the close loop with `closefrom`/`close_range` behind a probe (E-08).**
6. **Decide on no-new-privileges for every model-issued child**, with an opt-out key.
7. **Carry sizes and times as `double` in the platform interface, then probe the 64-bit macros (E-09).**
8. **Correct the drift (E-10)** and delete `jc_disk_free_mb` (E-12).
9. **Measure:** the scrub on macOS, the spawn backend on Cygwin, and `_SC_PHYS_PAGES` on the BSD rigs.

## 9. What is done well

- **Capability probes with a written-down reason for each.** Every probe in the Makefile names the
  platform that taught it. Haiku needed one line; illumos two probes; no source conditional for either.
- **Every fallback branch compiled on every merge**, including a curl-free build run under a 512 KB
  stack, the stack FreeMiNT gives jichi.
- **One spawn chokepoint** with the same rituals on both backends and a driver that runs the fences
  against both.
- **Key protection by effect.** Each mechanism is read back; `doctor` says "partial" where it is partial.
- **The second Ctrl-C was fixed properly**: one installer built on `sigaction`, with the reasoning in the
  header (`include/jc_platform.h:75-96`).
- **Landlock fails closed** when it is present but cannot be applied, and says loudly when it is absent.
- **Non-Linux rows are treated as defect detectors**: most of what they found -- children inheriting an
  ignored `SIGPIPE`, a recursive path resolver too deep for a small stack -- were defects on every
  platform.

## 10. What this chapter did not review, and open questions

Not read: most of `src/main.c` (19,008 lines), `src/net/jc_http.c` beyond its conditionals, the TUI's
own loops beyond a search (so how `SIGHUP` interacts with a TUI blocked reading a terminal that has gone
away is [R]), the platform rigs under `scripts/`. Open questions a maintainer can settle with one
command each:

- Which C library and `time_t` width did the armhf Pi Zero 2 W rows have (`ldd --version`,
  `getconf LONG_BIT` on that image)?
- Is Landlock enabled in the kernels of the 32-bit-capable boards (`cat /sys/kernel/security/lsm`)?
  That decides whether E-02 is reachable on the bench's hardware today.
- Does `jc_mem_total_mb()` return 0 on FreeBSD, NetBSD or OpenBSD (the `doctor` machine-profile line on
  each rig)?
- Does Cygwin's `posix_spawn` avoid the fork emulation?

## Read further

- `docs/PLATFORMS.md` -- every platform row, with its evidence and its verdict word.
- `docs/DECISIONS.md`, entries M658, M326u, M723 and M770 -- the decisions in section 3, each with its
  rejected alternatives.
- `docs/HARDENING.md` §2 -- the key-protection mechanisms per operating system, measured.
- `tests/smoke/portability_lint.sh` and `tests/smoke/posix_utils_lint.sh` -- the rules this chapter
  describes, held by lints.
- `docs/C_STANDARDS.md` -- C89, and the difference between undefined and implementation-defined behaviour.
- The POSIX pages for `sigaction`, `waitpid` and `exec` (rule 1 of section 4 is in the `exec` page).
- [Chapter 1](2026-10-17-hardening-01-build-and-binaries.md) (the build and the binary),
  [chapter 3](2026-10-17-hardening-03-processes-and-fences.md) (processes and the fences), and
  [chapter 7](2026-10-17-hardening-07-resources-and-small-boards.md) (resources and the small boards).
