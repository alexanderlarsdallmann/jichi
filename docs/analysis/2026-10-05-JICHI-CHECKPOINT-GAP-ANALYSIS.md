# jichi gap analysis — checkpoints & /undo (data flow)

Read-only. All paths under `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`),
evidence cited, inference flagged, reproducible appendix.

## TL;DR

A checkpoint is `git add -A` + `git commit` in a *shadow* git repo
(`src/snapshot/jc_snapshot.c:6`). That single design choice means **git-ignored
files are never staged and therefore never restored by `/undo`** — and the
restore report is *structurally unable* to say so, because the shadow repo
holds no record of what it skipped. The register flags the hazard prominently
(SNAPSHOTS.md:37-41: "Ignored files are outside the net — read this before you
rely on `/undo`") and reports the tracked + untracked files it *did* revert
(M349) — but it has never turned the hazard into a test, and the one line the
report *could* add ("this turn also touched N ignored file(s) that /undo
cannot see") has no code and no driver.

## 1. What exists — and why it is a strength

The reversibility machinery is real and unusually honest about its own scope:

- **Shadow repo, not your repo.** `git add -A` + `git commit` in its own
  `GIT_DIR`, work tree = the workspace, "without touching the user's own
  `.git`" (`jc_snapshot.c:6-8`). The user's history is never polluted.
- **Undo is explicit and reported.** `/undo` = `git reset --hard <commit>` +
  `git clean -fd` ("so files the agent *created* after the checkpoint" are
  removed too) (`SNAPSHOTS.md:50-51`). A *report* enumerates what was reverted:
  before this work "a revert of 768 files and a revert of none looked identical
  on screen" (`jc_snapshot.c:1723-1726`), now it counts tracked-differing and
  untracked-removed separately (`jc_snapshot.c:1766-1790`).
- **The model is told too (M349).** After an undo that changed a file, a
  `[undo]` note lands in the live conversation — label, the reverted files (up
  to 8, then a count), and "re-read before trusting earlier results" — and the
  session is saved immediately "so a later `--continue` cannot revive the stale
  beliefs" (`SNAPSHOTS.md:65-73`). The register even records the residual gap
  (the CLI `undo` subcommand has no live conversation to tell).
- **The ignored-file hazard is *documented*, not hidden.** `SNAPSHOTS.md:37-41`:
  "Ignored files are outside the net — read this before you rely on `/undo` …
  never captured, and therefore never restored … `reset --hard` cannot restore
  what was never staged, and `clean -fd` leaves ignored files alone. This
  matters because other pages send you to exactly such files."

The net: reversibility is first-class, and the one structural blind spot is
named in the docs.

## 2. The one thing definitely lacking

**A test of the hazard the register names, and a report line for it.** The
ignored-file hole is documented and *true* (the shadow repo never stages
ignored paths, so `/undo` silently leaves them at their post-turn state), but:

- **No driver turns it red.** I found no smoke test that creates a
  git-ignored file, edits it in a turn, runs `/undo`, and asserts the ignored
  file was *not* restored. `tests/smoke/undo_scope.sh`, `undo_note.sh`,
  `baseline_checkpoint.sh`, `rewind.sh`, `snapshot_lint.sh` cover scope, the
  M349 note, baselining, rewind, and the publish lint — none plants an ignored
  file and checks `/undo`'s behavior with it.
- **The report is blind by construction, and says nothing.** The M349/restore
  report counts *tracked files that differ* and *untracked files a `clean -fd`
  would remove* (`jc_snapshot.c:1766,1786-1790`). It has **no pass that counts
  ignored files** (verified: no `ignor|gitignore|exclude` in the report
  function `1723-1810`), so a turn that edited an ignored file reports
  "no tracked file differs from that checkpoint" while a file the user cares
  about silently stays changed. The doc's warning is the *only* place the user
  is told this can happen.

## 3. Why "definitely lacking" (alternatives and their status)

- *"The doc warns about it, so it's not a gap."* The doc warns the *human who
  reads SNAPSHOTS.md*. It does not (a) tell the *user in the moment*, when
  `/undo` reports "no tracked file differs" after a turn that touched an
  ignored file, or (b) *prove the behavior* with a test, so a change to the
  shadow-repo staging could silently close — or widen — the hole with no red
  signal. A documented hazard with no test is a hazard, not a check. This is
  the M326b shape the register itself fears: the check passes because its
  premise (the staging set) was never exercised.
- *"Git repos skip the file-count check and trust .gitignore (jc_snapshot.c:32-34), so this is git-repo-only."* False — the ignored-file blind spot is *worse* in a git repo, because that is exactly where `.gitignore` excludes real files (build outputs, `.env`, local configs) that an agent might edit and a user expect to undo. The 20k-file self-disable is a separate, non-git-only path.
- *"`clean -fd` leaves ignored files alone, so /undo is at least *predictable*."* Predictable is not *communicated*. The predictable-but-silent case is the one that costs: the user runs `/undo`, sees "reverted", and the ignored file is still changed — and nothing told them which.

## 4. Evidence

- **`src/snapshot/jc_snapshot.c:6-8`** — "A shadow git repository (its own
  GIT_DIR, work tree = the workspace) records checkpoints without touching the
  user's own .git." (Verified.)
- **`src/snapshot/jc_snapshot.c:32-34`** — "Beyond this many files in a
  non-git workspace, snapshots disable themselves … Git repos are trusted to
  bound the set via .gitignore and skip the check." (`JC_SNAPSHOT_MAX_FILES
  20000`, `:35`.) (Verified.)
- **`docs/SNAPSHOTS.md:37-41`** — "Ignored files are outside the net — read
  this before you rely on `/undo`. … never captured, and therefore never
  restored … `reset --hard` cannot restore what was never staged, and `clean
  -fd` leaves ignored files alone." (Verified — the hazard is *documented*.)
- **`docs/SNAPSHOTS.md:50-51`** — "/undo restores the most recent checkpoint
  with `git reset --hard <commit>` followed by `git clean -fd` (so files the
  agent *created* after the checkpoint …)." (Verified.)
- **`src/snapshot/jc_snapshot.c:1723-1726`** — "so a revert of 768 files and a
  revert of none looked identical on screen" (the report was added to fix
  this). (Verified.)
- **`src/snapshot/jc_snapshot.c:1766,1786-1790`** — the report counts
  `untracked` (via `git clean -nd`) and appends "%d untracked file(s)
  removed"; the *tracked* count via a differing-files walk. **No ignored-file
  pass** — `sed -n '1723,1810p' | grep -n ignor` is empty. (Verified.)
- **`docs/SNAPSHOTS.md:65-73`** — M349: the `[undo]` note to the model,
  "up to 8, then a count", saved immediately; "Known residual gap: the CLI
  `undo` subcommand … a saved session's beliefs stay stale there (recorded in
  DEFERRED.md)." (Verified.)
- **Tests** — `ls tests/smoke/ | grep undo` → `undo_across_branch.sh`,
  `undo_note.sh`, `undo_scope.sh`, `baseline_checkpoint.sh`, `rewind.sh`,
  `snapshot_lint.sh`. **None plants a git-ignored file and asserts `/undo`
  leaves it changed** (verified by grep for `gitignore|ignored` across the
  undo/snapshot drivers — the only hit is `snapshot_lint.sh`'s *publish* scan,
  a different feature, M484).

## 5. Implications

- **The reversibility guarantee is narrower than it reads.** The user's mental
  model is "/undo reverts that turn's edits." For tracked and untracked-but-
  not-ignored files, that holds and is reported. For ignored files it *does
  not hold* — and the report actively says the reassuring thing ("no tracked
  file differs"), which for a turn whose only meaningful change was to an
  ignored file (a local config, a generated manifest, an `.env`) is the
  *most confidently wrong* line the tool can emit.
- **It is a trust problem, not just a correctness one.** The register's whole
  M349 project is "the model is told too" — undo is only safe if the model
  (and user) know what it *didn't* revert. The ignored-file case is the one
  where that knowledge is impossible from the report, because the shadow repo
  never held the file. The doc carries the knowledge, but the doc is not in
  the moment, and not in the model's context after an undo.
- **The hole is *wider than the doc implies* for git repos.** Non-git
  workspaces have no `.gitignore`, so the hazard is rarer there; git repos —
  where jichi "is trusted to bound the set via .gitignore" (`jc_snapshot.c:34`)
  — are exactly where ignored files are *expected* and an agent editing one is
  plausible. The doc's "other pages send you to exactly such files"
  (SNAPSHOTS.md:43) is the register already aware the agent is routed there.

## 6. Recommendation

1. **A red-first driver: the ignored-file `/undo` test.** Plant a workspace
   with a `.gitignore` (or use one), create an ignored file, a turn that edits
   it, `/undo`, and assert (a) the ignored file is *still changed* (documenting
   the real behavior), and (b) the report *names it* — "this turn also touched
   N ignored file(s); /undo did not revert them." Today the driver is green on
   (a) by accident (nobody checks) and red on (b) (no such line exists). That
   converts the documented hazard into a tested, reported one.
2. **The report line.** In the restore-report path (`jc_snapshot.c:1723-1810`),
   add a `git ls-files --others --ignored --exclude-standard` count (or
   `git status --porcelain` reading the `!!` ignored entries) intersected with
   the turn's known-edited set, and append "… ; N ignored file(s) not
   reverted" when non-zero. This is the one sentence that makes the doc's
   warning true *in the moment* instead of *in the manual*.
3. **One line in SNAPSHOTS.md next to the warning:** the report now says when
   it skipped an ignored file (step 2), so "read this before you rely on
   /undo" becomes "the report tells you when it applied."

Rejected, in the register's spirit:

* *Stop git from ignoring files, or stage them anyway (`git add -f`)* — that
  defeats the user's `.gitignore` and the shadow repo's "without touching your
  .git / respect your ignore" contract (`jc_snapshot.c:6,34`). The blind spot
  is inherent to "a checkpoint is what git stages"; the fix is to *report* the
  blind spot, not to widen staging into the user's ignore policy.
* *Treat it as doc-only and close it* — the register's own doctrine (cited in
  `snapshot_lint.sh`'s header: "prefer a lint to an audit, because the audit
  found what it knew to look for") is that a hazard with no red-first test is
  the thing that *regresses silently*. The doc is the audit; the driver +
  report line are the lint.

## 7. Limitations and honest counterarguments

1. **The core guarantee is strong and reported.** I am not claiming `/undo`
   is unreliable — for the tracked + untracked-but-visible set it is explicit,
   counted, and told to the model (M349). The gap is one named class of file
   (ignored) where the guarantee does not hold and is not surfaced.
2. **The hazard *is* documented — loudly.** SNAPSHOTS.md:37-41 is unambiguous.
   This is not a hidden defect; it is a *known, documented* limitation that
   lacks the two things the register elsewhere insists on: a red-first test
   that pins the behavior, and an in-the-moment report line. The
   "definitely lacking" thing is precisely those two, not the awareness.
3. **I did not run `/undo` on an ignored file** (read-only analysis). The
   "the report has no ignored-file pass" claim is verified by reading
   `jc_snapshot.c:1723-1810`; the "no test plants one" claim by grepping the
   undo/snapshot drivers. Step 1's driver is what converts both to executed
   evidence.
4. **Git repos and non-git workspaces differ.** In a *non-git* workspace there
   is no `.gitignore`, so the ignored-file class is small (only files matching
   git's global ignore, if any); the hazard concentrates in git repos, which
   is where jichi is used most. I scope the recommendation to the git-repo
   case where the class is non-empty.

## 8. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

    # the shadow repo (why ignored files are out of scope)
    sed -n '6,8p'   src/snapshot/jc_snapshot.c        # "shadow git repository … without touching your .git"
    sed -n '32,35p' src/snapshot/jc_snapshot.c        # self-disable + "trusted to bound via .gitignore"

    # the documented hazard
    sed -n '37,43p' docs/SNAPSHOTS.md                 # "Ignored files are outside the net"
    sed -n '50,51p' docs/SNAPSHOTS.md                 # /undo = reset --hard + clean -fd
    sed -n '65,73p' docs/SNAPSHOTS.md                 # M349: model told; CLI residual gap

    # the restore report (tracked + untracked, NO ignored pass)
    sed -n '1723,1726p' src/snapshot/jc_snapshot.c    # "revert of 768 … identical on screen"
    sed -n '1760,1790p' src/snapshot/jc_snapshot.c    # untracked count via `clean -nd`
    sed -n '1723,1810p' src/snapshot/jc_snapshot.c | grep -n ignor   # (expect empty)

    # the tests: none plants an ignored file through /undo
    ls tests/smoke/ | grep -iE 'undo|snapshot|rewind|baseline'
    grep -rln 'gitignore\|ignored' tests/smoke/undo*.sh tests/smoke/snapshot_lint.sh tests/smoke/rewind.sh

**Verdict line for the register this could seed:** jichi's checkpoints
(src/snapshot/jc_snapshot.c) are `git add -A` in a shadow repo — a clean design
that keeps the user's .git untouched — and `/undo` is explicit, counted, and
told to the model (M349: a `[undo]` note lists the reverted files, "up to 8,
then a count", and the session is saved so stale beliefs can't revive). The
register *documents* the one class the shadow design excludes — "Ignored files
are outside the net — read this before you rely on /undo" (SNAPSHOTS.md:37-41)
— but has never tested it and the restore report is structurally blind to it:
it counts tracked-differing and untracked-removed files (jc_snapshot.c:1766,
1786-1790) with no ignored-file pass, so a turn whose only change was to a
git-ignored file reports "no tracked file differs from that checkpoint" while
leaving that file changed. The hazard is loudest in git repos — exactly where
.gitignore excludes real files (local configs, build outputs) an agent is
routed to edit ("other pages send you to exactly such files", SNAPSHOTS.md:43).
Definitely lacking: (1) a red-first driver that plants an ignored file, edits
it, runs /undo, and asserts it is still changed *and named*; (2) one report
line ("… ; N ignored file(s) not reverted") from `git status --porcelain`'s
`!!` set intersected with the turn's edited files. Revisit when: the driver is
green (the behavior pinned, not incidental) and the report surfaces the class
it currently can't see — at which point "read this before you rely on /undo"
becomes "the report tells you when it applied."
