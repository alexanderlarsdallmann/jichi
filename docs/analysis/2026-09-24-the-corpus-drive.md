# The corpus drive: 48 runs on zigodot, and where D1's note belongs

*Analysis, 2026-09-24 (M731). The full drive that plan D1's step 0 asked for
([`plans/2026-09-after-m712.md`](../plans/2026-09-after-m712.md) §2), run on threadwork
on 2026-09-23 on the M714 build and read the day after. It follows the pilot
([`2026-09-23-the-corpus-pilot.md`](2026-09-23-the-corpus-pilot.md)). Written for the
developer who builds D1 and for a self-learner who wants to see a threshold fitted
from evidence rather than chosen. Every number names what produced it; the data
lives outside the tree, in `~/.cache/jichi-corpus/2026-09-23-zigodot` on threadwork
(telemetry at the `full` tier, one journal, one output stream and one diff per run),
and this page can be read without it.*

---

## 0. The answer first

1. **D1's note belongs at three.** Every repeat of a successful call that the reading
   below classifies as futile reaches three identical calls with nothing changed in
   between (35, 13, 5 and 4 of them), and no productive turn in either model's runs
   reaches more than two. That is M432's shape exactly — *"fire on every measured
   loop while staying clear of the 2x tail"* — and it is **provisional until the
   workstation's 500 real turns are read the same way** (§4).
2. **The stop half cannot be fitted here.** 47 usable turns is under the script's
   own 50-turn floor, and three loops are not a distribution. The plan's rule for
   that case applies: the milestone ships the note only.
3. **One of the three capped runs is invisible to any repeat detector.** It wandered
   for 202 calls and never made the same call four times (§5).
4. **Reading the runs found three recording defects that no count would have**
   (§9): the journal records an interrupted run as `done` and `running`; an
   LM Studio context overflow came back as the run's answer, marked `done` and `ok`,
   after a request 14 % over the window the config declared; and `search_code`
   drops grep's warning that a pattern is malformed.
5. **M714's `search_code` fix held in the field** (§7): of the drive's no-match
   searches that can be re-checked, 96 of 96 were true. The pilot, before the fix,
   had 11 false in 45.
6. **DEFERRED item 7 gets three more anecdotes, none of them an answer** (§6), and
   they are the case M715 recorded answer *bytes* for: two capped runs left 196 and
   226 bytes of mid-thought.
7. **One run is spoiled, and I spoiled it** (§10).

---

## 1. The drive

| | |
|---|---|
| **When** | 2026-09-23, 12:43:29 → 16:17:26, both arms in parallel |
| **Build** | jichi `8e68d13c` (M714), pinned outside the tree; driver `scripts/corpus-drive.sh` at `0076720a` |
| **Workspace** | a clone of zigodot per arm, reset to `70801ac` before every task; the operator's checkout never touched |
| **Tasks** | 24 per arm, 14 read-only and 10 that edit; `tasks/full.list` |
| **Models** | `jlu/qwen3-coder-next` on the HRZ gateway (free); `prism-ml/bonsai-27b` in LM Studio, `contextLength` 65,536 declared |
| **Verifier** | `zig build test && zig build gate-lint` |
| **Invocation** | `--auto --no-route --no-session --quiet --lease fail`, with an edit scope — every run a headless one-shot |
| **Fences / caps** | fences on (edit scope, lease); caps off, except a 3,600 s wall-clock limit per run (`gnutimeout`, SIGINT, then SIGKILL after 60 s) |
| **Recording** | telemetry at the `full` tier (whole arguments and result text); one journal per run |

**The runs, from the journals:**

| | qwen | bonsai |
|---|---|---|
| runs | 24 | 24 |
| `stop_reason` | 24 `done` | 21 `done`, **3 `max_iters`** (08, 12, 24) |
| tool calls | 623 | 1,122 |
| tokens | 43,494,491 | 49,625,360 |
| edit tasks that reached the verifier | **10 of 10**, every one exit 0, `sanity: no_count` | **4 of 10**, every one exit 0, `sanity: no_count` |
| M432 failure-loop events | 2 (task 12) | 9 (tasks 03, 04, 06 ×2, 09, 12 ×3, 21) |

The six bonsai edit tasks that never verified: three capped (08, 12, 24), one that
edited nothing (14), one overflow (18), and the spoiled one (16) — each read in §2.

## 2. Reading the runs before counting them

A count is only as good as the population it is taken over, so every run whose
numbers looked unusual was read first.

- **bonsai 16 — spoiled, excluded.** Two of its model calls failed at 14:49:19 and
  14:50:06 with *"Context size has been exceeded"*: my own probe was sending requests
  to the same LM Studio instance at those seconds (§10). The run then reached the
  drive's 3,600 s limit. **47 turns remain.**
- **bonsai 18 — an overflow, recorded as an answer.** The model read `codegen.zig`
  twice; the next request, **74,866 tokens** by the server's count, exceeded
  LM Studio's 65,536-token window — the window the config *declared*. LM Studio
  returned its error as content, and the run's final "answer" is that error text,
  with `stop_reason: done` and journal `outcome: ok` (§9).
