# jichi on Windows: where POSIX ends

Three honest layers: the path that works today, a guided **porting survey**
you can run as an experiment (or as the graded class assignment it ships
with), and what a native port would actually require — subsystem by
subsystem, with the reasons it is out of scope for jichi proper.

## Layer 1 — the supported path: WSL

jichi is Linux/POSIX-only by design, and **WSL runs it exactly as on
Linux** — this is the normal, supported way to use jichi on a Windows
machine, walked step by step in
[PREPARE_AND_BUILD.md](PREPARE_AND_BUILD.md) §Windows (via WSL). If you
want jichi *working* on Windows, stop reading here; the rest of this page
is for learning what "POSIX-only" means in practice.

## Layer 2 — the survey: try to build it where POSIX thins out

Environments sit between WSL (full Linux) and native Win32, and they make an
excellent portability laboratory:

- **Cygwin** provides a POSIX *emulation layer* as a DLL (`cygwin1.dll`): `fork`,
  termios, signals, unix-domain sockets, even `/proc` — jichi may build nearly
  unmodified there, and every place it doesn't is a lesson.
- **MSYS2/MinGW** compiles against the *Windows* C runtime: no `fork`, no
  termios, no `/proc` — jichi hits walls early and often, and *which* wall,
  in *what order*, is exactly the survey.

> **MSYS2 is not one environment, and the distinction decides what you measure
> (corrected 2026-08-18).** MSYS2 ships several, and they sit on *opposite* sides of
> the POSIX line:
>
> | MSYS2 environment | C runtime | `fork` / termios / `/proc` | expect |
> |---|---|---|---|
> | **MSYS** (`msys-2.0.dll`) | a Cygwin fork | **yes**, emulated | close to Cygwin |
> | **MINGW64 / UCRT64** | native Windows CRT | **no** | walls, early |
> | **CLANG64** | native Windows CRT | no | walls, plus clang diagnostics |
>
> So the bullet above describes **MINGW64**, not the **MSYS** shell — and the Method
> below says *"in the MSYS shell"*, which lands you on the POSIX-**emulating** side.
> Launching `mingw64.exe` and launching `msys2.exe` measure different platforms; run
> the toolchain from the wrong one and the survey records a result that looks real
> and is not. **Note which launcher you used with every result.**
>
> That makes the gradient four points, not two — and having *both* DLL-based layers is
> a genuine gain: if Cygwin and MSYS2/MSYS agree, that is evidence about POSIX
> emulation in general; if they diverge, it isolates Cygwin-*version* behaviour from
> emulation behaviour. One environment cannot separate those.
>
> **Layer 2 is measured for both emulation layers now** (Cygwin M477; MSYS2/MSYS
> 2026-08-19, [`analysis/2026-08-19-msys2-first-row.md`](analysis/2026-08-19-msys2-first-row.md)).
> Neither needed a product change. Cygwin's defects were all in jichi's own build
> probes (M476) or in three tests asserting the shape of their host -- two assuming a
> pid 1 exists, one assuming `/proc/self/status` carries `VmHWM:`. MSYS2 builds clean
> in 94 s, fully featured, and runs 12,440 unit checks and 211 smoke drivers.
>
> **MSYS2 is only *partly* verified, and the reason matters before you use it.** It
> mounts `noacl`, so **`chmod` returns success and does nothing**: jichi's
> file-privacy guarantees therefore do not hold, and the API key file, the daemon
> socket and the audit log are world-readable while jichi reports no problem. Cygwin
> on the same machine, same NTFS, same user yields 600 where MSYS2 yields 644 -- so
> this is shipped configuration, not a Windows limitation. Two more MSYS2 practicals:
> export `MSYS=winsymlinks:nativestrict` or `ln -s` silently makes a directory copy,
> and install `diffutils`, since a default install has neither `diff` nor `cmp`.
> `make ci` has not been run on either.
>
> **MINGW64 remains unmeasured**, and it is the interesting row: the native CRT is
> where POSIX emulation stops.

