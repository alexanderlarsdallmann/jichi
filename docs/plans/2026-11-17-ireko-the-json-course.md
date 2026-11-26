# Ireko: the reading course for jichi's JSON layer (design, and the review record)

*2026-11-17. The operator: "Please, proceed with the JSON course!" It is the first
course of [the reading-courses plan](2026-11-17-reading-courses.md), section 3.1. The
course will be `docs/reading/IREKO.md` and its chapters. This page records how it is
made and what its review finds, so a reader can judge it.*

The name, which the operator confirmed on the same day, is 入れ子（いれこ）*ireko*,
"nested boxes": a set of boxes that fit one inside the next. A JSON document is exactly
that, and the parser that reads it calls itself once for every box it opens. The other
name considered, 解読（かいどく）*kaidoku*, "decoding", is kept for a later course.

The name was also put to a local Japanese model, `llm-jp-4-8b-thinking`, as the operator
asked. It found 入れ子 natural and fitting for nested objects and arrays read by a
recursive-descent parser, and advised naming the subject beside it, which the index page's
title does ("the JSON layer"). Its three alternatives, 階層解析, ネスト探索 and 再帰読破,
are technical compounds unlike the other courses' single words, and it gave wrong readings
for two of them (ねすたんさく for *nesuto tansaku*, さくれきどっか for *saiki dokuha*), so they were
read and not adopted. For 解読 it named protocol decoders among the fitting subjects, which
makes it a candidate for the protocol clients' course (section 3.5 of the plan).

## 1. Scope and shape

The course reads about 2,000 lines of C (1,997 at `81f9d35c` for the six files below; 2,065 with `include/jc_jsonrepair.h` and `include/jc_jsonc.h`, which chapter 7 also quotes):
- `src/json/cJSON.c` and `src/json/cJSON.h`: the parser, the tree and the printer;
- `src/json/jc_json.c` and `include/jc_json.h`: the helpers that read values out of a
  tree;
- `src/util/jc_jsonrepair.c`: the repair of a model's nearly-JSON;
- `src/convert/jc_jsonc.c`: JSON with comments, for configuration files.

There are seven chapters:

| # | Chapter | Reads | Incidents it tells |
|---|---|---|---|
| 1 | Everything passes through here | where jichi uses JSON; the `cJSON` struct; who owns a tree | M171 (the library was never third-party), M188 (the tree compiles as C++); read for the chapter: the "replace as a pair" promise that M1078, M1085, M1094 and M1113 broke |
| 2 | Boxes inside boxes: the recursive-descent parser | `cJSON_Parse` and the `parse_*` functions; looking a key up | M472 (the input chose the recursion depth), M1078 (trailing content), M1089 and M1094 (the node cap), M1113 (duplicate keys) |
| 3 | Strings | `parse_string`, `\u` escapes and UTF-8; how a string is printed | M725 (a string costs its own length), M700 (an earlier profile); measured for the chapter: a lone surrogate, a raw control byte and an unknown escape are accepted |
| 4 | Numbers | `parse_number`, the `int` view and its clamp; printing a `double` so it reads back the same | M469 and M470 (the sanitizer was there, the input was not), M866 and M1077, M1076, the armeb failures |
| 5 | Building and printing | the `Create`/`Add` functions, appending, the print buffer | M1084 and M1085 (the fast paths and the print parity test); two limits measured for the chapter |
| 6 | Who wrote this JSON? | `jc_json.c`: strict and lenient reading | M168 and M287 (numbers), M519, M530, M534, M843 (booleans); measured for the chapter: what is left |
| 7 | When the JSON is broken | `jc_jsonrepair.c` and `jc_jsonc.c` | M148 (a conservative repair), M334 and M858 (a cut-off reply repaired into a smaller object), M353 (the repaired call tells its author), M459 and M924 (the config reader, then the writer, read comments); measured for the chapter: the quote swap reaches inside strings |

The house style is [Annai](../reading/ANNAI.md) for the level and
[Fukabori](../reading/FUKABORI.md) for the honesty about trade-offs, as in
[Tazuna](2026-11-13-tazuna-the-harness-course.md). Each chapter has the same sections:
- **Why this exists**
- **The shape**
- the walk through the code
- **Try it**
- **What went wrong here, and what it teaches**
- **Check yourself**

Most of the exercises compile a short C program against `src/json/cJSON.c`, which
needs nothing but the C library. A reader can therefore try the parser without building
jichi at all.

## 2. The rules every chapter is held to

The same as Tazuna's (its plan, section 2):
- anchors are `file.c:function`, and line numbers are banned
  (`tests/smoke/reading_refs_lint.sh`);
- every quoted block is tagged with its source and matches it line for line
  (`tests/smoke/reading_quotes_lint.sh`);
- every published command was run first, in the published form;
- nothing is claimed that was not read in the code or measured;
- incidents are told only from the tree's own records, cited by milestone;
- there are no links into `docs/internal/`, and no machine names or paths.

## 3. How it is written

