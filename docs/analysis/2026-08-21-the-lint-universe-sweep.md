# The lint-universe sweep: what forty checks actually enumerate

**Date:** 2026-08-21 · **Milestone:** M511 · **Task:** ask every lint in the
smoke tier *what is in your universe, and does it match your header?* — after
two consecutive milestones found a green check covering less than it claimed. ·
**Outcome:** three more gaps, one of them carrying **24 live defects in
learner-facing graded tests**: eight capstone graders reject a correct answer on
any non-GNU `grep`, and two safety traps silently pass a solution that does the
forbidden thing. All fixed and proven two-sided. Plus a false failure produced
by my own measurement, recorded in §5.

---

## 0. Why sweep, and what question to ask

M508 found the reading guides' anchor count summing three series while each
index page presented it as its own. M510 found `config_keys_lint.sh` checking 71
of the 94 top-level config keys it claimed. Both were **green**. Both were found
by the same question, which is not the question a passing suite answers:

> *What exactly is in this check's universe, and is that what its header says?*

Two for two is a rate worth spending an afternoon on. Forty lints, prioritised
by the class both gaps belonged to: **a universe extracted from C source by a
regex.** That is where an omission hides, because the check keeps passing over
the smaller set.

## 1. What was audited

| Lint | Universe | Verdict |
| --- | --- | --- |
| `subcommands_lint` | dispatch sites in `main.c` | **gap** (§2) |
| `keys_lint` | `ch == N` sites in `jc_term.c` | **gap** (§3) |
| `posix_utils_lint` | shell files under `tests/`, `scripts/` | **gap, 24 live defects** (§4) |
| `tool_names_lint` | `struct jc_tool` definitions | clean — 43 objects, all in `src/tools/`, all matching the anchored pattern, plus 2 dynamic; 45 = 45 |
| `slash_commands_lint` + `builtin_cmds_lint` | `"/cmd"` literals in `jc_tui.c` | clean — 59 literals in the file, 59 in the universe |
| `telemetry_events_lint` | `telem(app, "x")` + `jc_eventlog_begin` | clean — every `telem(` call site in `src/` passes `app`; no other emitter shape exists |
| `session_fields_lint` | `struct jc_message` fields | clean — 7 fields, extraction gets 7, floor is 7 (exact) |
| `asset_keys_lint` | `jc_yaml_get*(..., "key")` per loader | clean — the `jc_yaml_seq_*` accessors take a node, not a key, so nothing is read outside the matched shape |
| `unit_orphans_lint` | test definitions vs declarations | clean — 161 = 161, both directions empty |
| `refs_lint` | `strncmp(text + s, "x:")` | clean — all 9 colon-forms in the file are in the universe |
| `notice_tags_lint` | bracketed tags | clean — it enumerates from **source** as well as from the doc |
| `config_defaults_lint` | `(def N)` annotations in the config header | complete today (20 of 20), **fragile**: the pattern admits `int\|long\|double` only, so a `size_t` or `unsigned` default annotation would escape. Left alone deliberately — widening the type class risks admitting non-field lines, and there is no defect to justify that today |
| `sprintf_lint` | `src/` + `include/` | clean, and the scope is right: `tests/tools/*.c` is first-party but not *shipped*, and carries no `sprintf` anyway |

**Not audited in depth:** the lints whose universe is a comparison between two
documents (`docs_index`, `docs_locators`, `changelog_coverage`,
`milestone_currency`, `doc_commands`, `examples`, `assignment_i18n`, `org_mode`,
`project_records`), the Makefile-shape checks (`portability_lint`,
`mincurl_recipe_lint`), and the presence checks (`harden_flags`,
`self_learner`, `sub_prompt`, `tool_caps`, `arena`, `license`, `snapshot`,
`docs_counts`). Their universes are directory globs and document sections rather
than regex-extracted name sets; the shell-script globs among them **were**
checked (§4). Stating the boundary rather than implying full coverage.

## 2. `subcommands_lint`: two levels, one and a half dispatch shapes

The header promises **TWO LEVELS** and says so in capitals, because the defect
that created the lint (`learn corrections`) was a second-level omission. Its
level-2 extraction was `strcmp((sub|verb), "x")` — two local variable names.

Enumerating **every** string-comparison target in `main.c` found a third:

```
args.pos[0]  51 literals   (covered)
sub          17            (covered)
verb         11            (covered)
cmd           4            (covered)
args.pos[1]   4            NOT COVERED
args->pos[1]  2            NOT COVERED
```

Five verbs live only there: `mcp call`, `constraints scan`, `context tools`,
`context history`, `checkpoints gc`. All five are in `--help` today, so nothing
was undiscoverable — the *universe* was missing, which is how the next one would
have got in unseen. Extraction widened (25 → 33 verbs, floor 20 → 25), with `-`
filtered because `export -` and `brief-check -` are an argument convention, not
verbs.

## 3. `keys_lint`: a map entry the extraction could never reach

`grep -ohE 'ch == [0-9]+\)'` — the trailing `\)` requires the comparison to
*end* its condition. So `ch == 127 || ch == 8` contributed only the 8, and byte
**127** — the DEL that most terminals actually send for Backspace, handled in
three places in `jc_term.c` — could never enter the universe.

The tell is in the lint itself: its byte→chord map has carried
`127) name="Backspace"` all along, **unreachable**. The author's intent was
right and the extraction could not deliver it. Bytes 1–31 were complete only
because each also appears in a parenthesised site: luck, not coverage.
Extraction now matches `ch == [0-9]+` (21 → 22 bytes, floor 22).

