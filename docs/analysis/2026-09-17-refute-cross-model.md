# Does the `refute` frame travel? Three more refuters, and the false-attack count (M641)

**Date:** 2026-09-17 · **Pre-registration:** [`proposals/2026-09-refute-ab.md`](../proposals/2026-09-refute-ab.md),
last section, fixed before the runs · **Baseline:** [`2026-09-17-refute-ab.md`](2026-09-17-refute-ab.md)
(`jlu/qwen3-coder-next`, 12 of 12) · **Harness:** `tests/bench/refute_ab/` · **Grader:** the person who
planted the claims, reading each attack against `src/util/` at the commit the reports were written
against (`fce98655`). All models free (`jlu/*`).

---

## Results

Same twelve reports, same twelve plants, refute arm only, one run per report, opaque ids, a clean
export of the source as the workspace. An **attack** is a non-planted claim the answer calls false,
wrong, unsupported or "not a defect"; a **false attack** is one on a claim that is true of the source.
A claim the answer concedes ("stands", "the fact is true, the framing is not") is not an attack.

| Refuter | Hits (of 12) | Attacks | False attacks | Mean wall | Read? |
|---|---|---|---|---|---|
| `jlu/qwen3-coder-next` (baseline, M637, this checkout, report as *prompt*) | **12** | 45 | **4** (9%) | 14 s | yes |
| `jlu/qwen3.8-27b` (report as *input*) | **9** | 46 | **0** | 172 s | yes; 3 answers hit the output ceiling |
| `jlu/gemma-4-26b-it` (report as *input*) | **11** | 51 | **2** (4%) | 8 s | 11 of 12; one "nothing found" in 1 s |
| `jlu/gpt-oss-20b` (report as *input*) | **9** | 46 | **1** (2%) | 9 s | 9 of 12; two "nothing found" in 1–2 s, one no verdict |
| `jlu/qwen3.8-27b`, `contextLength` 131072 (a labelled deviation) | **12** | 70 | **0** | 187 s | yes; every answer complete |

Every refuter clears both pre-registered bars: at least 6 of 12 hits, and fewer false attacks than
hits. **The frame travels, and on every model measured it found rather than attacked.**

Excluded from the table, kept on disk: the first cross-model pass ran the report in the refute
stage's *prompt* slot, as the baseline had, and `gemma-4-26b-it` and `gpt-oss-20b` answered "no
claim was provided" 12 of 12 times each in about a second (see §Harness). Those runs measured the
harness. `qwen3.8-27b` under the same slot scored 10 of 12, 57 attacks, 0 false — it read past the
empty claim, as the baseline model had.

## What the false attacks are

Seven false attacks across four models, and they are two claims:

1. **`jc_diff.c`, claim 2 — the degrade fallback in `lcs_middle` is wrong.** This is the one
   substantive true defect in the twelve reports: the fallback passes `0, mm + nn` and so emits no
   deletions for the old middle and reads `new_[p + j]` past the end of the array. The 27B upheld
   the claim both times it saw it and named the out-of-bounds read the report had missed;
   the baseline called it "partially true". **Gemma and gpt-oss both called it false** — "a valid,
   albeit non-optimal, diff", "does not corrupt the diff". Where the corpus had a real bug, the two
   smaller models were confidently wrong about it, in the direction the frame pushes.
2. **The small stuff.** The baseline's four: unchecked `fprintf` returns at three sites (true of the
   source; the "fclose will report it" reason is wrong too) and a dead store (`interp`) dismissed as
   "speculative". Gemma's second: the same dead store, rebutted with a false description of the
   control flow. The 27B conceded both facts and undercut only their severity, which is the right
   move and is not an attack.

So the rate is low everywhere, but what it is made of differs: the baseline's false attacks are
nits dismissed by category; the small models' one false attack each is the real defect.

## What the rate does not see

