# Hardening review 8 — What became of the earlier findings

*Part of the hardening and platform review of jichi made on 2026-10-17 at commit
`e5d832a4`. Start with [the overview](2026-10-17-hardening-00-overview.md).*

---

**Who this chapter is for.** A maintainer who wants to know which earlier findings
still stand, and a self-learner or junior developer who wants to see why a project
should re-check its own records. If you want the new findings, read the area
chapters instead; this one is about the old ones.

**After reading it you will know:**

- how the earlier findings were re-checked, and why a "fixed" in a record was not
  taken on trust;
- which fixes were confirmed in the code, grouped by theme;
- which earlier findings are still open, and which chapter of this review now
  carries each one;
- which fixes changed shape — a fix that holds in one place and not in its
  sibling;
- which statements in the project's own records are no longer true;
- how to do this kind of re-check yourself.

**What you need first.** Nothing beyond the overview. The finding ids (`R04-F8`,
`def-150`, `M472` …) are the project's own: `R..-F..` are findings of the
2026-10-08 review, `def-NNN` are rows of `docs/DEFERRED.md`, `M...` are milestones
in `docs/ROADMAP.md`.

---

## 1. Why re-check what is already written down

A record that says "fixed" is a claim about the code, made on the day it was
written. The code keeps moving after that day. Three things can make the claim
false without anybody editing the record:

1. **The fix is undone or bypassed by a later change.** A later feature adds a
   second way to reach the same operation, and the second way does not pass the
   check (this review found that twice for the edit scope, chapter 3).
2. **The fix was carried to one caller and not to its siblings.** A bug class has
   five call sites; the fix touched the one in the bug report (section 5 has four
   cases).
3. **The record was wrong on the day it was written**, and nothing tested it.
   (Section 6: a hardening page says an optimised build carries no fortify checks;
   the optimised build of this commit carries ten.)

So every reviewer of this review, before judging anything new, took the earlier
findings of their area from the project's records and checked each one **in the
code at `e5d832a4`**: they read the line that holds the fix, or ran it.

## 2. How the re-check was made

Each of the six reviewers (chapters 1 to 7) read the earlier findings of their
area from these records:

- `docs/HARDENING.md` (M130–M134, M300, M693, and its recommendations);
- the external audit of 2026-08-17 and its implementation page;
- the security audit of 2026-10-04 and the gap analyses of 2026-10-05;
- the whole-tree review of 2026-10-08, its public chapters and its internal
  reports;
- the branch review and scaffolding findings of 2026-09-29;
- the portability analysis of 2026-10-15;
- the open rows of `docs/DEFERRED.md`.

They checked 258 rows in all:

| Area (chapter) | Earlier findings checked |
|---|---|
| Untrusted input and memory (2) | 33 |
| Processes and fences (3) | 25 |
| Network and secrets (4) | 51 |
| Files and persistence (5) | 75 |
| Build and platforms (1, 6) | 40 |
| Resources and small boards (7) | 34 |
| **Total rows** | **258** |

A row is not the same as a distinct finding: a few findings fall in two areas
and were checked twice (the JSON printer's number formatting appears in three
tables). That double check is useful — two reviewers reading the same line
independently — but it means the rows cannot be summed into "N findings".

**Why this chapter gives no "percent fixed".** A first attempt counted the
statuses with a keyword classifier. It misfiled rows: the row for the M2 git
work says the fix *holds* for three program-launching settings and *does not
cover* a fourth, and the classifier counted it as fixed. A number produced that
way would look precise and be wrong. The project's own rule fits here — a
classifier whose fallback branch produces a positive claim is not an instrument
(CLAUDE.md, "a classifier's else-branch must not be a finding"). So the sections
below are curated lists, read row by row.

## 3. What held — fixes confirmed in the code

These earlier fixes were found in place at `e5d832a4`, each at the line the
reviewer names in their report. They are grouped by the kind of protection, so a
learner can see what a mature fix set for an agent looks like.

**Secrets that could reach a child process**

- M130: API keys are removed from every child's environment (shell, git,
  background jobs, the verifier, MCP servers), through one spawn primitive.
- M608 and M724: jichi's own key names lead the scrub list, the list is armed
  before anything forks, and a list that cannot be built refuses the run instead
  of leaking.
