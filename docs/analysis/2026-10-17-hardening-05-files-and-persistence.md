# Hardening review 5 — Files, paths and what jichi keeps on disk

*Part of the 2026-10-17 hardening and platform review of jichi; start with [the overview](2026-10-17-hardening-00-overview.md).
Drafted by reviewer D (Claude Opus 5.5), edited by the review's lead. Every `path:line` refers to commit
`e5d832a4`. Evidence marks: **[V]** verified by reading the code path end to end, **[M]** measured (the
run is described), **[R]** read but not traced to the end, **[GK]** general knowledge, not checked here.
Severities use the 2026-10-08 scale: **high** = data loss, a crash, a security boundary or a fence that
does not hold; **medium** = a wrong result or a misleading report in a plausible case; **low** = an edge
case or a maintainability cost; **note** = worth knowing.*

**Who this chapter is for.** A self-learner or junior developer who reads some C, has never seen
jichi's source, and wants to know how a program that lets a language model edit files should read,
write and remember things on disk -- and where this one gets it wrong.

**What you will know after reading it.**
- The two ways jichi writes a file, why it keeps both, and what each loses on a full disk.
- How jichi's undo works (a hidden second git repository), and the one sequence in it that deletes
  files nobody asked it to delete.
- Why a symlink, a FIFO or a NUL byte in somebody else's repository is a design input, not an edge case.
- The ten high findings in this area, what fixes each, and which test proves the fix.

**What you need first.** C strings (what a NUL terminator is), the idea of a file descriptor, and basic
git (`add`, `commit`, `reset`, `.gitignore`).

---

## 1. The area in one page

jichi touches files in three roles.

**The model's file tools.** `read_file`, `write_file`, `edit_file` and `apply_patch` read and change the
user's project, the **workspace**. All of them pass through two **chokepoints** -- single functions every
call must go through -- in `src/chat/jc_app.c`: `jc_app_read_file` (`:574`) and `jc_app_write_file`
(`:590`). Each first asks the **path fence**, jichi's name for the check that keeps an autonomous run
inside the workspace (`jc_app_path_denied_ex`, `:334`). The fence canonicalises the path with
`realpath()` -- symlinks resolved, `..` removed (`src/util/jc_path.c:183`) -- and compares it with the
workspace root.

**jichi's private state.** Sessions (saved conversations), tool-output spills, the trust store and the
workspace lease live under `~/.jichi.d/`, outside every workspace on purpose, so an undo cannot revert
them.

**The shadow repository.** Before the first tool call in a turn that can change something, jichi takes a
**checkpoint**: `git add -A` and `git commit` in a separate git repository under
`~/.jichi.d/checkpoints/<key>/` whose work tree *is* the workspace (`src/snapshot/jc_snapshot.c`;
`src/chat/jc_agent.c:4098-4105`). `/undo`, `rewind`, the command-line `undo` and the autonomy envelope's
rollback all restore from it. The user's own `.git` is never touched.

Five terms used below:

- **Atomic replace.** Write a temporary file in the same directory, then `rename()` it over the old name.
  A reader sees the old file or the new one, never half of one.
- **Durability, `fsync`.** A returned `rename` does not mean the bytes are on the disk; they may still be
  in the page cache. `fsync` on the file forces its data out, `fsync` on the directory forces the new
  name out. Atomic is not durable.
- **TOCTOU** (time of check to time of use). A check and the action it guards are two system calls; if
  the file system changes between them, the check answered a question about a file that is gone.
- **umask.** A per-process mask the kernel applies to a new file's mode. Ask for 0666 under umask 022 and
  you get 0644. Relying on it means relying on whoever started the process.
- **FIFO.** A named pipe. Opening one for reading blocks until another process opens it for writing --
  forever, if nobody does.

---

## 2. Design decisions, and what each one teaches

### 2.1 Two write functions: in place for the user's files, atomic for jichi's own

**The decision.** `jc_write_file` writes the user's files in place; `jc_write_file_atomic` replaces
jichi's private files with temp-plus-rename (`include/jc_platform.h:139-147`; `docs/FILE_HANDLING.md` §2).

**Why.** An atomic replace creates a new file (a new inode) under the old name: it breaks hard links,
turns a symlink into a regular file and resets mode and owner. Right for jichi's state, wrong for a
user's file (ROADMAP M146, citing the M141 rationale).

**What it costs.** The in-place writer, whole (comments marked `[review]` are this review's, not the source's):

```c
/* src/platform/jc_platform_posix.c:340-353 */
jc_status jc_write_file(const char *path, const char *data, jc_size len)
{
    FILE *f;
    size_t put;
    f = fopen(path, "wb");          /* [review] the old contents are gone from here on */
    if (f == NULL) {
        return JC_ERR_IO;
    }
    put = fwrite(data, 1, len, f);
    if (fclose(f) != 0 || put != len) {
        return JC_ERR_IO;           /* [review] the error is seen here, after the loss */
    }
    return JC_OK;
}
```