- **bonsai 14 — an edit task that edited nothing.** Fifteen calls, thirteen of them
  `search_code`, an empty diff, and a last message that is a statement of intent —
  *"Let me search more specifically for patterns that look like loop bounds or
  limits:"* — with no tool call attached, so the loop took it as the final answer.
  `done` in the table; stopped mid-task in fact.
- **qwen 12 — the deliverable is absent.** The task: *write a test that pins down*
  what the VM does on integer division by zero. The final diff is one line, an unused
  `DivisionByZero` added to an error set; the answer calls the absence a virtue
  (*"Complete: No debug code or test files left behind"*) after 131 calls and 19 test
  edits. The verifier passed (§8).

## 3. Repeats — the measurement, and the reading

```sh
python3 tests/measure/success_repeats.py <arm>/telemetry.jsonl --min-turns 1 --per-turn FILE
```

Keyed on the whole arguments and a hash of the result (every event in this drive is
at the `full` tier); `--per-turn` is new in this milestone and writes one row per
turn, joinable to the journal on `run`. `corpus_filter.py` (M720) found no synthetic
session in either arm.

| | qwen (24 turns) | bonsai (24 turns) |
|---|---|---|
| a turn repeats one successful call ≥ 3 times, **nothing changed between** | 0 | 5 |
| ≥ 5 | 0 | 3 |
| ≥ 10 | 0 | 2 |

**Every turn with an unchanged repeat of three or more, read:**

| turn | stop | calls | unchanged | the repeated call, and what it was |
|---|---|---|---|---|
| bonsai 19 | `done` | 82 | **35** | `search_code` for `error\.Err\|error\.Error` in `parser.zig`, answered `(no matches)` 35 times **in a row** (calls 41–75). The answer was true: the file uses specific names such as `error.ExpectedRightParenInCall`, never `error.Err` or `error.Error` — checked with `grep -E` on `70801ac`. The loop is the model's. |
| bonsai 08 | `max_iters` | 200 | **13** | `search_code` with a pattern in Perl's dialect (`\s`, `(?:…)`, `\n`); grep warned *"? at start of expression"*, the tool dropped the warning, and the model searched again — eleven of the thirteen consecutively (calls 171–181). |
| bonsai 24 | `max_iters` | 200 | **5** | `search_code` for `exit\|panic` in `vm.zig`, a real result, asked for again at calls 31, 45, 46, 54 and 76 with nothing edited between. |
| bonsai 06 | `done` | 125 | **4** | `zig build test 2>&1 \| tail -5`, the same failing build output three times in a row (calls 113–115) and once more at 123; the turn later recovered and verified. |
| bonsai 12 | `max_iters` | 202 | 3 | the same `sed -i` command three times. The first changed the file and the next two changed nothing, so inside the loop — where a shell command that changed the tree resets the count (M689) — this is two, not three. |

**The tail, read the same way.** Every other turn in both arms peaks at two, and each
two is benign: a second run of the gate (`zig build test && zig build gate-lint`,
qwen 02), a duplicated search (qwen 18, bonsai 21), a second `list_files` (bonsai
09), a second `git_diff` (bonsai 22). Re-running the gate *after an edit* does not
appear here at all, because a successful edit resets the count — which is the reset
doing its job.

## 4. The fit

**The note: three identical successful calls — same tool, same whole arguments, same
result — with nothing changed in between.** Reset by a successful mutating tool call,
and by a shell command that changed the tree.

- It fires on **every** futile repeat measured here: bonsai 19 at its third search
  (call 43, with 32 identical calls still to come), 08 at call 171, 24 at call 46,
  06 at call 115.
- It fires on **no** productive turn: the legitimate tail stops at two, in both models.
- The margin is one, and it is the same margin M432 chose for failures (exact
  repeats at three, against a tail of two).

**Provisional until the cross-check.** The workstation holds the larger corpus
(plan §2: 500 real turns after the M720 filter, 20 of them — 4.0 % — at ≥ 5
unchanged, with a tail of 163, 67, 50 and 13). The fit stands if **no productive
turn there reaches three**; otherwise the note moves to one above the largest
productive value. The procedure, for that machine:

```sh
python3 tests/measure/success_repeats.py --since 2026-08-14 --per-turn /tmp/ws-turns.tsv
# then read every row with unchanged_max >= 3, as §3 does, and classify it
```

**The stop is not fitted.** Loops seen so far: 35 and 13 here, 51 in the pilot (on the
build before M714), 163, 67, 50 and 13 on the workstation. A stop at ten would end all
but bonsai 24's five — but its exit code is `DEFERRED.md` item 7's question, which is
the operator's, and 47 turns print `NOT EVIDENCE`. The note ships alone, as the plan
says it should in exactly this case.

## 5. What a repeat detector cannot see