- M770 (Linux): jichi's own `/proc` entries are closed to its children
  (`PR_SET_DUMPABLE`, read back to confirm it took).
- M771 (FreeBSD) and M796/M798/M799 (OpenBSD, NetBSD, illumos): the platform
  equivalents, partial by design on the last three, as recorded.

**Descriptors a child could inherit**

- M472: the journal, telemetry and the model connection are close-on-exec, with
  a close-everything backstop in the child.
- def-151, closed at M832: the `popen` path now goes through the same spawn
  primitive, so it gets the same backstop.
- M844: no child ever receives jichi's own standard input.

**The network**

- The 2026-08-17 audit's H1: a credential no longer follows a redirect; only the
  three callers that carry no header follow one (M472).
- A TLS floor and peer/host verification on every request (M472).
- The SSRF guard of `fetch_url` (M131) is present. Its reach has gaps, which are
  new findings (chapter 4).
- The multipart filename header injection (L2) is fixed.
- An error inside the model's stream no longer becomes the answer (M909).

**Parsing what arrives from outside**

- The JSON parser's recursion is bounded (M472); its number-to-int conversions
  are range-checked (M469, M866); a string buffer is sized to its own string, not
  to the rest of the input (M725, an 88% heap cut on large replies).
- SSE accepts a bare carriage return as a line end (M775).
- LSP `Content-Length` is checked for overflow and bounded; MCP and ACP peers have
  a line cap (M609, M659).
- Text cut for display never splits a UTF-8 character (M191, M439); terminal
  escape sequences in model text are neutralised (M472).
- Numeric command-line flags are parsed with range checks instead of `atoi`
  (M923); the test that failed on 32-bit x86 was fixed (M956). The whole unit
  suite also passes as a 32-bit build (chapter 1).

**The build and signals**

- The compiler and linker hardening flags exist and are probed one at a time
  (M472, M479, M833). Chapter 1 shows what they do and do not cover.
- Signal handlers are installed with `sigaction` semantics, not glibc's one-shot
  `signal()` (M918); children no longer inherit an ignored `SIGPIPE` (M461).
- With `HOME` unset, jichi no longer falls back to `/tmp` (the 2026-08-17 audit's
  M4).

**Files and state**

- The path resolver handles a dangling symlink leaf (M607), a write into a new
  directory (M638) and deep paths without recursion (M727).
- A FIFO among the session files no longer hangs the session store (M198).
- File reads no longer pile up on the session-long arena (M197, M199).
- `undo` prints its scope before it destroys anything (M537) — but see chapter 5:
  the scope it prints can be incomplete.
- The lease and the board survive concurrent writers (M760, M761, M533); progress
  write failures are reported (M642); DJGPP text-mode history is fixed (M837).
- `config set` keeps a JSONC file's comments (M924).
- Three of the scaffolding findings of 2026-09-29 are fixed (M847, M848, M849).

**Memory**

- The glibc allocator is tuned and trimmed at turn boundaries (M218); stream
  buffers shrink back after a large reply; a session is not re-serialised when it
  has not changed; it is streamed to disk instead of built as a whole tree (M232).
- The model connection is reused across calls (M227): one TCP connection carried
  all 51 requests of a measured run.
- The idle terminal UI makes no system calls at all (measured over 30 seconds).

**Fences and gates**

- A fence that cannot apply refuses the task instead of running unfenced (M852).
- A project's configuration is filtered through a trust table before it can
  redirect a key (M851).
- A boolean written by a model or a person is read leniently, so `1` cannot turn
  a fence off (M530); every gate uses the tool's canonical name (M532); the
  privileged-command and kinetic gates sit below the approval verdict
  (M153, M163a).

## 4. What is still open — and where this review carries it

Each of these was open before and is still open at `e5d832a4`. Security items are
described by class, as `SECURITY.md` promises; the mechanisms are in the project's
internal records.