> **Layer 1 is now measured** (M475, [`PLATFORMS.md`](PLATFORMS.md)): WSL2 runs the
> full `make ci` green. That gives the survey a calibrated top of the gradient — any
> failure below it is the emulation layer, not jichi.
>
> **Expect the compile to be the easy half.** On WSL2 jichi built clean on the first
> try; what separates these environments is pty behaviour, signal delivery, and
> `select` on pipes versus sockets — which is what `make smoke` exercises through
> `ptydrive`. On MINGW64 there are no ptys at all, so **smoke is the likely wall, not
> the compiler.** Also: `make ci` cannot run on any of them (no valgrind), so
> `make check-target` is the realistic gate — exactly as [`BUILD.md`](BUILD.md)
> prescribes for a box lacking the optional tooling.

**Method** (this is the assignment's method too):

1. Install the environment + toolchain (Cygwin: `gcc-core`, `make`,
   `libcurl-devel`; MSYS2: `pacman -S gcc make libcurl-devel` in the MSYS
   shell). Clone jichi, run `make`, capture everything.
2. For each failure, record: the file:line, the missing or misbehaving
   thing, and its **class**:
   - `missing-header` — e.g. no `<sys/select.h>` variant, no `<termios.h>`;
   - `missing-symbol` — compiles but won't link (`fork`, `killpg`);
   - `semantic` — exists but behaves differently (path forms, `select` on
     pipes vs sockets, signal delivery, permission bits on NTFS);
   - `runtime` — builds, then fails live (`/proc/self/status` absent,
     `AF_UNIX` path length/ACL rules, `SIGPIPE` never arriving).
3. Fix what one line fixes, note what it took, and stop at **the wall** —
   the first failure whose fix would be a subsystem rewrite rather than a
   patch. Naming the wall precisely is the survey's real product.
4. Compare against Layer 3's table: did you find walls it misses? That is
   a contribution, not a discrepancy.

## Layer 3 — what a native port would require (and why not)

jichi's actual POSIX surface, from the code (each row names the subsystem
that owns it):

| POSIX dependency | Used by | Win32 equivalent | Port cost |
|---|---|---|---|
| `fork`/`exec`/pipes/process groups | tools, snapshots (git), MCP stdio, the parallel pool, daemon workers, user tools (10 fork sites) | `CreateProcess` + job objects | **high** — no COW `fork`; the parallel pool's fork-with-inherited-state model needs a redesign, not a shim |
| termios raw mode + `TIOCGWINSZ` | the TUI line editor | Console API / VT mode | medium — Win10+ VT sequences help, the raw-mode editor still needs a console-input rewrite |
| `select` over pipes/sockets/PTYs | SSE streaming, MCP, control socket, bg processes, daemon | `WaitForMultipleObjects`/IOCP | medium-high — Winsock `select` only takes sockets; every pipe loop changes |
| unix-domain sockets | the control channel, the daemon | `AF_UNIX` exists on Win10 1803+ | low-medium — but no `SIGPIPE`/credentials semantics; 0600-socket security model differs |
| signals (`SIGINT`/`SIGTERM`/`SIGPIPE`, `sig_atomic_t` flags, `killpg`) | graceful abort, supervisor kills, watchdogs | console ctrl handlers + job termination | medium — the *semantics* (graceful-once-then-default, group kills) need re-expression |
| `/proc` (`self/status`, per-pid stat) | RSS telemetry (M180), the memwatch (M117) | `GetProcessMemoryInfo` | low — cleanly isolatable |
| `realpath`, `mmap`, `umask`/0600, `~` paths | path fence, index residency, private sinks | `GetFullPathName`, `CreateFileMapping`, ACLs | medium — the path *fence* must be re-proven against drive letters, UNC paths, and case-insensitivity |


**Re-measured 2026-08-19**, mechanically rather than by reading: `grep -rn` over
`src/**/*.c` with comment-only lines dropped, split by whether the file lives in the
platform layer. The table above was written from the code and holds; the numbers have
drifted and the split is worth stating explicitly, because it is the thing a reader
is most likely to guess wrongly.

