# Tazuna: the reading course for jichi-harness (design, and the review record)

*2026-11-13. The operator: "Please, write a jichi reading course for the jichi
harness, too." The course is [`docs/reading/TAZUNA.md`](../reading/TAZUNA.md).
This page records how it was made and what its review found, so a reader can
judge it.*

## 1. Scope and shape

There are ten chapters, one per thing the harness does, in the order a drive
meets them:
- the purpose and the threat model;
- `init`, the mirror and its credential helper, `run` and the handoff;
- the `drive` loop, the scheduled `push`, `notify` and `watch`, and `gate run`;
- `main` and `doctor`;
- the method the harness was built by.

The house style is [Annai](../reading/ANNAI.md) for the level and
[Fukabori](../reading/FUKABORI.md) for the honesty about trade-offs. Each
chapter has the same sections:
- **Why this exists**
- **The shape**
- the walk through the code
- **Try it**
- **What went wrong here, and what it teaches**
- **Check yourself**

## 2. The rules every chapter was held to

These are enforced by `tests/smoke/reading_refs_lint.sh` and
`tests/smoke/reading_quotes_lint.sh`, or checked in review:
- anchors are `file.c:function`, and line numbers are banned;
- every quoted block matches its source line for line;
- every published command was run first, in the published form;
- nothing is claimed that was not read in the code or measured;
- incidents are told only from the tree's own records, cited by milestone;
- there are no links into `docs/internal/` (the public snapshot does not carry
  it), and no machine names or paths.

## 3. How it was written

1. **Drafts.** Five Claude agents each drafted two chapters from one shared
   brief, working in a scratch clone of the tree that became 0.14.0. Each
   agent built `jichi-harness` in its own copy, ran its "Try it" commands in
   throwaway directories, and reported:
   - the lints' last lines;
   - every command with its exit code;
   - the claims it could not verify.
2. **Review.** The supervising Claude session ran the reading lints and the
   other docs lints over the whole course. It also reviewed each chapter against
   the code. The review findings are in section 4.

## 4. Defects the writing found in the harness itself

Writing a reading course turned out to be a code review. The agents read every
line they described and ran every command they published. They reported the
places where the code, its comments, its help text or `docs/HARNESS.md`
disagree.

**Every item here is re-verified by the supervisor before it is filed or
taught as fact.** An item a chapter states as measured carries its measurement
in that chapter. Items not yet re-verified are listed as reported, not as
findings.

### 4a. Verified, and fixed in M1178

First verified by reading the code, each was then re-measured by the
supervisor on the unfixed binary, fixed, and given a test seen red on that
binary and green on the fix. The account for learners is
[`docs/analysis/2026-11-13-five-harness-port-defects.md`](../analysis/2026-11-13-five-harness-port-defects.md).

| defect | found by | the measurement |
|---|---|---|
| `jc_hp_watchgate.c:jc_gate_withdrawn` read to the end of the LOG, not of its line | the chapter 7 draft | missed withdrawals (64 KiB after one), and a false one across two lines (found by the fix's own test) |
| `jc_hp_watchgate.c:wgate_read_file` read a LOG of 1 MiB or more as no LOG | the chapter 7 draft | a 1,072,526-byte LOG gave a silent tick; the live LOG was at 474,487 bytes |
| `jc_hp_gateledger.c:jc_hp_gateledger_run` read and freed an uninitialised accept list | the chapter 8 draft | valgrind on the real binary: uninitialised reads in the accept check and in `free()` |
| `jc_hp_gate.c:gf_git` pasted the `<SHA8>` into `sh -c` | the chapter 8 draft | an injected command ran |
| the busy filter could not see a running gate | the supervisor's replay of a real moment, before the course; chapter 7 teaches it | the port would have started a second gate |

### 4a2. Found by the review, verified, and fixed in M1179

- **`push` with `pushKept` sends the kept attempts unscanned.**
  `jc_harness_push.c:resolve_tips` gives the key scan and the size check only
  `<prefix>/main` and `<prefix>/work/*`, while `build_refspecs` also pushes
  `refs/kept/*` when `pushKept` is set. Found by the chapter 3 reviewer on
  2026-11-13 and reproduced by the supervisor: with `pushKept` on, a kept
  attempt holding a canary token reached a local stand-in remote, after a key
  scan that reported one file version checked and no secret. `pushKept` is off
  unless a config sets it. The drives' own host push (`push.sh`) never pushes
  `refs/kept`, and no live config sets `pushKept`. `docs/HARNESS.md`'s "`refs/kept/*`
  is never pushed" was true of the script, not of the port with `pushKept`.
  Fixed in M1179 (the scan lists the kept refs; more than 24 tips is a refusal).
- **`drive`'s end time is read too leniently.**
  `jc_harness_drive.c:parse_deadline` checks only how many fields `sscanf`
  converted. `+01:00` is read as the offset `+0001` (a deadline 59 minutes
  off), and a trailing zone name is ignored and the time read as UTC. Found
  by the chapter 5 reviewer and confirmed by the supervisor in the code: the
  offset is scanned with `%d`, and nothing checks what follows it. Fixed in
  M1179 (a strict parser of the two forms, with a calendar check).
- **A page command that cannot be executed is not reported.** On the default
  fork backend an exec failure is the child's exit 127, which
  `jc_hp_tick.c:tick_run_page` did not report; the PAGE line was still
  written. Found and measured by the chapter 6 reviewer. Fixed in M1179 (any
  non-zero exit is reported, 127 named); the root, in the spawn code's fork
  backend, is left for after 0.14.0.

### 4b. Reported by a draft and confirmed by the supervisor's own runs

These are behaviours, not defects. The chapters state them as measured.

- `watch gate --dry` still writes the `on-master` state word. Seen on
  2026-11-13 in a dry replay against copies of the state file. (Whether the
  reference did the same is not shown by anything in the tree.)
- A `not-ff` RED from the watcher reaches only `host.notifyCommand`, as text:
  no inbox note and no digest line. Measured by the chapter 7 draft and again
  by its reviewer with a recording hook.
- The `DRY:` line names `gate-run.sh` whatever `host.gateRun` says: chapter 7's
  step 2 runs with `gateRun` set to `/bin/true` and prints `gate-run.sh`.
- A failed unit's `systemctl list-units` line starts with `●` (bytes
  `342 227 217`) and a blank. Measured on systemd 255 with a transient unit
  that ran `/bin/false`. Chapter 10's draft had it right; chapter 7's draft and
  the M1178 analysis page had said it was not measured, and were corrected.

