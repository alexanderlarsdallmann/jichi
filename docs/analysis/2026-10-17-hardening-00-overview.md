# Hardening review 0 — Start here: hardening jichi, and making it fit each platform

*The overview of a hardening and platform review of jichi, made on 2026-10-17 at commit
`e5d832a4` (origin/master that day), on the operator's request: "analyze the jichi source code, and
make recommendations for hardening the code base with possible optimizations for the various
platforms ... document your findings, with design decisions, their implications, and
recommendations, and explain it for self-learners, and junior developers, too." Written by Claude
Opus 5.5, who led the review, ran the measurements and re-checked the central claims; six reviewer
instances of the same model drafted the area chapters.*

---

## Who this is for, and how to use it

**For** a self-learner or junior developer who wants to see a real hardening review of a C program
— what it checks, how it decides what matters, and what it recommends — and for a maintainer who
wants the findings in the order to fix them. You need no prior knowledge of jichi; the chapters
define their terms as they go.

**After reading this page you will know:**

- how the review was made, and how far to trust each kind of statement in it;
- what jichi already does well against the threats an agent faces;
- the seven lessons the findings teach, each with the findings that show it;
- what to fix first, and why in that order;
- what each platform offers that jichi does not use yet, and what the small boards measured;
- which chapter to read next.

**What you need first.** Nothing. If you have never seen jichi, the earlier whole-tree review's
overview ([2026-10-08 review 0](2026-10-08-review-00-overview.md)) draws the program in one picture;
this review assumes only that jichi is an AI coding agent written in C89 that runs tools a language
model asks for.