- **False acceptances.** A refuter that accepts a false claim is not counted here. The baseline
  accepted two: `jc_cli.c`'s "overflow" (a bounded `jc_snprintf`), and `jc_rss.c`'s claim 15 ("the
  `>=` should be `>`"), which it and Gemma both called a real bug -- the 131k run shows the proposed
  fix would overflow the neighbouring `closebuf` (2 + 22 + 1 = 25 bytes into 24), so the `>=` is
  right and the claim is false. Gemma accepted at least
  seven non-defects as valid (the documented post-colon space in `jc_auditview.c`; a `jc_size`
  versus `size_t` "mismatch" that is a typedef; a bounded 1200-byte path buffer called a
  vulnerability; two contract NULL checks), gpt-oss at least five, the 27B none — it concedes facts
  and disputes framing. Precision on attacks and precision on acceptances are different numbers,
  and the frame improves the first more than the second.
- **"Nothing found" as a way not to read.** Gemma's `jc_patch.c` and gpt-oss's `jc_progress.c` and
  `jc_rss.c` answers are the three headings each followed by "nothing found", produced in one or
  two seconds — no file was read. The frame permits "nothing found" so the model does not invent a
  defeater; three of 24 small-model answers used it to do no work. The 27B never did; its misses
  are the output ceiling.
- **Headings as decoration.** Gemma and gpt-oss regularly filed rebuttals under Undercutting or
  Stands, or wrote "nothing found" under a heading and then the rebuttal beneath it. The verdicts
  were usually right; the structure the frame asks for was not what carried them.
- **The output ceiling.** Three of the 27B's twelve answers are empty or stop mid-sentence: jichi
  derives `max_tokens` as a fifth of `contextLength`, the config declared none, and a thinking
  model spent the fifth on reasoning. The pre-registration said the cap would not be lowered; it
  did not say the default would be raised, so the table shows the misses as misses. The last row
  is the same model with a declared 131k context, labelled as the deviation it is: **12 of 12, 70
  attacks, 0 false** -- the three misses were the cap, not the frame, and with room to reason the
  27B is the strongest refuter measured.
- **An inverted-condition plant is guessable.** In the excluded prompt-slot pass, gpt-oss "found"
  `jc_proc.c`'s plant — `pipe(fds) == 0` — in one second, with a correct quote of `!= 0`, and no
  time to have read the file. A refuter told to disagree with `== 0` will say `!= 0`, and four of
  the twelve plants are of that kind. The other two kinds (a line that does not exist, a function
  that does not exist) require the file.

## Harness

- **The claim slot.** The refute stage reads its claim from the pipeline context; the harness had
  passed each report as the stage's *prompt*, so the frame said `--- the claim under review ---
  (empty)` with the report above it. The Qwen models read past that; Gemma and gpt-oss declined,
  correctly. The spec now carries the report as `input` (M641, a workflow feature), every seeded run
  above uses it, and the sealed record says which slot a run used. The baseline's 12 of 12 was
  measured under the prompt slot; the 27B's 10 of 12 under the same slot says the slot did not
  decide it for that family.
- **The workspace.** The first cross-model pass ran in this checkout, where `planted.tsv`, the
  reports and the baseline's graded answers sit; the 27B cited one. Everything in the table ran
  against a clean `git archive` of the source, and the sealed record carries the workspace and a
  contamination flag. The baseline ran with `planted.tsv` on disk; none of its answers mentions it,
  and the run did not log tool calls, so that stays a limit of the 12 of 12.
- `--model` refuses anything outside the free namespace before a request is built.

## Tested, not read (M642)

The three grading calls that decide the shape of this result were walked through with the
operator, one by one, and each was turned into a test where a test could reach it:

1. **`jc_diff.c` line 118, the degrade fallback** -- under the fault injector (`FAULT=1`,
   `JICHI_FAULT_ALLOC_AFTER=0`) the old fallback **segfaulted the unit suite** on the
   out-of-bounds read the 27B had described. `tests/test_diff.c` now asserts three deletions
   and two additions for a 3-versus-2 middle when the table allocation fails; the fallback is
   the bounds-safe trivial branch. The claim was true; Gemma's and gpt-oss's rebuttals stay
   counted as false attacks.
2. **`jc_progress.c` lines 171, 211, 296** -- with the progress file a symlink to `/dev/full`,
   `grade --record` printed nothing and recorded nothing while the function answered success.
   `tests/test_progress.c` and `tests/smoke/progress_write_fails.sh` now demand `JC_ERR_IO`
   and the caller's "could not append" line, with the grade's exit code unchanged. The claims
   were true; the baseline's dismissal stays counted as three false attacks.
3. **`jc_rss.c` line 76** -- no test can reach the guard: `field()` is static and every tag
   jichi passes is at most 11 characters. The bound is now written for the buffer it is exact
   for (`strlen(tag) + 3 > RSS_TAG_BUF`, with `open` and `closebuf` sharing the named size) and
   the comment says why `>` would overflow `closebuf`. This one is tested by reading only, and
   the page says so.

Two of the three were product defects. The measurement of the refuters found them because the
first seat had reported them and the refuters had disagreed about them; neither would have been
looked at otherwise.

## What this does not say

- Two families beyond Qwen, one size each; no frontier model, by rule.
- The grader planted the claims. A false attack is easier to judge than a blind arm — the source
  has the property or does not — and the two disputed cases are named above so a second reader can
  check them.
- Attacks are counted per claim as an answer addresses them; a blanket dismissal of ten claims
  weighs ten. Claims a report had already retracted ("no defect here") still count as attacks when
  rebutted, which inflates every denominator by a handful.
- Nothing about a different first seat: the reports remain one model's prose, and the one real
  defect in them is the one the small models attacked.