### 4c. Reported, not yet verified

Not taught as fact. Each needs a measurement before it becomes a register row
or a fix:

- `run_on_sigint` reads a plain `pid_t` in a signal handler (against the
  `sig_atomic_t` rule in CLAUDE.md);
- `backlog_stamp` always writes `+0000`;
- `jc_utf8_trunc_len` reads past short strings in `init`;
- the watcher waits forever when `host` names only `pushHome` (the `repo[]`
  buffer reused; chapter 10 describes it from reading);
- `hdoc_add`'s counter overflow;
- an empty `JICHI_HARNESS_MODEL_KEY_FILE` wins over the config;
- stale help, comment and HARNESS.md texts: the gate's "not built yet", the
  `init` usage, the hooks-refusal path, the cred_get comment, the check
  numbering, and the cell counts;
- `mirror head` runs git in the mirror without the safety checks `mirror
  fetch` and `mirror push` make first (a reviewer measured it with a planted
  hook; `rev-parse` runs no hook, so the effect is unproven);
- `cred_get`'s comment says `none` gives an empty username, but the helper
  prints `username=none`;
- two `notify` calls for one SHA8 in the same minute write the same note file
  name, so the second replaces the first;
- `jichi-harness --help` still says the HOLD, land and repin arms are not
  built;
- `LANDED` records only that the land command returned 0, not that `master`
  moved;
- `docs/ROADMAP.md`'s M1159 "Rejected" line says land and repin do not go
  through `sh -c` with a pasted command, but `gf_capture_cmd` does paste one.
  Since M1178 the only value pasted there, the `<SHA8>`, is checked first.

## 5. Review record

**State on 2026-11-13: all ten chapters reviewed against the code, and
corrected.**

**How.** After the drafts, the supervisor rewrote the passages about the five
defects for the M1178 code (chapters 7, 8, 10) and re-ran their "Try it"
blocks on its binary. Then five Claude agents, two chapters each, checked
every checkable sentence against the code in a read-only copy of the tree
(the merged course and M1178): each function's behaviour, each quoted block,
each anchor, each number, each history claim against `docs/ROADMAP.md`,
`CHANGELOG.md`, `docs/ANECDOTES.md` or the code's comments, and each "Try it"
block, run in its published form. Each listed what was wrong or unverifiable,
with evidence. The same agents then applied their findings, re-verifying each
one first, in the chapter files only. The supervisor checked the edits and
fixed what they left.

**What the review found.** About 870 checkable claims were checked; 83 were
wrong or could not be verified from the tree (the reviewers' own counts).

| chapters | claims checked | wrong or unverifiable |
|---|---|---|
| index, 1, 2 | about 220 | 19 |
| 3, 4 | about 145 | 15 |
| 5, 6 | about 142 | 15 |
| 7, 8 | about 185 | 20, plus the stale outputs |
| 9, 10 | about 178 | 14 |

Most were overstatements ("every", "exactly", "only") that one case in the
code contradicts, citations to the wrong record, and outputs that changed with
M1178 or with the example address. Three were defects in the code (section
4a2), and three earlier corrections by the supervisor turned out wrong or
unmeasured in turn: chapter 7's "both errors fall on the cheap side", chapter
7's "a failed unit's line was not measured", and a claim about the reference
script's `--dry` behaviour that the tree cannot show.

**What every published command is.** Every "Try it" block was run in its
published form on the M1178 code, and every shown output is from such a run.
Temporary paths, timestamps and git commit hashes that carry a timestamp
differ from run to run.

**The lints.** `reading_refs_lint`, `reading_quotes_lint`, `docs_locators_lint`,
`posix_utils_lint`, `snapshot_lint`, `milestone_currency_lint` and `docs_flags`
pass on the course. `reading_quotes_lint` checks that each quoted line is
still in its file and that each tag names the right function, not the order of
the lines; the review checked the order by hand.