1. **Drafts.** Four Claude agents each draft from one shared brief, each in its own
   scratch worktree of `81f9d35c`: chapters 1 and 2; 3 and 4; 5 and 6; and 7 with the
   course's index page. Each runs its "Try it" commands in throwaway directories and
   reports the lints' last lines, every command with its exit code, and the claims it
   could not verify.
2. **Review.** The supervising session reviews each chapter against the code, re-runs
   every published command, runs the reading lints and the other documentation lints
   over the whole course, and joins the course to the other guides.

## 4. Defects the writing found in the code

Every item here was reproduced by the supervisor, or confirmed in the code by the supervisor
and measured by a drafter or a reviewer; the last column says which. None is fixed on this
branch: the code belongs to the team, and each item goes to it as a card with the handover.

### 4a. Defects that change a value, a verdict or memory

| # | Where | What happens | How it was checked |
|---|---|---|---|
| 1 | `src/util/jc_jsonrepair.c:pass_quotes` | The single-to-double quote swap does not track strings, so an escaped apostrophe changes the value: `{'msg': 'it\'s'}` is repaired to `{"msg": "it\"s"}`, which parses as `it"s`. The header says every pass leaves string content untouched. | reproduced by the supervisor (chapter 7) |
| 2 | `src/json/cJSON.c:pb_put_number` | A finite number whose magnitude is above 1e300 prints as `null`: `[1e301]` round-trips to `[null]`. The comment speaks only of NaN and infinity; `<float.h>`'s `DBL_MAX` would give an exact test. | reproduced by the supervisor (chapter 4) |
| 3 | `src/json/cJSON.c:parse_number` | Number text that is not JSON is accepted and the part `strtod` did not read is dropped: `{"offset":10-5}` reads 10, `{"a":-}` reads 0, `[1e5e5]` reads 100000. | reproduced by the supervisor (chapter 4) |
| 4 | `src/json/cJSON.c:cJSON_ReplaceItemInObject` | It replaces the **first** member with a key, while every read returns the **last** (M1113). Through jichi: a project's override of a key the global config holds twice is lost (`jc_config_merge_json`); `config set` on such a key reports success and changes nothing a read sees; `jc_tool_unstring_args` reports a fix the read does not see. | reproduced by the supervisor; driven through the binary by a reviewer (chapters 2 and 5) |
| 5 | `src/config/jc_config_trust.c:detach` | Removing the last member of an object does not update its cached `tail`, and `cJSON_AddItemToArray`'s guard cannot see a detached tail, so a later append reads and writes freed memory (AddressSanitizer: heap-use-after-free; valgrind through the real `jc_cfgtrust_filter`). Latent: no append follows today. | reproduced by the supervisor on a small program; through the real filter by a reviewer (chapter 5) |
| 6 | `src/chat/jc_agent.c:call_out_of_scope_path` | The edit-scope fence normalises a tool call's arguments with no `app`, so M334's cut-off check does not run there: a reply cut off at the model's output limit is repaired and refused as "outside --edit-scope" before the executor can say it was cut off, and no `args_truncated` event is counted. | confirmed in the code by the supervisor; measured by a reviewer with a scripted cut-off reply (chapter 7) |
| 7 | `src/config/jc_config.c:config_load_impl` | A typo on `"pathFence"` (or `"selfReview"`) turns the key **off**, where leaving it out means auto: a presence test, then a lenient reader with a fallback of 0. `doctor --unattended` reports it as "explicitly off". The comments in `include/jc_str.h` and `src/tools/tool_util.c:tu_arg_bool` promise that a typo can never flip a fence. Decided on 2026-11-17: the reading fails closed and the report is loud ([`DECISIONS.md`](../DECISIONS.md#dec-2026-11-17-unreadable-fence)); the fix is [def-415](../DEFERRED.md#def-415), not yet built. | reproduced by the supervisor and a reviewer with `doctor` (chapter 6) |

### 4b. Smaller defects

- `src/json/cJSON.c:parse_number`: a valid number of 64 or more characters makes the whole
  document refused (its 64-byte buffer). RFC 8259 allows a limit; nothing says this one.
- `src/json/cJSON.c:cJSON_Parse`: `cJSON_Parse(NULL)` leaves the node-cap flag from the call
  before, while the header says the flag describes the most recent call.
- `src/util/jc_telemetry.c`: the summary's "argument repair ok=X/Y" counts all four kinds of
  `args_repair` event (`json`, `unwrap`, `unstring`, `elided_placeholder`), though its comment
  says it counts M148's repair.
- `src/tools/tool_util.c:tu_arg_int` with `json_str_to_double`: a model's `{"limit":"nan"}` reads
  0, which `read_file` takes as no limit; `"inf"` reads 2147483647.
- `src/json/cJSON.c:parse_string`: a lone surrogate, a raw control byte, an unknown escape and
  bytes that are not UTF-8 are all accepted. RFC 8259 allows a parser to accept more than JSON;
  the lone surrogate reaches a tool unchanged (`write_file` writes the bytes `ed a0 bd`), while
  the history and the next request are sanitised. A decision, not yet made.

### 4c. Comments and lints that no longer say what the code does

- The provenance comment atop `src/json/cJSON.c` ("can replace this file and cJSON.h as a pair
  without touching anything else in the tree", "struct layout ... match upstream"): no longer true
  since M1085, M1094 and M1113.
- In `src/json/cJSON.c`: "a malloc'd 64-byte node" (72 since M1085); "five return points"
  (`parse_object` has seven); "(150 KB of '[')" (counts both brackets); `pb_put_number`'s claim
  about `%.0f` above 2^53 (glibc prints it exactly; the standard only asks for correct rounding up
  to `DECIMAL_DIG` digits). The M909 in the OpenAI reader's comment is M914 in the ROADMAP.
- `include/jc_json.h` and `src/json/jc_json.c` cite `cJSON.c:282` (a line number, stale); the
  lenient reader's comment omits `on`/`off` (M534) and models (M530).
- `include/jc_tool.h` names the telemetry kind `"repair"`; `jc_tool.c` writes `"json"`.
  `tests/test_tool.c:test_args_repair_execute` mentions a Python-literal case it does not have.
  `src/chat/jc_agent.c` includes `jc_jsonrepair.h` and uses nothing from it.
- `tests/smoke/config_bool_lint.sh` labels `pathFence` "a default-OFF key"; its default is auto.
- `tests/smoke/json_cast_lint.sh` misses casts to other types (`size_t`, `unsigned`, `short`),
  casts with an extra parenthesis, and casts in two steps (the token counts in `jc_agent.c`;
  `docs/DEFERRED.md` def-378 holds the class).
- `tests/smoke/reading_quotes_lint.sh` check 5 passes a block's first line through `awk -v`, which
  reads backslash escapes, so a correct tag on a block whose first line holds a backslash is
  reported as wrong (reported by a drafter, from the awk rule; not re-run).
- `tests/smoke/reading_refs_lint.sh` check 4's anchor pattern has no capital letters, so it would
  not count anchors in `cJSON.c` if this course were added to its series list.
- No lint checks Markdown links in `docs/reading/` or `docs/plans/`; the supervisor checked this
  course's 51 links with a script and proved the script flags a missing file and a missing anchor.

## 5. Review record

**State on 2026-11-17: all seven chapters, the index and this page reviewed against the code.**

**How.** Four fresh agents reviewed the drafts, two chapters each and the last one chapter 7,
the index and section 1 of this page. None had drafted what it reviewed. They read the code, not
the comments; checked every milestone against `docs/ROADMAP.md`, `docs/ANECDOTES.md`,
`docs/DECISIONS.md` and `git log`; checked the statements about JSON against RFC 8259 and about C
against the C99 text (the C89 section numbers could not be checked against a C89 text); and
reproduced every published command and every defect claim. Two compared behaviour with upstream
cJSON 1.7.17 as a Linux distribution ships it. They changed nothing and reported each finding with
the passage, the evidence and an exact replacement.

**What the review found.** About 940 checkable claims were checked: about 235 in chapters 1 and 2,
200 in chapters 3 and 4, 235 in chapters 5 and 6, and 265 in chapter 7, the index and this page.
87 findings came back: 25 statements that were wrong, and the rest missing context, a missing
implication, an ambiguous sentence, or an attribution that the cited record did not support. Among
the wrong ones: the bits a surrogate half carries; "the double nearest to 2^53 + 1" (it lies
halfway, and the tie goes to even); M469 found "by a different computer" (a different compiler on
the same workstation); M1083 counted as JSON work; a quote given to a header that does not contain
it; "five return points" for both container functions. The supervisor checked the evidence of each
finding against the tree before applying it, and applied all 87, six of them reworded: two measurements that only a reviewer made are told as
the review's, not as the chapter's own; a third is told from the code, with its measurement
recorded in section 4; a claim that rests on an internal note is told without a link; a
supervisor label is left out; and one cross reference now names the chapter it means. The review also found defects 4, 6 and 7 of
section 4a in full, and most of 4b and 4c.

**What every published command is.** Every command block was run in its published form, from the
checkout root, once by the drafter, once by a reviewer and twice by the supervisor's runner (before
and after the review), which compares every output block with what the run printed. Every output
matches. The only blocks that differ are diagrams, which are not output, and valgrind's process
ids, which the chapter says will differ. Before the handover, on 2026-11-18, the runner ran every
block a third time, on the team's head over master `8c51afe5`: the JSON code had not changed, and
two counts had grown (chapter 1's file counts, 215/98 to 217/100, and chapter 4's lint scan, 231 to
233 files), because M1196 and M1198 added two C files. Both were updated.

**The lints.** `reading_refs_lint`, `reading_quotes_lint`, `docs_locators_lint`, `docs_flags` and
the other documentation drivers of the docs gate pass on the reviewed course. `docs_flags` had read
an arrow in chapter 1's diagram, drawn with two hyphens on each side of the word *next*, as an
unknown command-line flag; the arrows are now drawn with `=` signs.
