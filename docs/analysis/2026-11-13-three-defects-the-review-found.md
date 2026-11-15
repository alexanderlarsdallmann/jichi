# Three more defects in the harness, found by reviewing its reading course

*2026-11-13. Milestone M1179. Written for self-learners, junior developers, and
agents. It follows
[the five defects of M1178](2026-11-13-five-harness-port-defects.md), found by
writing the same course.*

## In three sentences

After the [Tazuna](../reading/TAZUNA.md) chapters were written, five agents
checked every claim in them against the code, and three claims turned out to
be true of the code's comments but not of the code. A push option sent data
past the secret scan, the drive's end time was read wrongly in some
spellings, and a page command that could not run failed in silence. Each was
re-measured by the supervisor, fixed, and given a test that fails on the old
binary.

## 1. A scan that did not cover what the push sends

**What it should do.** Before `jichi-harness mirror push` sends anything, a
key scan reads every file version the push would add and refuses if one holds
the model key or the git token.

**What it did.** The push builds its list of refs to send in one function
(`build_refspecs`), and the scan builds its list of refs to read in another
(`resolve_tips`). With the config option `pushKept` on, the first list gains
`refs/kept/*`, the drive's kept attempts. The second list never did. So the
kept attempts went to the remote unscanned.

The chapter 3 reviewer measured it, and the supervisor reproduced it: a
rehearsal with `pushKept` on, a kept attempt holding a canary token, a key scan
that reported one file version checked and no secret, and a stand-in remote
that then held the token. `pushKept` is off unless a config sets it, and the
drives' own push script never sends kept refs, so no real secret went
anywhere.

**A second hole in the same place.** The scan carries at most 24 tips. Past
that, `resolve_tips` dropped the rest without a word, and a dropped tip is
pushed unscanned. It also skipped a tip when the memory for it could not be
allocated.

**The fix.**
- `resolve_tips` lists the kept refs whenever `pushKept` is on, so the scan
  and the size check read the same refs the push sends.
- More than 24 tips is a refusal that names the limit; a failed allocation is
  a failure. A tip is never dropped.
- The success line names the kept refs when it pushed them.

**The lesson.** When two lists must agree, they will drift unless one is made
from the other or a test compares them. A scan must cover what the push
sends, and the test must put the secret where only the new refs carry it.

## 2. An end time read too kindly

**What it should do.** `drive` stops starting runs at `drive.end`, written as
`YYYY-MM-DD HH:MM:SS +ZZZZ` or as a bare `YYYY-MM-DD HH:MM:SS` in UTC.
`docs/HARNESS.md` says it refuses anything else, because a deadline read as the
wrong instant would stop the loop at a time nobody chose.

**What it did.** It called `sscanf` and checked only how many fields were
converted. `sscanf` stops quietly at the first thing it cannot match, so:
- `+01:00` was read as the integer `+01`, the offset +0001: a deadline 59
  minutes off;
- `Europe/Berlin` or `Z` after the time was ignored, and the time read as UTC;
- `2099-02-31` was accepted and rolled over into March.

**The fix.** A small hand parser reads exactly the two forms, digit by digit,
allows only blanks after them, and checks the day against the month.

**The lesson.** `sscanf`'s return value counts what it converted. It does not
tell you that nothing else was there. To accept exactly one form, check the
end of the string too.

## 3. A page that failed in silence

**What it should do.** When the push has failed for a while, `push tick`
writes a PAGE line and runs `host.pageCommand`. A failure of that command is
logged, never fatal.

**What it did.** It reported only a failure to start the command. On the
default backend jichi starts a child with `fork` and then `exec`. When `exec`
fails, the child exits 127, so the parent sees a command that ran and exited
127, not a command that failed to start. A misspelt `pageCommand` paged nobody
and said nothing.

**The fix.** Any non-zero exit of the page command is now reported on stderr,
and 127 is named, because it is the one a broken configuration produces. A
command can also exit 127 by itself, so the message names the code and does
not claim the cause.

**Not done yet.** The root of it is in the shared spawn code: its fork backend
cannot tell its caller that `exec` failed. A pipe that the child writes the
error to, closed on a successful `exec`, would let every caller see it. That
code starts every process jichi runs, so it is left for after 0.14.0.

**The lesson.** With `fork` and `exec`, "could not run" and "ran and failed"
look alike to the parent. Report both.

## 4. The proof

| defect | test | pre-M1179 binary | fixed |
|---|---|---|---|
| kept attempts unscanned | `harness_mirror_push` cell 34 | exit 0: the scan read 0 file versions, and the push went out | the scan refuses, exit 1, naming the file; nothing pushed |
| tips past the limit dropped | `harness_mirror_push` cell 35 | exit 0: the kept refs were not tips at all | refused, naming the limit |
| the end time misread | `harness_drive` cell 15 | all five bad forms accepted, exit 0 | each bad form refused; three good forms give the instants they name |
| the page command silent | `harness_push_tick` cell 10 | nothing about the page command on stderr | `pageCommand exited 127` on stderr, the PAGE line written |

## 5. How these were found

Not by a test, and not by running the drive. A reviewer was asked to check
whether each sentence of a chapter was true, and wrote down where the code
and the sentence disagreed. Two of the three sentences repeated what a code
comment or `docs/HARNESS.md` said the code did. The comment was the
intention; the code was the fact. Reading a comment is not reading the code.