| Facility | call sites | files outside `src/platform/` | files inside |
|---|---|---|---|
| `fork(` | **12** (was 10) | **12** | **0** |
| `dup2(` | 21 | 9 | 0 |
| `waitpid(` | 16 | 11 | 0 |
| `select(` | 15 | 11 | 0 |
| `poll(` | 12 | 4 | 0 |
| `sun_path` (AF_UNIX) | 10 | 2 | 0 |
| `tcsetattr(` / `tcgetattr(` | 9 | 2 | 0 |
| `setpgid(` | 6 | 3 | 0 |
| `execv*` | 5 | 5 | 0 |
| `chmod(` | 3 | 1 | **1** |
| `/proc/` | 5 | 3 | **1** |

Process creation alone spans **nine directories** -- `src/chat` (3 files),
`src/tools` (2), and one each in `src/util`, `src/tui`, `src/snapshot`,
`src/session`, `src/mcp`, `src/lsp`, plus `src/main.c`. Only `chmod` and the procfs
reads have any presence inside the platform layer at all, and `src/platform/` is
**849 of 85,246** product C lines, about 1%.

So `jc_platform_posix.c` is **not the seam for a non-POSIX target**, and the file's
existence should not be read as implying one. What it isolates is the places where
*POSIX systems disagree with each other* -- procfs, `mkstemp`, path canonicalisation
-- and the FreeBSD, OpenBSD and NetBSD rows are the evidence that it does that job.
Windows-native is not supported by design, so no Win32-capable abstraction was ever
promised; a program that targets POSIX calling POSIX directly is honest about what it
is, not badly layered. The measurement is recorded because the *inference* from
seeing a platform directory is tempting and wrong.

**What this means for actually running MINGW64: less than it might seem, which is a
saving.** The load-bearing facts need no compiler -- `fork` is absent from the Windows
CRT and jichi calls it in twelve places; there are no ptys and the 209-driver smoke
tier is built on `ptydrive`; Winsock's `select` takes sockets only and jichi selects
on pipes in eleven files. A MINGW64 attempt would add three narrower things: which
translation units compile clean (probably most of the library half -- json, str, vec,
config, markdown, diff, base64 -- which would put a number on how much of jichi is
genuinely portable C89), which failures come from an absent `<unistd.h>` rather than
an absent function, and whether `pkg-config`/libcurl resolve there at all. Worth a
timebox, not a milestone.

One CRT detail if anyone does try: prefer **UCRT64** over MINGW64. Legacy
`msvcrt.dll`'s `printf` is notoriously non-conformant while UCRT is close to
standard -- and jichi's `%lu`-with-casts convention, forced by the armhf row where
`long` is four bytes, is exactly the style that survives either.

The honest conclusion: a native port is a **platform-layer rewrite**
(`jc_platform`, `jc_proc`, the select loops, the TUI input core — roughly
the bottom fifth of the tree) plus a security re-audit of the path fence
under Windows path semantics. Cygwin buys all of it "for free" at the cost
of shipping a POSIX-emulation DLL — which is why the *experiment* is worth
running and the *native port* is not on jichi's road: the maintained answer
for Windows users is WSL, which is a full Linux, not an emulation.

## The class assignment

The survey is shipped as a graded curriculum extra:
[`docs/assignments/18-where-posix-ends.md`](assignments/18-where-posix-ends.md)
— produce the survey report with the classified findings table and the
precisely-named wall; the mechanical floor checks the report's structure,
and the judgment layer (how well the wall is argued) is the instructor's,
per the three-layer model. It requires access to a Windows machine (or VM)
with Cygwin or MSYS2 — the one curriculum task with a hardware prerequisite,
stated in the brief.

## Phase 2 — scheduled

A timeboxed **Cygwin port attempt by the project itself** is scheduled
after the first learner surveys arrive (their findings decide where the
timebox goes). Outcome documented either way: a working Cygwin build would
move [INSTALL.md](INSTALL.md)'s support matrix; a failure becomes this
page's definitive appendix.