bonsai 12 made 202 calls and reached the cap without ever making one call four times:
it read, searched, edited with `sed`, ran the build, and read again, each step a
little different. Offline its peak is three; inside the loop it would be two. A
no-progress detector keyed on identical calls is blind to this by construction. What
*would* see it is a different signal — the same files read again with no change
between, or a budget — and that is a separate design, recorded in `DEFERRED.md`
rather than folded into D1.

## 6. DEFERRED item 7 — three capped one-shots, no answer

| run | final text |
|---|---|
| bonsai 08 | **0 bytes** |
| bonsai 12 | 196 bytes — *"Looking at `src/gdscript/division_by_zero_test.zig`, I can already see that someone has documented this behavior. But let me verify the actu…"* |
| bonsai 24 | 226 bytes — *"I see that the `@panic` handling is already in place for many utility call sites. Now I need to add the actual VM opcode handler…"* |

With the pilot's two, that is **five capped one-shots, none of which answered** — and
two of them left text. A journal field that said `answered: true` for non-empty text
would have counted both; M715 records `answer_bytes` for exactly this reason. Five is
not the floor of twenty, and these journals predate M715's fields (the text was read
from each run's output stream), so the row stays open: it needs a drive on a build from
M715 on.

## 7. `search_code`, in the field

543 `search_code` calls; **340 (63 %) answered `(no matches)`**. The 96 of those made
during read-only tasks — whose workspace was never edited, so it equals `70801ac` —
were re-run with `grep -rnIE` against that commit: **0 false negatives**. The other 244
were made while an edit task was changing its workspace and cannot be re-checked from
the base commit. So the dialect fix of M714 holds where it can be measured; the high
no-match rate is the models searching for what is not there, which §3 shows at its
most expensive.

## 8. The verifier's blind spot

All fourteen edit verifications that ran reported `sanity: no_count`: zigodot's gate
prints its test count as `gate_lint: N test blocks run` (`tests/gate_lint.py:292` in
zigodot), and jichi's test-output parser reads JUnit-XML, TAP and a failure heuristic
(`include/jc_testparse.h`), none of which that line is. So jichi could not compare the
count before and after a run, and a task whose deliverable was a test — qwen 12 —
passed with no test added. The pilot found the mirror image (a test file *weakened*
under a green gate). The design question — a gate that states its own count format,
or a recognised pattern for lines like this one — is recorded in `DEFERRED.md` and not
decided here.

## 9. Three recording defects, found by reading

1. **The journal calls an interrupted run `done`.** bonsai 16's output stream says
   `"aborted":true, "stop_reason":"interrupted"`; its journal's `end` says
   `"outcome":"running", "stop_reason":"done"`. The end event passes `JC_OK` to the
   stop-reason classifier, on the stated assumption that *"reaching this line means
   the loop returned normally"* — which an interrupt does. The journal is the record
   the measurement scripts read, so this puts interrupted runs into the `done`
   population silently.
2. **An overflow came back as the answer.** bonsai 18's request exceeded a window the
   config had declared, by the server's count (74,866 against 65,536); LM Studio sent
   the error as content; jichi recorded it as the run's answer, `done`, `ok`. The M73
   hint exists for exactly this, and this run cannot say whether it fired: the drive
   runs `--quiet` (`scripts/corpus-drive.sh:217`), which suppresses it by design. What
   the code does say is that it *would not* have fired — its signatures
   (`jc_text_is_context_overflow`, `src/util/jc_cli.c`) do not include LM Studio's
   wording, *"exceeds the available context size"*. Why jichi's own estimate let a
   request that large through is not established here.
3. **`search_code` drops grep's warning about the pattern.** On the no-match path the
   tool discards grep's standard error (so a stray warning never reads as a match) and
   probes the pattern only when grep *fails* (exit 2). A pattern grep accepts with a
   warning, exit 1, therefore comes back as a bare `(no matches)`. bonsai 08 repeated
   such a search thirteen times.

Each is small, and each is recorded in `DEFERRED.md` with its reproduction, so that
the milestones that fix them start from a failing check. *(M732, the same day, fixed
the first and the third and the second's signature, check first; the second's other
two parts stay open.)*

## 10. What I did to this corpus, kept

While checking the commands of `docs/VERIFY_A_PLATFORM.md` on 2026-09-23, I sent two
requests to the LM Studio instance this drive was using. Both of mine failed with
*"Context size has been exceeded"*, and so did two of the drive's own calls, in bonsai
16, at the same seconds. A local model server shares its memory between requests; it
was part of the experiment, and nothing else should have spoken to it. The run is
excluded from every count above. The rule is now rule 3 of `VERIFY_A_PLATFORM.md`.

## 11. What this page does not claim

- **A rate.** 47 turns is under the 50-turn floor; every percentage above describes
  this drive and nothing wider.
- **Generality.** One codebase, two models, one day, one build (M714, before D6's
  journal fields).
- **That the note will work.** The fit says where it would fire; whether a model heeds
  it is measured only once it exists.
- **The workstation's side.** §4's cross-check has not been run; until it has, three is
  a fit on one corpus.