| Earlier id | What is open (class) | Now in |
|---|---|---|
| R07-F9 | Out-of-range conversions of JSON numbers to `int`/`long`: undefined behaviour, now counted at 115 sites | ch. 2 |
| R07-F10 | The JSON printer loses precision, and on 32-bit prints large integers wrongly | ch. 2 |
| R07-F11 | The JSON parser accepts malformed numbers and trailing text | ch. 2 |
| R07-F14 | Streamed tool calls beyond 32 are dropped silently | ch. 2, 7 |
| R07-F15 | The SSE reader's overflow flag is set and never read | ch. 2, 7 |
| def-75 | The C89 fallback formatter mishandles width and precision | ch. 2 |
| R04-F3 | `edit_file`/`apply_patch` cut a file at its first NUL byte (re-measured) | ch. 5 |
| R04-F8 | A git argument from the model is not separated from git's options | ch. 3 |
| R04-F21 | The same class for the code-search tool's path (impact measured higher) | ch. 3, 5 |
| R06-3 | A command's timeout stops being enforced after the command closes its output | ch. 3 |
| R06-17 | The privileged-command detector misses commands behind some wrappers | ch. 3 |
| R09-F8 | A language server inherits jichi's API keys | ch. 3, 4 |
| def-149 | No sandbox around the top-level shell (only parallel write children are fenced) | ch. 3, 6 |
| def-150 | The path fence's check-then-open window (left by decision) | ch. 3, 5 |
| R05-F6 | A session is saved only at the end of a top-level turn | ch. 5, 7 |
| R05-F11, R06 #7 | `memory.md`, `constraints.md` and learn's rules files are rewritten non-atomically | ch. 5 |
| R06 #11 | Event-log and audit writes are not checked (silent on a full disk) | ch. 5, 7 |
| R06 #12 | The disk guard measures the workspace, but the records live under `HOME` | ch. 5, 7 |
| def-302 | `jc_mkdir_p` reports success when the last directory could not be created | ch. 5, 6 |
| R09-F4, R09-F5 | The index leaks per parallel batch; an index rewrite truncates a mapped file | ch. 5, 7 |
| R09-F12 | The index and repo-map walks follow symlink loops (impact now measured) | ch. 5, 7 |
| def-298 | Project commands, agents, skills and rules load without a trust step | ch. 5 |
| def-198 | Telemetry is never rotated | ch. 7 |
| def-30 | No key protection against same-user process inspection on macOS | ch. 4, 6 |
| Portability 5.1.2 | A proxy in the environment interacts badly with the SSRF guard | ch. 4 |
| Portability 5.1.3 | The random-number fallback is weak and silent | ch. 4 |
| Portability 5.2.2 | No 64-bit `off_t`/`time_t` on 32-bit builds | ch. 5, 6 |
| Portability 5.2.6 | No `FD_SETSIZE` guard at the `select()` sites | ch. 1, 6 |
| def-315 | `SIGTERM` does not stop a command on the `popen` path (reproduced) | ch. 6 |
| R03-F2 | One stalled daemon client wedges the listener | ch. 3 |

## 5. What changed shape

These are the instructive cases. In each one a fix is real, and the record that
says "fixed" is not wrong about the line it names — but the protection does not
reach as far as the record suggests.

1. **The git neutralisation (M2).** The spawn-time settings hold for the three
   program-launching settings they name, measured. A fourth class of repository
   setting that makes git run a program is not covered (chapter 3; the mechanism is
   in the internal record). One of the three is closed only by an unpushed commit.
2. **The edit scope (M133).** Enforced at every delegation depth, as recorded —
   but two independent argument shapes reach the file system without matching the
   scope (chapter 3).
3. **The Landlock shell fence (M831).** Present, and it refuses when it cannot
   apply (M852). It restricts *writes*, on *Linux*, for *parallel write children*;
   it does not restrict what a fenced child may execute.
4. **Private file modes (M132).** Sessions, telemetry, the audit log and the
   background spool are private. `~/.jichi.d` itself, the snapshot shadow
   repository and two kinds of worktree are created with the process umask
   (measured at 755 and 444; chapter 5).
5. **The repo map's memory (M140).** The lifetime of the file texts was fixed; the
   peak was not — every scanned file is held until the map is rendered (chapter 7).
6. **Quoted git paths (R04-F1, fixed at M927 for the parallel merge).** The
   edit-scope sweep reads the same git output without the fix (chapter 5).
7. **JSONC-preserving config edits (R07-F1, fixed at M924 for `config set`).**
   `setup`'s merge into an existing config does not use it (chapter 5).
8. **The regular-file check (M198).** Present at five of the walkers, missing at
   two (chapter 5).
9. **Control-character escaping of file names (M200).** Present in `list_files`,
   absent from the repo map and the index (chapter 5).