## 4. `posix_utils_lint`: the code we ask *other people* to run

This is the finding that mattered. The lint scans `tests/` and `scripts/` — the
code the project runs — for constructs that break on a BSD: `\b`, GNU BRE `\|`,
`head -c`, `grep -P`, `sed -i`, `\xNN`. Every one of those checks exists because
an OpenBSD or NetBSD row caught it (M461, M466, M471).

It did not scan the code the project asks **learners** to run: **79 shell
scripts under `docs/`** — every graded assignment's `test.sh`, plus the trace
capturers — and the fenced `sh` blocks of the three learner corpora (2,422 lines
in all). Measured there: **24 defects.**

| Class | Count | What happens on a BSD |
| --- | --- | --- |
| GNU BRE alternation in a plain `grep` | 10 | `grep -qi 'stack\|fold\|reduce'` searches for the **literal string** `stack\|fold\|reduce`. A learner whose `DESIGN.md` says "a fold" is **failed by a grader that is itself wrong** — in **eight capstone graders** (Racket, Guile, Elixir, Haskell, Clojure, Zig, C++, Rust), the final task of eight courses |
| GNU `\b` word boundaries | 14 | matches nothing. Two are **safety traps** — `\b(sprintf\|strcpy\|strcat\|gets)` and `\b(new\|delete\|malloc\|free)\b` — so a solution that *does* the forbidden thing **passes**: a false green on the exact check the task exists for. Others reject a correct answer (`45-haskell-loops-to-folds`, `71-process-session-notes`, `69-process-design`) |

One flagged line was **not** a defect and is worth recording: `grep -cE '\|.*'`
— inside an ERE, `\|` is the portable way to write a *literal* pipe. The tier's
own check already draws that distinction (it only flags `\|` when the grep is
not in `-E` mode); a first pass that ignored it would have "found" a defect in
correct code.

**Fixes.** The ten BRE cases become `-E` (byte-identical on GNU, correct
everywhere). The fourteen `\b` cases become the explicit class the tier itself
uses — `(^|[^A-Za-z0-9_])word([^A-Za-z0-9_]|$)`, which is exactly what `\b`
tests against — except `69-process-design`'s id extraction, where `-o` would
capture the boundary character; that one tokenises with
`tr -cs 'A-Za-z0-9_' '\n'` and anchors with `grep -x`, and the comment says why.
Two reading-guide commands (`fukabori-02`, `fukabori-04`) had the same `\|` and
are fixed too.

**Proof, both sides.** Old-versus-new comparison of all 22 changed graders
against their pristine fixtures: **21 byte-identical outputs and exit codes**;
the 22nd (`53-never-call-sprintf`) differs only in ASan addresses and PIDs and is
identical once those are normalised. Then the two-sided harness
(`tests/e2e/curriculum_graders.py`, every grader through `jichi grade`, pristine
must fail and reference must pass): **195 ok, 0 failures**, with every toolchain
present except `rustc`.

**The corpus is now part of the lint**, flattened once into `file:line:text` so a
hit still names its location, with markdown **prose** deliberately excluded — a
page explaining why `\b` is unportable has to be able to write it down, the same
reason comment lines are skipped throughout that lint.

## 5. The instrument, again: a cache that made a good grader look broken

The harness reported `58-zig-capstone.md solution accepted: wanted exit 0, got 1`
on the first run after the fixes. It looked like my change had broken a capstone.

It had not, and the sequence is worth writing down because it is the third
instrument failure in two days:

1. To prove the grader fixes were behaviour-preserving, I ran each **old** and
   **new** grader in copies of its task directory. For the Zig task that
   compiled the **pristine stub** `rpn.zig`.
2. Zig's build cache is shared (`~/.cache/zig`). The harness then ran the same
   task from a fresh temp dir with the **reference** implementation and hit the
   cached stub: `expected 5, found 0`.
3. The tell was in the error text — a stack frame naming
   `/tmp/graderdiff/58-zig-capstone.old/test_rpn.zig`, a directory the harness
   had never heard of.

Isolated to one variable: identical sources **fail** with the shared cache and
**pass** with a fresh one. (The first attempt at this changed two things at once
— the extraction method *and* the cache — and had to be redone; a measurement
whose conditions moved twice is not a measurement, which is
[ANECDOTES #63](../ANECDOTES.md)'s lesson arriving again.)

The harness now creates a private zig cache for its own run. A gate whose
verdict depends on what someone ran yesterday is not a gate.

## 6. What this says about the practice

Three gaps in the ~14 lints whose universe is regex-extracted from source, and
the shape is the same every time: **the extraction was pinned to an incidental
detail of how the code happens to be written** — a hardcoded reader function
(M510), a variable name (§2), a trailing parenthesis (§3), a directory that
happened to be where the project's own scripts live (§4). None of them was
wrong when written. All four became wrong when the code grew a second way of
doing the same thing.

The cheap defence is already in the tier and worth stating plainly: **every
extraction needs a floor, and the floor should be the count at the time of
writing, not a round number below it.** `session_fields_lint`'s floor is 7 with
7 fields — the tightest in the tier, and the one that would fail loudest. A
floor of 18 under 21 bytes (§3) leaves room for three chords to vanish
silently.

The expensive defence is this sweep, and its yield says it was worth running
once: 4 gaps, 24 live defects, in checks that were all green.