`fopen(path, "wb")` truncates the file the moment it returns. A full disk, a quota, a kill or a power
cut before `fclose` leaves a short file. Measured [M]: with a file-size limit forcing the write to fail
(the same code path as a full disk), writing 16 KB over a 12,000-byte file through `jc_write_file` left
8,192 bytes of new data and no original; `jc_write_file_atomic` under the same limit returned the same
error and left the original 12,000 bytes untouched (D-04).

**The lesson.** "Keep the inode" and "never lose the contents" are both achievable: resolve the symlink,
create the temporary file in the *target's* directory, copy the original's mode onto it, write, `fsync`,
rename -- and fall back to in-place only for a file with more than one hard link. Name both requirements
before choosing between them.

### 2.2 No `fsync` anywhere

**The decision.** The source has no `fsync` or `fdatasync` call [M: a search of `src/` and `include/`
finds none]. FILE_HANDLING §2 says so and gives the reason: sessions, telemetry and calibration are
"recreatable state on a developer's workstation", and `fsync` has a cost.

**What it implies.** On ext4 with its default `auto_da_alloc`, a rename that replaces a file forces the
data out first, so a power cut usually leaves the old or the new version [GK]; without that heuristic
(FAT-formatted SD cards, some other filesystems) a zero-length file is possible [GK]. The cost is small
here: replacing a 200 KB file took 0.32 ms without and 2.00 ms with `fsync` of file and directory,
averaged over 50 runs on this review's NVMe/ext4 workstation [M]; it has not been measured on the
project's SD-card boards. And the premise has drifted: the session loader loads a newer jichi's session
anyway because "the conversation is the user's and is not regenerable" (`src/session/jc_session.c:541-543`).

**The lesson.** When two comments in one codebase disagree about whether the same data is precious, the
cheaper answer usually won by default rather than by decision. One durable save per *turn* for the
session, not an `fsync` on every write, is the proportionate fix.

### 2.3 Undo is a shadow git repository

**The decision.** Checkpoints are commits in a hidden repository whose work tree is the workspace
(`docs/SNAPSHOTS.md`). It reuses git's machinery, never touches the user's `.git`, and works in a
directory that is not a git repository at all.