**How to read a finding.** Every finding has an id (`A-01` … `F-10`; `L-nn` for the lead's own) and
names its evidence as `path:line` at `e5d832a4`. Its **severity** says what happens if it fires:

- **high** — data loss, a crash, a security boundary, or a fence that does not hold;
- **medium** — a wrong result or a misleading report in a plausible case;
- **low** — an edge case, an inconsistency, or a maintainability cost;
- **note** — worth knowing, not a defect.

Its **mark** says how it was established: **[V]** verified by reading the code path end to end;
**[M]** measured — something was run and the output is recorded; **[R]** read but not traced to the
end; **[GK]** general knowledge about a platform or library, not checked here.

**Security findings are described by class only** in these public pages — what kind of defect, how
severe, which file, the shape of the fix — as [`SECURITY.md`](../../SECURITY.md) promises reporters.
Their mechanisms and reproductions are kept with the project's internal records until they are
fixed.

---

## 1. How the review was made

1. **One commit, pinned.** Every line number refers to `e5d832a4`. The review ran in clones outside
   the project's working checkout, so the project's own session, which was working at the same time,
   was never disturbed. Two newer commits of that session (not yet pushed) are named where they bear.
2. **Six areas, one reviewer each.** Untrusted input and memory (A); processes, exec and the fences
   (B); the network and secrets (C); files and persistence (D); the build and the platform layer (E);
   resources on small boards (F). Each reviewer read its area in full, checked every earlier finding
   of the area against today's code (chapter 8), and ran experiments only in a private copy: offline,
   against the repository's own mock model, with no timeout around any measurement.
3. **The lead measured the binaries.** Eight build modes compiled and their unit suites run; the
   installed binary inspected; gcc's and clang's static analyzers over every file, with a count of
   where the analyzer gave up; a 32-bit build and its suite; and a benchmark on a LicheeRV Nano board
   (one RISC-V core, 229 MB of RAM).
4. **The lead re-ran the central claims.** Ten were re-run independently, in a scratch copy,
   before anything was written: the undo deletion (D-02), both edit-scope bypasses (B-04, A-01),
   the git setting that still runs a program (B-02), the symlinked state files (D-15), the relay
   overflow (D-14), the verifier that reads green (E-01), the fortify level (E-05), the JSON
   printer (A-09 with L-04) and the stale fortify claim in the hardening record (L-16). Two more
   were re-checked by reading only: the repository-map peak (F-01, whose measurement is the
   reviewer's) and one network finding that could not be reproduced offline — it is held back from
   these pages until a test exists.
5. **Excerpts and links were checked mechanically.** Every quoted code block that names its source
   line was compared with that source line by a script; every relative link was resolved.

**What this review did not cover.** The agent loop and context management, the terminal UI, most of
the utilities, the index/LSP/MCP layers as code (they appear here only where they read files or start
processes), and the documentation as documentation. Those are the six slices the 2026-10-08 review
left unfinished; finishing them is the second phase of this work.

## 2. In numbers

| Area | Chapter | Findings | high | medium | low | note |
|---|---|---|---|---|---|---|
| Build and binaries (with the lead's) | [1](2026-10-17-hardening-01-build-and-binaries.md) | 7 | 0 | 0 | 7 | 0 |
| Untrusted input and memory | [2](2026-10-17-hardening-02-input-and-memory.md) | 15 | 3 | 6 | 4 | 2 |
| Processes, exec and the fences | [3](2026-10-17-hardening-03-processes-and-fences.md) | 11 | 1 | 6 | 3 | 1 |
| The network and secrets | [4](2026-10-17-hardening-04-network-and-secrets.md) | 21 | 0 | 6 | 13 | 2 |
| Files and persistence | [5](2026-10-17-hardening-05-files-and-persistence.md) | 37 | 10 | 18 | 9 | 0 |
| Platforms and signals | [6](2026-10-17-hardening-06-platforms.md) | 9 | 1 | 1 | 5 | 2 |
| Resources and small boards | [7](2026-10-17-hardening-07-resources-and-small-boards.md) | 10 | 0 | 2 | 4 | 4 |
| **Total** | | **110** | **15** | **39** | **45** | **11** |
| Earlier findings re-checked | [8](2026-10-17-hardening-08-earlier-findings.md) | 258 rows | | | | |

Chapter 1 carries reviewer E's build findings (E-04 to E-07) and the lead's three (L-02, and L-05a and L-05b,
the two real defects the analyzers found); chapter 6 carries E-01 to E-03 and E-08 to E-13. A few findings are
reached from two areas and are counted once, in the chapter that owns the fix.

## 3. What jichi already does well

A review that lists only defects misleads, so this section comes first. Each item was checked in the
code.

1. **One door for every child process.** Since M832 every program jichi starts goes through one
   spawn function that removes the API keys from the child's environment, closes every descriptor
   jichi holds, and resets inherited signal state — and a test asserts the *result* on both spawn
   backends ("no descriptor above 3"), not the attempt (chapter 3).
2. **Hardening flags probed one at a time.** The Makefile asks the compiler whether it accepts each
   protection — the stack protector, stack-clash protection, control-flow protection, PIE, full RELRO
   — and uses only what it accepts, so a new platform cannot fail to build because of them (chapter 1).
   The binary the operator installed carries all of them.
3. **Fences fail closed, and a degraded fence says so.** The Landlock shell fence refuses a task it
   cannot confine; where the kernel lacks Landlock the run says so in three places (chapter 3).
4. **"Who wrote this input?" is a written rule, and the code follows it.** Booleans from a model, a
   person or a foreign program are read leniently, jichi's own logs strictly — a reviewer checked
   every strict read and found all of them on jichi's side (chapter 2).
5. **The JSON parser's depth bound is a model fix**: one counter at the only point of recursion, so
   no future edit can leak it (chapter 2).
6. **Memory lifetimes are designed, linted and visible.** Three arenas by lifetime, a lint that keeps
   the tools on the short ones, and `/context` prints each arena's use (chapter 7). The idle terminal
   UI makes no system calls at all.
7. **The whole unit suite is ready for a hardened release build.** 15,398 checks pass at `-O2`, with
   `_FORTIFY_SOURCE=3`, with zero-initialised automatic variables, at `SIZE=1`, with link-time
   optimisation, with clang, and as a 32-bit build (chapter 1). Only two lines of test code stand
   between today's gate and a gate that also builds the optimised binary.
8. **The records are honest and dense.** Decisions carry their rejected alternatives, fixes carry
   their measurements, and failures are kept as anecdotes. This review could check 258 earlier
   findings in a day because they named their lines.

## 4. Seven lessons, and the findings that teach them

These are the patterns behind most of the 110 findings. Each is worth carrying to any codebase.

### 4.1 A check must read exactly what the action will use

The project already has this rule for approval previews (M530: "a preview must read every argument
exactly as the executor will") and for gates (M532: decide on the canonical tool name). The review
found it broken in four more places:

- the **edit scope** reads a path as text, before the path is normalised (B-04), and reads the tool
  arguments before two argument shapes are normalised (A-01) — two independent ways past one fence
  (chapter 3);
- **`undo`'s preview** lists the files it will delete under today's ignore rules, then deletes under
  the restored checkpoint's ignore rules — and deletes files the preview never listed, permanently
  (D-02, chapter 5; measured);
- the **code-search tool's** path is checked by the fence and then read by the search program as an
  option (D-31, chapter 5).

**Lesson:** normalise untrusted input once, before any check, and hand the same bytes to the check
and to the action.

### 4.2 An unknown must not read as a pass

- If jichi is started by a process that ignores `SIGCHLD`, the operating system reaps the children
  itself, `waitpid` fails, and a status pre-set to zero is read as success: **a failing verifier
  reports GREEN** (E-01, chapter 6; measured).
- A session save that fails is ignored at every per-turn call site (F-02); a telemetry log past 64 MiB
  reads as "no telemetry" (F-04); a failed read of `memory.md` is taken as "no file" and the next
  write replaces it (D-16).

**Lesson:** when an instrument cannot tell, it must say *unknown*. The project's own rule — "a
classifier's else-branch must not be a finding" — applies to return codes as much as to classifiers.

### 4.3 A fix is finished only when its siblings are

- The JSON parser got a depth bound (M472); the YAML parser beside it did not, and a few kilobytes of
  YAML front matter in a cloned repository overflow the stack (A-04).
- Three conversions of a JSON number to an integer were clamped; about 115 more are not (A-02).
- Quoted git paths were fixed for the parallel merge, not for the edit-scope sweep (D-03); JSONC-safe
  editing was fixed for `config set`, not for `setup` (D-22); the key scrub covers every child except
  the language server (B-08).

**Lesson:** before closing a bug, search for its pattern across the tree. Chapter 8 lists every
"fixed" that turned out to be fixed in one place only.

### 4.4 A cloned repository is input

A repository can contain symbolic links, and git stores them. jichi's own state files under `.jichi/`
(D-15), the codebase index (D-27), the repository map (D-28, D-29), `list_files` (D-30) and the rules
and glossary loaders (D-34) follow a link the repository chose out of the workspace. A project command
can run a shell command without approval (D-32), and `setup` can read a project configuration without
the trust step (D-21). These are described by class in chapter 5; the fix shape is common — `lstat`
before you open, and check "is this inside the workspace" on the path you will actually open.

**Lesson:** "a path you found", "a path the user gave you" and "a path the repository chose" are three
different kinds of input, and only the first two have been treated as such.

### 4.5 Arithmetic that assumes 64 bits

- On a 32-bit board the diff renderer's table size overflows a `long`, and editing a file of about
  46,000 lines crashes the agent (A-03).
- The JSON printer prints every integer above 2³¹ as `-2147483648` on 32-bit, and loses precision on
  every platform: `50.5871234` is sent on as `50.5871` (A-09 with L-04).
- The Landlock rule set is half uninitialised on 32-bit Linux, so the shell fence is refused at
  random (E-02).

The whole unit suite passes as a 32-bit build (L-09) — so the suite has no test that would see any of
these, and no gate builds 32-bit. **Lesson:** a platform you support but never build in the gate is a
platform whose bugs you will hear about from a user.

### 4.6 Bound the peak, not only the lifetime

Arenas bound how *long* memory lives. They do not bound how much is alive at once:

- the repository map holds every scanned file until it renders: 138 MB of peak memory for a
  3.8 KB map in a measured case (F-01);
- a 4 MB JSON reply becomes about 160 MB of parse tree (A-06);
- a symbolic-link loop grew the repository map to 1.94 GB at start-up (D-29).

**Lesson:** after choosing a lifetime, ask what the peak inside that lifetime is, and measure RSS, not
only the heap.

### 4.7 The binary users run is not the binary the gate tests

- The default build is unoptimised, so `_FORTIFY_SOURCE` is inert; `make install` installs that build.
  The binary installed on the review machine carries zero fortify checks (L-01, E-05).
- An optimised build carries ten — among them a bounds check on every `FD_SET`, at ten `select()`
  sites that have no bounds check of their own. The hardening record says an optimised build carries
  none (L-16).
- No gate stage builds or tests an optimised binary (L-02). On the LicheeRV Nano, `-O2` halves jichi's
  own CPU time per turn (chapter 7).

**Lesson:** decide which build is the product, and make the gate test that one.

## 5. What to fix first

Ordered by harm, then by how little each fix costs. Each item names its findings; the chapters give the
smallest correct fix and the test that must fail before it.

1. **Data loss.** `undo`/`rewind` deleting newly ignored files (D-02); `edit_file` and `apply_patch`
   cutting a file at its first NUL byte (D-01); a failed in-place write destroying the file it replaced
   (D-04); `setup` replacing a commented configuration without a backup (D-22).
2. **The fences that do not hold.** The edit scope (B-04, A-01), with one normalisation step before the
   check; the code-search option (D-31); the git branch option (B-03).
3. **The verifier that reads green.** Reset `SIGCHLD` at start-up and treat any `waitpid` failure as
   *unknown* (E-01); handle `SIGHUP` (E-03); bound the wait after a command closes its output, so a
   timeout and Ctrl-C still work (B-01, B-06).
4. **Repository-controlled paths and project input.** One helper that refuses a symbolic link or a
   non-regular file for every `.jichi/` state path (D-15); `lstat`-based walkers (D-27 to D-30, D-34);
   the trust step for `setup` (D-21) and for project commands (D-32); the git setting class M2 has not
   covered yet (B-02).
5. **Memory safety.** The relay overflow (D-14); the YAML depth bound (A-04); the recovery parser's
   over-read (A-05); the 32-bit diff overflow (A-03); the 32-bit Landlock structure (E-02).
6. **Secrets.** Redact the error text telemetry writes (C-03); scrub the language server's environment
   (B-08, C-05); the held-back network finding, test first; the proxy interaction (C-02).
7. **A release build that is the tested build.** Fix the two test lines that stop the suite from
   building optimised; add an `-O2` stage and a `-m32` stage to the gate (L-02); keep the compiler's
   fortify level where it is higher (E-05); install the optimised build (L-01).
8. **Numbers and casts.** The JSON printer (A-09), the clamped integer reader and a lint for the bare
   cast (A-02).
9. **Resources.** The repository-map peak (F-01); report a failed session save (F-02); save the session
   inside long turns (D-08); the request printer's fast paths (F-06).
10. **Records and small items.** The stale statements chapter 8 lists; the low findings and notes.

## 6. Platforms

Chapter 6 ranks what each platform offers by value for effort. In short:

| Platform | The best next step |
|---|---|
| Every platform | Normalise inherited signal state at start-up (E-01), handle `SIGHUP` (E-03) |
| Linux (glibc, musl) | A parent-death signal for every child; `close_range` for the descriptor clean-up; then the Landlock fence for the top-level shell (the open half of def-149) |
| FreeBSD | Parent-death signal and `closefrom` through `procctl`; read the real CPU count |
| OpenBSD, NetBSD, illumos | `closefrom`; `pledge` or process privileges only if users ask — these rows are defect detectors |
| macOS | Measure whether the environment scrub hides the key from `ps`; use the spawn flag that closes every unnamed descriptor |
| Cygwin, MSYS2 | Measure the `posix_spawn` backend: if it avoids the emulated `fork`, it is the largest speed-up on any row |
| 32-bit Linux | Fix E-02, then the 64-bit size and time types (E-09); add the `-m32` gate stage |
| FreeDOS (DJGPP), FreeMiNT | A third, synchronous spawn backend (def-269) |
| Small boards | Build with `OPT=-O2` or `SIZE=1`, never the default; keep lite mode, which already switches off the repository map below 1 GB |

**Measured on the LicheeRV Nano** (riscv64 C906, one core at 1 GHz, 229 MB, Debian, gcc 13.2), the
same mock turn five times per build (chapter 7 has the full table):

| Build | Build time | Binary | jichi's CPU per turn | Peak RSS |
|---|---|---|---|---|
| default (`-O0`) | 221 s | 2,177,896 B | 0.40–0.44 s | 12.4–12.8 MB |
| `OPT=-O2` | 731 s | 1,755,216 B | 0.20–0.23 s | 12.2–12.5 MB |
| `SIZE=1` | 597 s | 1,494,480 B | 0.25–0.29 s | 12.1–12.3 MB |

## 7. The chapters

1. [Build and binaries](2026-10-17-hardening-01-build-and-binaries.md) — what each compiler and
   linker protection does and how to check it; why fortify is off by default and what that costs; the
   gate that never builds the product; what the static analyzers reach.
2. [Untrusted input and memory](2026-10-17-hardening-02-input-and-memory.md) — framing, parsing,
   interpretation; recursion depth; why a `double`-to-`int` cast is undefined; numbers on the wire.
3. [Processes, exec and the fences](2026-10-17-hardening-03-processes-and-fences.md) — the one spawn
   door, the gates in order, and the fences that do not hold.
4. [The network and secrets](2026-10-17-hardening-04-network-and-secrets.md) — redirects, the private
   address check, the life of an API key, and every sink it might reach.
5. [Files and persistence](2026-10-17-hardening-05-files-and-persistence.md) — in-place versus atomic
   writes, `fsync`, undo as a shadow git repository, and the paths a repository chooses.
6. [Platforms and signals](2026-10-17-hardening-06-platforms.md) — every platform conditional in the
   tree, what a program inherits from its parent, 32-bit, and the ranking.
7. [Resources and small boards](2026-10-17-hardening-07-resources-and-small-boards.md) — where
   memory, CPU and disk go, with measurements from a workstation and a 256 MB board.
8. [What became of the earlier findings](2026-10-17-hardening-08-earlier-findings.md) — 258 earlier
   findings checked against today's code, and the records that are no longer true.

## Read further

- [The 2026-10-08 whole-tree review](2026-10-08-review-00-overview.md) — the review this one extends.
- [`docs/HARDENING.md`](../HARDENING.md) — the project's hardening record, M130 onwards.
- [`docs/TEST_INTEGRITY.md`](../TEST_INTEGRITY.md) — why a check must be seen to fail.
- [`docs/PLATFORMS.md`](../PLATFORMS.md) and [`docs/LOW_MEMORY.md`](../LOW_MEMORY.md) — the platform
  matrix and the small-machine numbers this review adds to.
- [`SECURITY.md`](../../SECURITY.md) — what the project promises reporters, and why the security
  findings here are described by class.
