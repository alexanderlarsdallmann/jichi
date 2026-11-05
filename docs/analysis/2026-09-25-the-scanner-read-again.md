# The scanner read again: 656 prompts, 23 wrong rules, and a verb it did not know

*Analysis, 2026-09-25 (M743). A measurement of the constraint scanner
(`src/chat/jc_constraint.c`) over every distinct user message in threadwork's session
store, written the night of the 2026-09-24 zigodot drive, because that drive's zero
turned out to measure the scanner rather than the work
([the drive's page](2026-09-25-the-overnight-drive.md), §6). It follows M730's
measurement ([ROADMAP M730](../ROADMAP.md), [CONSTRAINTS.md](../CONSTRAINTS.md)), which
used the other machine's store. It is written for the developer who changes the
scanner next, and for a self-learner who wants to see a heuristic measured against its
population rather than argued about. The corpus is the operator's own prompts and stays
out of the tree. `tests/measure/scanner_corpus.py` rebuilds it and reproduces the
session-store counts, 626 of the 656 texts. The other 30, the two drives' task files, were
deleted with the drive's data at 00:42 that night
([the drive's page](2026-09-25-the-overnight-drive.md), §1).*

---

## 0. The answer first

1. **The drive's zero was the scanner's.** The drive's ten complete arms, 240 `--auto` runs, journalled no
   `constraint_advisory` event. Sixteen of the drive's tasks end *"Do not modify any
   file."*, and the read-only rule knew one verb, *edit*. So the scanner inferred
   nothing from any of them (§1).
2. **On the population the scanner actually sees, 23 of its 73 rules were wrong** (§3).
   That is 656 distinct messages: every turn of every session in the store, in three
   formats, plus both drives' task files. The 23 came from nine mechanisms, and **none
   crossed a sentence**, which is the one mechanism M735 fixed. The most frequent is a
   word inside a file name: `src/root.zig` made seven prompts say *do not run
   privileged commands*.
3. **Seventeen messages meant "read-only" and inferred nothing** (§4): the sixteen
   drive tasks and one code review that said so three times.
4. **After M743: 69 rules, 2 wrong, and none of the 50 right ones lost** (§5). All 23
   wrong rules were removed except two, all seventeen misses are found, and each of
   the 36 messages whose verdict moved was read.
5. **One of M207's checks had been green for the wrong reason** (§6). *"do not edit
   src/gdscript/parser.zig"* passed because a skip meant for a plural *s* ate the *s*
   of *src*.
6. **M730's number does not carry over, and was never meant to** (§2). It measured the
   workstation's store. On this machine's version of the same population (first
   messages only), the scanner at HEAD inferred six rules and all six were wrong.

## 1. Why the scanner was measured again

The drive ran ten arms of 24 tasks each on zigodot with `--auto`. D2's question for it
was *how often does an inferred rule meet the work?* In its ten complete arms the answer came back 0 in 240
runs. 0 is a suspicious number for a heuristic that runs on every turn, so before
reading it as "inferred rules rarely matter" I asked the pinned binary directly.
`brief-check` calls the scanner and no model:

| Sentence | Inferred |
|---|---|
| Do not **edit** any file. | read-only |
| Do not **modify** any file. | nothing |
| Do not **change** any file. | nothing |
| Do not **touch** any files. | nothing |
| Do not **write to** any file. | nothing |

The drive's read-only tasks all use *modify*. Its zero answered a question about a verb
list, and the measurement below follows from that.

## 2. The population, and how it differs from M730's

`tests/measure/scanner_corpus.py` collects every user-role message from
`~/.jichi.d/sessions/**/*.json` and every file named with `--extra`, dedupes them by
content, and runs `jichi brief-check` on each in an empty `HOME`.

Threadwork's store has 1,409 session files, in three shapes:

- **290 in the store's own directory** (2026-05-13 to 2026-09-24). 149 have an empty
  history (e2e sessions under `/tmp`), 82 are jichi-format, and 59 are
  **Continue-format**. The Continue-format ones are the IDE extension's sessions,
  from when jichi shared `~/.continue/sessions` (moved to `~/.jichi.d` at M204). They
  wrap each turn as `{"message": {"role", "content"}}`.
- **1,119 in an older layout nested under it** (2026-06-18 to 2026-07-28).
- Across all of them: **504 distinct jichi-format user messages and 124
  Continue-format ones**, from every turn, not only the first, since `--auto` scans
  every turn (the M110 auto-adopt in `jc_agent.c`). Two messages are in both, *"hi"*
  and *"Thank you!"*, so 626 are distinct. The Continue ones are the only large
  stratum written by a person to a coding agent. Most of the jichi-format ones are
  briefs an agent wrote.
- **Both drives' task files add 30 distinct texts**: 57 files, one task list used on
  two nights plus three mode preambles. That makes 656 in all.

The counts were checked by a second route. The script dedupes by SHA-1. A pipeline
over `json.dumps` of each message with `sort -u`, run per format, gives 504 and 124,
and their intersection is the two messages named above. `sha1sum | sort -u` over the
task files gives 30.

M730 measured "the first user message of every stored session **on the development
workstation**, 289 prompts". Threadwork's store is a different set of prompts. The
closest thing to M730's population here is `--first-only --top-only`: 67 distinct
first messages from the store's own directory. The HEAD scanner infers six rules from
them, and all six are wrong (§3: three from `root.zig`, three from `never-gated`).
M730's record said that sentence scope "would have scored 3 of 3" on its sample. That
was true of its sample. On this one, sentence scope was not the problem.

## 3. What the HEAD scanner inferred, read one by one

73 rules in 46 messages:

| Rule | Count |
|---|---|
| do not run build commands | 22 |
| do not use the tool `run_tests` | 15 |
| do not run tests | 15 |
| read-only | 13 |
| do not run privileged commands | 7 |
| do not install packages | 1 |

Each was classified by reading the message around the line `brief-check` names, and
asking whether the brief meant it. One reader did this, the agent that wrote this page.
It is a judgment, and every case is quoted below or in `tests/test_constraint.c` so it
can be re-judged.

**50 right.** Eleven briefs say *"Do NOT run `zig build`/`zig build test` yourself --
the gate runs it"* (3 rules each, 33). Three probes say *"Do not run tests."* (6). And
eleven messages mean read-only, in forms like *"READ-ONLY analysis. Do NOT edit any
file."* and *"Please stay read-only."* (11).

**23 wrong, by nine mechanisms:**

| # | Mechanism | Rules (messages) | Verbatim |
|---|---|---|---|
| 1 | a keyword inside a **file name** | 8 (7) | *"Do NOT modify \`src/root.zig\`."* → privileged commands; *"Do NOT edit root.zig, build.zig"* → privileged + build |
| 2 | a negation cue glued to a **hyphen** | 5 (5) | *"forces never-gated dead code to COMPILE/RUN for the first time"* → build |
| 3 | the clause after a **dash** | 4 (3) | *"do NOT hardcode an external absolute path -- the test must be portable), run it through runConformanceTest"* → tests (the brief asks for a test to be run) |
| 4 | the English verb ***make*** | 1 (1) | *"Do not make changes to prepare_sourcedata.py."* → build (Continue-format, written by a person) |
| 5 | *not just* | 1 (1) | *"CHECK YOUR WORK as you go with the runner (do not just build blindly)"* → build |
| 6 | an **article** before *read-only* | 1 (1) | *"Confirm the upstream Godot path remains the read-only reference"* → read-only |
| 7 | a **kind** of file as a blanket object | 1 (1) | *"Do NOT edit any source; your only file output is the design doc named below."* → read-only |
| 8 | *avoid X **and** build Y* | 1 (1) | *"Optimize the implementation to avoid the intermediate `ArrayList` and build the final string directly."* → build |
| 9 | a **third-person** *never* | 1 (1) | *"the run-gd harness never installs a ClassDB"* → install |

Mechanism 3's two other messages are *"Do NOT add recursion/defaults/typed-params --
just make ONE user call"* and *"-- just make if/else correct"*. They carry mechanism 4
as well, since the *make* after the dash was read as the build tool.

Mechanism 2 is M433's own. *"force never-compiled core code to compile"* is the brief
that `brief-check` was built to catch (M433), and it was still firing in five prompts,
because the tool reports a misparse and nothing had fixed this one.

## 4. What it missed

A second route looked for the misses. A scratch script, kept with the corpus and not in
the tree, extracts every sentence where a negation meets an edit-family verb: 136
sentences in the jichi-format and task strata, 84 distinct. I classified them by hand.
Seventeen messages mean read-only and inferred nothing:

- **the sixteen drive tasks**: *"... citing file:line for each claim. Do not modify any
  file."*
- **one code review**: *"You are performing a READ-ONLY code review. Make NO edits to
  any file."* and later *"Do NOT modify any files."* Three statements, all missed. *a
  read-only code review* has no cue, *no edits to* read *to* as the object, and
  *modify* was not a verb.

The candidate list also holds the sentences that must NOT become read-only. They are
the reason the recall fix could not be a word list alone: *"Do not modify any other
file."* (with *other* skipped as a determiner), *"Do not touch
src/gdscript/codegen.zig."* (with *src* a blanket object), and *"Do not touch anything
else."* (with *else* not a narrowing word). Each would have been read-only the moment
its verb joined the list. They are in the unit tests for that reason.

This route covers edit verbs only. A missed *commit* or *push* ban phrased in a way the
scanner does not know was not searched for, and this page claims nothing about it. The
Continue stratum was added to the corpus after the candidate search. Its one inference
was read (row 4), but its sentences were not searched for misses.

## 5. What M743 changed, and the corpus after

Each change comes from a row above, and each has a unit check that goes red when that
guard alone is reverted. 27 perturbations, one guard each, turned their checks red
(the ROADMAP entry lists them).

- **Verbs**: *modify*, *change* and *touch* are orders, *make any changes* matches its
  plural, and an object after *to*/*in*/*on* is read (§4).
- **Narrowing**: *other*, *else* and *but* narrow. *code*, *source* and *src* name a
  kind of file, and a kind is a scope (row 7). A backticked, quoted or bracketed object
  is a named one. A verb list reaches its one object. A subject before *do not* makes
  it a statement, as the negation scan has held since 2026-09-21.
- **read-only** after an article is an adjective (row 6).
- **Cues**: one glued to a hyphen or a letter is part of a word (row 2). *not just* /
  *only* / *merely* / *simply* forbids nothing (row 5). A negation reaches to the end
  of its clause: a semicolon, a spaced `--`, an em or en dash (row 3).
- **Targets**: *make* is the build tool only when it is run (*"do not run make"*,
  *"\`make\`"*, row 4). A keyword followed by a file extension is a file name (row 1).
- **Wording**: `brief-check` and `constraints scan` said inferred rules would be
  ENFORCED, ten milestones after M734 made them advise. They now say ADVISORY.

The same corpus, rescanned with `--against` the HEAD binary:

| | HEAD | M743 |
|---|---|---|
| rules | 73 | 69 |
| right | 50 | 67 |
| wrong | 23 | 2 (rows 8 and 9) |
| read-only messages missed | 17 | 0 |
| messages whose verdict moved | | 36, each read |

M730's population on this machine (§2) goes from 6 rules, all wrong, to 0.

## 6. The check that was green for the wrong reason

M207 added *"do not edit src/gdscript/parser.zig"* as a scoped example that must not
become read-only. It passed at HEAD. With only the plural-*s* skip removed, it fails.
The old code skipped one *s* after the blanks, meant for a plural glued to the phrase,
which `word_offset()` never lets through. So it judged the object *rc*, not *src*, and
*src* was on the list of blanket objects. The check passed because one bug hid
another. It was never red against its own guard, because its own guard was not what
kept it green. M743 removes the skip, and removes *src* from the blanket list for its
own reason (row 7). The check now fails if *src* is put back, which is the guard it
was written for.

## 7. What this does not settle

- **Two wrong rules remain.** Row 8 needs the coordinator *and* to be told apart from a
  list of banned verbs (*"never commit or push"*). Row 9 needs a third-person verb told
  apart from a stated policy (*"we never push to master"*, which the scanner's design
  keeps on purpose). Both are in DEFERRED.
- **The trade in row 7.** *"Do not modify the code."* no longer infers read-only, so a
  brief that means it now gets no backstop. M207's rule chose this direction, and
  after M734 both errors only advise, but a false advisory still tells the model
  something the operator never said.
- **Commas still do not end a negation.** *"Do not build, just run the tests"* still
  bans the tests. No corpus sentence did this, and a comma also separates the items of
  a real ban (*"do not build, test, or commit"*).
- **The population is narrow.** Most of it is agent-written briefs for one project,
  from one machine. The workstation's store is the natural cross-check, and
  `scanner_corpus.py --against` is how to run it there.