10. **Sessions and redaction.** `HARDENING.md` §4 recorded "sessions are not
    redacted, mode 0600 only"; since M802 a session *is* redacted when saved. The
    change is an improvement; the page is out of date.

The lesson for a junior developer is in items 6 to 9. **When you fix a bug, search
for its siblings before you close it**: the same call, the same pattern, the same
input reaching a second function. A fix carried to one caller of five is a record
that says "fixed" over four live copies of the bug.

## 6. Statements in the records that are no longer true

These were checked in the tree or measured. Each record should get a dated
correction that names what it supersedes (the workspace's rule for notes another
machine may already have acted on), not a silent in-place edit.

| Record | What it says | What is true at `e5d832a4` |
|---|---|---|
| `docs/HARDENING.md` §7b | An optimised build carries no `__*_chk` fortify symbols; the protection is "by construction" | An `OPT=-O2` build imports ten, among them the `FD_SET` bounds check at ten `select()` sites, `realpath` in the path resolver, and `strcat`/`strncat` (measured, chapter 1) |
| `CLAUDE.md`, conventions | `strcpy`, `strcat`, `gets` are banned like `sprintf` | `src/` has 10 `strcpy`, 4 `strcat`, 3 `strncat`; only `sprintf` is linted. The sites sampled are bounded by explicit checks (chapter 1) |
| Snapshot gap analysis (2026-10-05) | The undo preview and the destruction are "by construction the same universe" | They run the same command under different ignore rules; files can be deleted that the preview did not list (measured, chapter 5) |
| Telemetry gap analysis (2026-10-05) §1 | Every field goes through the scrubber | Three error fields bypass it (measured, chapter 4) |
| Security audit (2026-10-04), G1 | The `popen` descriptor gap is documented and open | Closed at M832 |
| i386 record (2026-10-15) | One Landlock test failure was a container artefact | Probably a real defect: a structure half-initialised on 32-bit reproduces that exact failure (chapter 6) |
| `include/jc_spawn.h:68-72` | git and the language server are never scrubbed | Stale for git since M2; still true for the language server |
| `src/main.c:18673` (comment) | Telemetry is off by default | On by default since M599 |
| `docs/BUILD.md`, `docs/PLATFORMS.md`, illumos notes | jichi has no OS-name conditionals | `include/jc_proc.h:272` has had one since M796 |
| `make info LTO=1` | `_FORTIFY_SOURCE` is live | Not at `-O0`; the line keys on the wrong variable (chapter 1) |
| `docs/FILE_HANDLING.md` §2 | jichi never calls `fsync` | Still true, and the premise is now contested (chapters 5 and 7) |
| `docs/DECISIONS.md` | — (no row) | No row records "a credentialed request never follows a redirect", a decision the code depends on |

## 7. How to do this yourself

A re-check like this one takes an afternoon per area and needs no special tool.

1. **Pick one record** — a `DEFERRED` table, an audit, a "known issues" section.
2. **For every row that says fixed, find the line.** Open the file the record
   names, at today's commit, and read the code that is supposed to hold the fix.
   If the record names no line, search for the function it names. Write down the
   path and line you found.
3. **If the fix has a test, run it — and break the fix to watch the test fail.**
   A test you never saw fail has not been shown to work (the project's
   `docs/TESTING_RUNBOOK.md` scripts this as "teeth").
4. **Search for siblings.** Take the pattern of the bug — the call, the missing
   check — and search the whole tree for it. Every hit that lacks the fix is a
   finding.
5. **For every row that says open, check it is still open.** Open rows go stale
   too, by being fixed quietly.
6. **Record what you found next to the old claim, dated.** Never edit the old
   claim silently: someone may already have acted on it.

If something goes wrong — the line moved and you cannot find the fix — say so in
the record ("not found at this commit") instead of guessing. "Not re-verified" is
an honest status; "fixed, probably" is not.

## Read further

- [The overview](2026-10-17-hardening-00-overview.md) — the new findings and the
  order to fix them in.
- `docs/analysis/2026-10-08-review-00-overview.md` — the earlier whole-tree
  review most of these rows come from.
- `docs/HARDENING.md` — the hardening record itself.
- `docs/TEST_INTEGRITY.md` — why a check must be seen to fail, and why a
  register must be audited against a second source.