**What it implies.** jichi inherits git's semantics: `.gitignore` decides what is in the safety net,
`git clean` decides what is deleted, git's quoting decides how names come back, git's umask-derived modes
decide who else can read the copies. SNAPSHOTS.md documents the first ("anything your `.gitignore`
excludes is never captured"). Three findings are each one of the *other* inherited behaviours: D-02
(`clean` and a changed `.gitignore`), D-03 (quoted names), D-06 (file modes).

**The lesson.** When you build on a tool, write down which of its behaviours you now own. "It's git" is
not a specification of what `/undo` deletes.

### 2.4 The fence checks, then opens -- a window left open on purpose

**The decision.** The fence resolves, decides and returns; the open comes afterwards, so a path component
swapped for a symlink in between is a TOCTOU window. It is deferred (DEFERRED def-150, from the M472
audit): the only process that can win the race runs as the same user -- in practice the model's own
shell, which could write the file directly anyway. DECISIONS M607 records the rejected alternative,
`O_NOFOLLOW` at the open, as covering writes only.

**Status today.** The deferral's "revisit when a sandbox lands" has partly come true: the M831 Landlock
fence confines a parallel write child's *whole process* (`include/jc_landlock.h:9-12`), so there the
kernel enforces the write side regardless of the window [V by reading]. On Linux 5.6+, `openat2()` with
`RESOLVE_BENEATH` resolves a path relative to a directory descriptor and refuses to leave it; on this
review's workstation (kernel 6.8) an escaping symlink and an escaping `../` both failed with `EXDEV`
while a file inside opened [M, a small program calling the system call directly].

**The lesson.** A deferral with a written "revisit when" is only as good as someone re-reading it when
the condition changes. This one was written well and now needs re-reading.

### 2.5 "A path you found" versus "a path you were given" -- and a third kind

**The decision.** M198 made every caller that reads a path it *discovered by scanning* check first that
it is a regular file, since a FIFO with no writer blocks forever; a path the user *named* (`--config`,
`read_file`, `@path`) is left alone because the user may mean a pipe (the comment on
`jc_is_regular_file` in `include/jc_platform.h`; FILE_HANDLING §1).

**What it implies.** A third kind of path is not named by the rule: one the *repository* chose.
`AGENTS.md`, `.jichi/memory.md`, `.jichi/glossary.md`, a project's command files and anything a
directory walk reaches through a symlink are named by whoever wrote the repository. That kind got
neither treatment, and most of this chapter's security-class findings (D-15, D-27..D-36) live there.

**The lesson.** Classify a path by *who chose it*, not by how your code came across it. For a program
that opens strangers' repositories, "the repository" is its own trust level.

---

## 3. The lead finding: a preview and an action that read different rules

### D-02. `undo`, `rewind` and the rollback delete files that are ignored *now*, when the target checkpoint's `.gitignore` did not ignore them

- **Severity:** high (permanent loss of files jichi never captured; the preview and the preservation
  message both say otherwise).
- **Evidence:** [M] twice -- by this reviewer and independently by the review's lead (measurement L-10),
  both with the real binary, the repository's mock model and a scratch workspace; [V] for the code.
- **Status:** new.

**The mechanism.** `undo --dry-run` lists "Untracked files that would be removed". The list comes from
`git clean -nd` -- the dry-run form of the command the undo will run -- against the workspace *as it is
now* (`src/snapshot/jc_snapshot.c:1799`; the one-line scope summary uses the same call at `:1864`). The
undo is this function (comments marked `[review]` are this review's):

```c
/* src/snapshot/jc_snapshot.c:769-780 */
static jc_status restore_to(struct jc_snapshot_mgr *m, const char *commit)
{
    preserve_before_destroy(m, "undo");      /* [review] git add -A, commit, pin a ref */
    if (git_run(m, "reset", "--hard", commit, NULL, NULL, NULL) != 0) {
        jc_logf(JC_LOG_WARN, "snapshots: reset failed");
        return JC_ERR_IO;
    }
    git_run(m, "clean", "-fd", "-q", NULL, NULL, NULL);   /* [review] exit status not read */
    return JC_OK;
}
```

1. `preserve_before_destroy` commits the current state with `git add -A`, which skips everything the
   *current* `.gitignore` ignores.
2. `reset --hard <commit>` restores the checkpoint's files -- possibly an *older* `.gitignore`.
3. `clean -fd` deletes every untracked file that is not ignored, judged by the `.gitignore` the reset
   just put back.

A file ignored now but not at the checkpoint is in no commit (step 1) and is deleted (step 3). The
preview ran before step 2, under the current rules, and never listed it.

**What was measured.** Scratch workspace, snapshots on. A headless `--auto` run wrote one file
(checkpoint 1). Outside jichi, a directory `dataset/` was created with one file in it, and a
`.gitignore` containing `dataset/` was added. A second run wrote another file (checkpoint 2). Then
`undo 2 --dry-run` printed "Untracked files that would be removed: (none)", and `undo 2` printed
"reverted to checkpoint 2" and "the discarded state is preserved at <commit>". Afterwards `dataset/x.bin`
was gone, and `git ls-tree -r` of the preserved commit listed `.gitignore`, the two written files and the
original file -- no dataset. The lead's re-run matched with a different commit id.

**How you would hit it.** A model adds `venv/` or `data/` to `.gitignore` in one turn and a later `/undo N`
or `/rewind` goes back past that turn; or an `--auto` run edits `.gitignore` and is rolled back.

**Why it is the lead finding for learners.** The undo machinery is careful: it previews (M337), preserves
before destroying (M337b), and reports how much it destroyed (M537, after ANECDOTES #66). Each mechanism
is correct *for the rules in force when it runs*. The defect is that the action changes the rules
half-way through. The 2026-10-05 snapshot gap analysis called preview and destruction "by construction,
the same universe" because they are the same command; they are the same command with different inputs.
SNAPSHOTS.md's "Ignored files (per `.gitignore`) are left alone by `clean`" (`docs/SNAPSHOTS.md:122-123`)
is true of `clean` and false of the sequence.

**Recommendation.** Run the clean *before* the reset and check its exit status: `clean -fd`, then
`reset --hard <commit>`. The clean then reads the same rules as the preview. A file ignored now is never
touched; one the restored `.gitignore` no longer ignores survives as an untracked file -- the documented
promise. With preservation on, step 1 has already staged everything not ignored, so the early clean
deletes nothing that is not also preserved.

**The test that goes red first.** A smoke driver running the measured sequence, asserting that
`dataset/x.bin` survives `undo 2` and that the dry run's list equals what the undo removed. Red on
`e5d832a4`. **Effort:** S.

**The lesson to carry anywhere.** A preview is honest only if it is computed from the same inputs as the
action. When an action changes its own configuration part-way through -- a migration that rewrites the
schema it validates against, a deploy that replaces the config it reads, an undo that restores the
ignore file it obeys -- ask which version each step reads.

---

## 4. The other high findings

### D-01. `edit_file` and `apply_patch` cut a file at its first NUL byte and report success

- **Severity:** high on this review's scale (silent loss of content); the 2026-10-08 review said medium.
- **Evidence:** [M] re-measured on `e5d832a4`; [V].
- **Status:** already recorded -- 2026-10-08 review, chapter 04, F3; still open.

`jc_read_file` returns a buffer and its length; the buffer is NUL-terminated for convenience. `edit_file`
passes it to the matcher as a C string (`src/tools/jc_tool_edit.c:71-83`; `jc_patch_apply`,
`src/util/jc_patch.c:304`, uses `strstr`/`strlen`), and `apply_patch` copies it with a `strlen`-based
append (`src/tools/jc_tool_apply_patch.c:130`). Nothing after the first NUL is seen, and the result is
written back. Measured: a 42-byte file `line1 abc\nline2\0BINARY-TAIL\nline3 keep me\n`, read and edited
(`abc` to `XYZ`) by the mock model: the tool answered "Edited f.txt (1 replacement)" and the file became
15 bytes. `read_file` had shown two lines with no hint of more (`src/tools/jc_tool_read.c:127-133`).
`write_file` mirrors it: JSON's `\u0000` decodes to a NUL (`src/json/cJSON.c:220-251`) and the tool
writes `strlen(content)` bytes (`jc_tool_write.c:79`). UTF-16 text and binary fixtures carry NULs.

**Fix:** refuse to edit a file whose length differs from `strlen` of its buffer ("contains NUL bytes;
edit it with a script"); mark the offset in `read_file`; refuse a `content` that decodes to a NUL.
**Test:** the 42-byte fixture, asserting an error and an unchanged file. **Effort:** S.

### D-22. `jichi setup` over a commented project config replaces it, keeps no backup, and marks the result trusted

- **Severity:** high (loss of a hand-written config, and with it the project's own fences).
- **Evidence:** [M]. **Status:** new; the class of the 2026-10-08 review's R07-F1, which M924 fixed only
  for `config set`.

jichi configs may carry comments (JSONC), which the loader strips (`src/config/jc_config.c:403`).
`setup`'s merge parses strictly (`src/setup/jc_setup.c:577`), fails on a comment, falls back to a fresh
config (`src/main.c:6843-6853`), writes it over the user's file (`:6882`), and -- the result was not
"merged" -- adds its hash to the trust store (`:6893-6900`). Measured: a commented project config with
`pathFence: true`, a privileged-command deny and a test command held only generated keys after one
`jichi setup` with a preset and a model, without `--force`; no `.bak`. **Fix:** strip comments before the
merge (the M924 rule), refuse to replace an unparsable config without `--force`, keep a backup.
**Test:** merge into JSONC text; both original keys survive. **Effort:** S.

### Security-class high findings (described by class until fixed)

`SECURITY.md` promises reporters that a security defect is described publicly by class only until it is
fixed. These seven are given as class, affected file, status, fix shape and test kind; mechanisms and
reproductions are in the project's internal records.

| ID | Class | Affected | Status, marks | Fix shape and red-first test | Effort |
|---|---|---|---|---|---|
| D-14 | A text rewrite grows a string in place past its buffer (heap overflow from model-influenced text); opt-in relay feature only | `src/util/jc_relay.c` | new; [M] by reviewer and lead (L-11), [V] | rebuild into a fresh buffer and keep its length in step; a sanitizer-build unit test with many occurrences | S |
| D-15 | jichi's own `.jichi/` state files are read and written through symlinks, so a repository can point them outside the workspace; one writer also skips fence and approval | `src/chat/jc_memory.c`, `src/tools/jc_tool_plan.c` | new; [M] by reviewer and lead (L-12), [V] | one helper for every `.jichi` path (`lstat`, regular file, under the root); smoke test with outside markers | S-M |
| D-21 | A setup path uses the project's config without the project-config trust step, for a request carrying the user's credential; its replies are written without a link check | `src/main.c` (`setup --onboard`, `--advisor`) | new; [M], [V] | load the defaulted project config through the trust-gated loader; smoke test that no request carries the key | S-M |
| D-27 | A repository walker follows symlinks out of the workspace, and what it reads goes to the embeddings service | `src/index/jc_index.c` (`walk`) | new; [M], [V] | `lstat` each entry, keep a link only if it resolves under the root, visited set; unit test with an outside marker | S |
| D-29 | A symlink loop in a repository makes a start-up walk allocate without bound | `src/index/jc_repomap.c` | recorded as a loop (2026-10-08, R09 F12), measured wider; [M] | never descend into linked directories, entry and depth bounds; smoke test that the run finishes | S |
| D-31 | A tool argument reaches an external program where it can be read as an option, while the fence judges it as a path | `src/tools/jc_tool_search.c` | recorded (2026-10-08 ch. 04, F21, rated low), measured higher; [M], [V] | `--` before the path, refuse a leading `-`; tool test | S |
| D-32 | A repository's command definitions can run a shell command and read outside files without approval, and silently replace the user's command of the same name | `src/command/jc_command.c` | recorded as a class (def-298), whose "instructions, not permissions" premise this contradicts; [M], [V] | no shell expansion or outside reads until the project is trusted; warn on shadowing; smoke test | M |

---

## 5. Medium findings

**D-04. A failed write to a user's file destroys it, and `edit_file`/`write_file` do not put it back.**
[M] for the primitive (section 2.1), [V] for the callers; partly recorded (the in-place design is a
stated decision; its effect on the model's tools is not stated anywhere). `edit_file` answers "could not
write" and stops (`src/tools/jc_tool_edit.c:110-132`); `write_file` too (`jc_tool_write.c:80-84`).
`apply_patch` already restores originals and reports each file's state (M138,
`jc_tool_apply_patch.c:269-330`) -- the pattern to copy. **Fix:** restore the original on a non-fence
failure; later, the mode-preserving replace of section 2.1 and a free-space check. **Test:** a
fault-injection site in `jc_write_file` (the atomic writer has `JC_FAULT_WRITE`) that writes half and
fails; the original must survive. **Effort:** S / M.

**D-08. A session is written only at the end of a top-level turn, never with `fsync`.** [V]; recorded
(2026-10-08 ch. 05, F6), still open: no `jc_session_save` in `src/chat/jc_agent.c`. A headless `--auto`
run is one turn of possibly hundreds of tool calls; a kill at call 150 leaves nothing to `--continue`. A
crash between temp and rename leaves `<id>.json.tmp<pid>` in the store forever. **Fix:** save at
tool-round boundaries, throttled by the existing "needs save" check; an opt-in durable mode for the
atomic writer used once per turn; sweep temps of dead processes. **Test:** kill a mock run after N
rounds; the session holds N rounds. **Effort:** M.

**D-16. An unreadable `memory.md` is treated as missing, and the next `remember` replaces it.** [M]: a
`memory.md` over the 64 MiB read limit became an 11-byte file after one `remember`, silently. New.
`jc_read_file` reports every open failure as "not found" (`src/platform/jc_platform_posix.c:296-299`);
`jc_memory_add` reads that as "new file" (`src/chat/jc_memory.c:251-253`); `learn apply` likewise
(`src/util/jc_learn.c:757-759`). The board already refuses to overwrite what it could not read (M533,
`src/chat/jc_board.c:299-303`). **Fix:** proceed only on a genuine "does not exist". **Effort:** S.

**D-17. The workspace lease can lose its own record.** [M], [V]; new. The lease is the advisory "who is
working in this tree" record (`include/jc_lease.h`). A second run that warns and proceeds writes its
record *over* the first's (`src/util/jc_lease.c:277`); on exit its release finds its own record and
removes it (`:305-314`). The first run is still working with no record, and a third started with
`--lease fail` is let in. **Fix:** one record per holder; release removes only its own. **Effort:** M.

**D-18. jichi's files inside the workspace are rewritten in place.** [M]; recorded (2026-10-08 ch. 05,
F11, and the utilities chapter), still open: `.jichi/memory.md` (`jc_memory.c:273, 381`),
`.jichi/PLAN.md` (`jc_tool_plan.c:90`), the rules files `learn apply` edits (`jc_learn.c:737, 791, 886`).
Same mechanism as D-04. **Fix:** the mode-preserving replace, plus the board's lock for `memory.md`.
**Effort:** M.

**D-25. `config set` moves the live config away before its replacement exists.** [M]; new.
`jc_config_edit_save` (`src/config/jc_configedit.c:248-277`) renames the config to `.bak`, writes a
temporary file, then renames it into place. If the write fails, the path holds no config: measured, the
next `config show` said "No model is configured" and the project's fence settings stopped applying. The
new file also takes the umask's mode (a 0600 config with a key became 0644), and a symlinked config is
replaced by a regular file. A security-class half (the temporary file's fixed name) is kept internal.
**Fix:** temp first (exclusive create, original mode, `fsync`), backup by `link()` or copy, rename, and on
failure remove the temp and leave the config untouched. **Test:** block the temp path with a directory;
save must fail and the config stay byte-identical. **Effort:** S.

| ID | Class / summary | Affected | Marks, status | Fix shape | Effort |
|---|---|---|---|---|---|
| D-03 | The edit-scope sweep reads git's *quoted* file names, so unusual names are misjudged: an in-scope file counted as a violation, an out-of-scope one not reverted, and the journal recording the revert as done | `jc_snapshot.c:1913`, `jc_agent.c:1219-1240` | [M]; new -- M927 moved the merge to NUL-separated output, not this sweep | `-z` output, as the merge uses | S |
| D-06 | `~/.jichi.d`, the shadow repository, the index cache and some worktrees take umask-derived modes; copies of workspace content can be readable by other users when the home directory is traversable | `jc_platform_posix.c:265,271`, `jc_snapshot.c:534`, `jc_index.c:475` | [M] (755/444/664 measured); new; contradicts SECURITY.md's "private state" | create `~/.jichi.d` owner-only at start-up; `doctor` warns | S |
| D-09 | The workspace key is a non-cryptographic hash of the path; two workspaces can share one shadow repository, so `undo` in one would restore the other's tree | `jc_snapshot.c:37-58` | [M] (the dry run in a colliding workspace offered the other's tree); new | cryptographic hash; record the work tree and check it before restoring | M |
| D-12 | The parallel merge follows symlinks at both ends; a link becomes a copy of its target, a link to a pipe can stall the lead | `jc_tool_parallel.c:1000-1043` | [V], [M] for the stall; partly recorded (2026-10-08 ch. 04, F20) | `lstat`; re-create links as links; refuse other types | S-M |
| D-23 | `init`/`setup` check "exists" with `stat` and write with `fopen`, so a planted symlink redirects pack files outside the project | `src/main.c` (scaffold writers) | [M]; new | `lstat`; refuse targets outside; exclusive create | S-M |
| D-24 | A setup option copies a credential from the private global config into the project config, at a wider mode | `src/setup/jc_setup.c:648-656` | [M]; new | drop literal keys when inheriting, warn | S |
| D-28, D-30 | The repo map and `list_files` with a pattern follow symlinks out of the workspace and show outside paths | `jc_repomap.c:665-672`, `jc_tool_ls.c:137-142` | [M]; new | D-27's walker rule | S |
| D-33, D-36 | A FIFO in a repository can block start-up (rules, glossary) or the search tool indefinitely | `jc_rules.c:46`, `jc_glossary.c:21`, `jc_tool_search.c:252-266` | [M]; new (M198 applied at five other callers) | non-blocking open, `fstat`, regular file only | S |
| D-34 | Rules and glossary symlinks pull outside files into the system prompt | `jc_rules.c:84-96`, `jc_glossary.c:45` | [M]; partly recorded (def-298) | resolved target must be under the root | S |
| D-35 | Prompt-asset files are read whole (up to 64 MiB) onto session-lifetime memory before being cut to a small cap, so repository files can exhaust a small board's memory | `jc_rules.c:46`, `jc_glossary.c:21`, `jc_command.c:151` | [M]; new | read at most cap + 1 bytes into a short-lived buffer | S |

---

## 6. Low findings and notes

| ID | Sev. | Summary | Evidence | Fix |
|---|---|---|---|---|
| D-05 | low | `read_file` returns `/proc` files as empty, with success: the size comes from seeking to the end, which procfs reports as 0 (`/proc/cpuinfo`: 0 bytes read, 38,035 present) | [M], `jc_platform_posix.c:306-317` | read in a bounded loop when the size is 0; check `ferror` after `fread` (`:330-337`) |
| D-07 | low | the editor protocol's `session/load` puts the client's session id into a path without the check the command line uses | [V], `src/acp/jc_acp.c:916-926` | validate inside `jc_session_load_by_id` |
| D-10 | low | 32-bit builds lack large-file support: a file over 2 GiB "could not open" (`-m32`: `stat`, `fopen` fail with EOVERFLOW); a `readdir` error ends a listing silently | [M][V], `jc_platform_posix.c:310, 681-697` | `-D_FILE_OFFSET_BITS=64` behind a probe; check `errno` |
| D-11 | low | the atomic writer's `<path>.tmp<pid>` can collide across containers sharing a home | [V], `jc_platform_posix.c:373-379` | add randomness to the name |
| D-13 | low | the non-git file count recurses with a 2 KB frame per level; deep trees overflow fixed stacks (FreeMiNT) | [V], `jc_snapshot.c:293-330` | a loop, as at M727 |
| D-19 | low | a FIFO at `.jichi/memory.md` (or `board.json`, `PLAN.md`, `progress.jsonl`) blocks start-up | [M] | D-15's helper |
| D-20 | low/note | state files: all board save errors reported as "unreadable board"; unlocked board edits without a word where no lock exists (NFS); unchecked double-to-`int` casts (board id, lease pid); a torn progress line swallows the next record; the input-history trim can drop another window's line; long notes cut silently; `*.tmp<pid>` leftovers in `.jichi/` | [V]/[R], `jc_board.c:351-379`, `jc_lease.c:123,152`, `jc_progress.c:167-182`, `jc_inhist.c:196-234` | each S |
| D-26 | low/note | scaffold/config: a failed scaffold write leaves a truncated asset the next `init` skips; project directories 0755 while files follow the umask; no `fsync` on config writes; silent truncation of very long paths | [V]/[M], `src/main.c:6255-6259`, `jc_configedit.c:241-244` | each S |
| D-37 | low/note | readers: the repo map prints file names without escaping control characters; model-supplied numbers cast to `int` without range checks in the search tools; the index embeds dot-files such as `.env`; the repo map's budget is spent alphabetically; command names compare case-sensitively | [M]/[V], `jc_repomap.c:731`, `jc_tool_codebase_search.c:39` | escape, clamp, skip dot-files |

---

## 7. Platforms

- **Linux.** `openat2(RESOLVE_BENEATH)` closes the fence's window and bounds the walkers (section 2.4)
  [M]. `O_NOFOLLOW`, `O_DIRECTORY` and `O_CLOEXEC` exist but are hidden under the tree's
  `-D_POSIX_C_SOURCE=200112L` on glibc 2.39 -- a test program printed 0/0/0, and 1/1/1 with `200809L`
  [M] -- so a fix needs a per-file feature macro, as `src/util/jc_path.c:12-14` already sets for
  `realpath`. GNU grep 3.11 blocks on a FIFO given as its path; busybox 1.36.1's `grep -r` blocks on one
  anywhere in the tree (D-36) [M].
- **32-bit glibc** (i386, 32-bit Raspberry Pi OS): no large-file support (D-10) [M], and a 32-bit
  workspace key makes accidental collisions (D-09) plausible over many workspaces [GK]. The lead's 32-bit
  unit-suite run passed (L-09), so the suite does not reach these paths.
- **Small boards.** Every mutating turn rewrites the whole session file and adds shadow-repository
  objects; the first shadow `git add -A` of jichi's own tree (2,618 files) took 0.78 s here, an unchanged
  re-add 0.00 s [M]. `read_file` reads the whole file even when only `readMaxBytes` is shown
  (`src/tools/jc_tool_read.c:98-128`).
- **macOS, Cygwin** (case-insensitive by default). `apply_patch` and the read-before-edit check compare
  path strings (`src/chat/jc_app.c:1078-1089`, `jc_tool_apply_patch.c:110-115`), so `A.c` and `a.c` are two
  entries for one file [R]. macOS needs `fcntl(F_FULLFSYNC)` for a real flush [GK]; on Cygwin a `rename`
  over a file another process holds open can fail [GK], which today's `config set` turns into a missing
  config (D-25).
- **FreeBSD, OpenBSD.** `O_RESOLVE_BENEATH` and `unveil()` would give the kernel enforcement that
  `openat2` and Landlock give on Linux [GK]; neither is used.
- **FreeDOS/DJGPP.** No real symlinks, but DJGPP emulates them (DEFERRED def-271); whether its `lstat`
  reports them as links -- which every `lstat`-based fix here relies on -- must be checked on the rig [GK].

---

## 8. Recommendations, ordered by value for effort

1. **In `restore_to`, run `clean` before `reset` and check its exit status** (D-02, plus the 2026-10-05
   gap analysis's open item). Two lines and the smoke driver of section 3; it removes the only measured
   path here that destroys files of which no copy exists.
2. **Refuse NUL-containing files in `edit_file`/`apply_patch` and mark them in `read_file`** (D-01).
3. **Add `--` and refuse option-shaped paths in `search_code`** (D-31).
4. **Write one safe walker and one safe state-file reader.** A walk that `lstat`s, stays under the root
   and keeps a visited set (D-27..D-30), and a `jc_read_regular_file()` that opens non-blocking,
   `fstat`s and requires a regular file under the root (D-15, D-19, D-33..D-35). Most
   repository-controlled findings collapse into these two functions.
5. **Fix the relay baton's in-place growth** (D-14) before relay leaves opt-in.
6. **Consult the existing M851 trust store in setup and for project commands** (D-21, D-32).
7. **Make config writes safe** (D-22, D-25): comment-tolerant merge, temp first, mode kept, backup by
   link.
8. **Restore the original on a failed write** in `edit_file`/`write_file`, as `apply_patch` does (D-04);
   then a mode-preserving replace for user files and jichi's in-workspace files (D-18).
9. **Create `~/.jichi.d` owner-only at start-up**, and have `doctor` report it (D-06).
10. **Durable sessions** (D-08): save at tool-round boundaries and one `fsync` pair per turn, after
    measuring the cost on a board's SD card.
11. **Replace the djb2 workspace key** with a cryptographic hash and record the work tree in the shadow
    repository (D-09); this needs a migration of three stores.
12. **On Linux, open through `openat2(RESOLVE_BENEATH)` when the fence is on** (def-150), keeping today's
    path elsewhere.

---

## 9. Earlier findings in this area: status at `e5d832a4`

| Earlier finding | Status | Evidence |
|---|---|---|
| 2026-10-08 ch. 04 F3: edit/apply_patch cut at a NUL | open, re-measured (D-01) | `jc_tool_edit.c:82`, `jc_tool_apply_patch.c:130` [M] |
| ch. 04 F4: apply_patch keys files by path spelling | open | `jc_tool_apply_patch.c:110-115` [V] |
| ch. 04 F13: edit diffs bounded in lines, not bytes | open | `jc_tool_edit.c:158`, `src/util/jc_diff.c:80-91` [V] |
| ch. 04 F20: merge writes outside the chokepoint | open; symlink half new (D-12) | `jc_tool_parallel.c:1000-1043` [V] |
| ch. 04 F21: search path without `--` | open; measured higher (D-31) | `jc_tool_search.c:246-249` [M] |
| merge lost quoted names (M927) | fixed for the merge; not the sweep (D-03) | `jc_snapshot.c:1069-1110` vs `:1913` [V][M] |
| ch. 05 F4: durable `.jichi` state inside the rollback's reach | open | `jc_snapshot.c:282-288` excludes only `.git/` [V] |
| ch. 05 F6: session saved only at turn end | open (D-08) | no save call in `jc_agent.c` [V] |
| ch. 05 F7: rules walk climbs to `/` without `.git` | open | `src/chat/jc_rules.c:119-152` [V] |
| ch. 05 F11: memory rewritten non-atomically | open (D-18) | `jc_memory.c:273, 381` [V] |
| 2026-10-05 snapshot gap analysis: `clean -fd` status ignored | open; "same universe" premise refuted (D-02) | `jc_snapshot.c:778` [V][M] |
| 2026-10-05 session gap analysis: saved build tag unread | open | written `jc_session.c:348`, not read in `load_from_text` [V] |
| def-150: fence check-then-open window | open by decision; Landlock covers parallel write children | `jc_app.c:334-404`, then `jc_platform_posix.c:344` [V] |
| def-302: `jc_mkdir_p` returns success on failure | open | `jc_platform_posix.c:271-279` [V] |
| M607, M638, M727: resolver fixes | fixed | `jc_path.c:223-258, 260-336, 183-339` [V] |
| M198: FIFO in the session store | fixed there; not for `.jichi`, rules, glossary (D-19, D-33) | `jc_session.c:760-768` [V] |
| M537 (after ANECDOTES #66): undo reports its scope | fixed; its untracked count shares D-02's blind spot | `jc_snapshot.c:1829-1880` [V] |
| R07-F1 / M924: `config set` wiped a commented config | fixed there; not in `setup` (D-22) | `jc_configedit.c:123-131` [V] |
| M760/M761: lease and board read-then-write races | fixed; a new lease gap beside them (D-17) | `jc_lease.c:257-285`, `jc_board.c:338-381` [V] |

---

## 10. What is done well

- **The path resolver fails closed and was corrected by measurement three times**: M607 resolves a
  dangling symlink leaf through its target (`src/util/jc_path.c:223-258`); M638 judges a write into a new
  directory by its deepest existing ancestor and refuses a missing tail containing `.` or `..`
  (`:260-318`); M727 replaced per-hop recursion with a bounded loop (`:173-202`). Each comment names the
  measurement that forced it. [V]
- **The atomic writer is a small textbook**: same-directory temp, exclusive create, 0600 at creation,
  `fclose` and byte count both checked, temp removed on every failure (`jc_platform_posix.c:355-397`);
  section 2.1 shows it keeping the original where the in-place writer did not. [M]
- **The project documents its trade-offs honestly** (FILE_HANDLING §2-§3). [V]
- **The session store treats scanned paths as hostile**: FIFOs skipped (`jc_session.c:760-768`), identity
  from the file name (`:854-866`), foreign files not listed (`:836-852`), "absent" distinct from
  "unreadable" (M482, `jc_platform_posix.c:668-677`); sessions are redacted before they reach disk (M802,
  `jc_session.c:470-481`). [V]
- **`apply_patch` keeps all-or-nothing honest on disk** (M138), and **undo preserves before it destroys**
  (M337b) and **refuses to revert changes the run provably did not make** (M501,
  `src/chat/jc_agent.c:1330-1373`). D-02 and D-03 are holes in the inputs to these mechanisms, not in
  their design. [V]
- **The board's pattern is the one to copy**: lock, reload, apply, save, and refuse to overwrite a board
  it could not read (`src/chat/jc_board.c:299-303, 338-381`). [V]

## 11. Not reviewed

`jc_calib.c`, `jc_audit.c` and `jc_eventlog.c` beyond a search; the Markdown and YAML parsers; PDF
extraction; the language-server integration; the terminal UI's `@` completion; the session renderers;
the worktree preserve/pin/resume bodies in `jc_snapshot.c`; most of the ~6,100 lines of pack prose in
`src/scaffold/jc_scaffold.c` (searched, not read line by line). The fence's decision logic belongs to
this review's chapter on process execution and fences.

## 12. Read further

- `docs/FILE_HANDLING.md` -- the project's own page on reading and writing files; read §2 beside 2.1.
- `docs/SNAPSHOTS.md` -- what the undo net covers; read with D-02.
- `docs/DEFERRED.md` def-150 and `docs/DECISIONS.md` M607 -- the check-then-open deferral and the rejected
  `O_NOFOLLOW`.
- `docs/ANECDOTES.md` #1 and #66.
- `docs/analysis/2026-10-08-review-04-tools-and-delegation.md`,
  `docs/analysis/2026-10-08-review-05-sessions-memory-control.md` -- the previous review of these files.
- `docs/analysis/2026-10-05-JICHI-SNAPSHOT-GAP-ANALYSIS.md` -- the undo analysis D-02 corrects.
- The Linux manual pages `openat2(2)`, `rename(2)` and `fsync(2)`.
