# Breaking down the system prompt (M312)

*Design note written before implementation, per the M299 craft rule.*

---

## The problem

M310 and M311 between them made the system prompt the largest measured thing in a
call — **~15,200 tokens** of an 18,300-token prefix on this repository, and **~3,952**
of a graded `attempt`'s ~4,900 even with the rules file skipped. And it is the one
component the report cannot explain:

```
  system prompt     ~15196
    of which: rules ~10442, repo map ~3978, memory ~0, glossary ~0, skills ~0, output style ~0
```

Six named sub-parts, summing to ~14,420, against a stated ~15,196. COMPACTION.md is
honest that they do not sum ("the base persona + section headers are the remainder"),
but that honesty is the defect: **the remainder is where the answer lives** for anyone
whose rules file is small. On a graded attempt — rules skipped, no repo map — every
named part is zero and the whole 3,952 tokens is "remainder".

There is a second, quieter error. The line measures `app->rules`, the **raw** string,
while `jc_sysmsg_build` includes it through `append_capped` with an M73 fit cap. When
the cap bites, the report describes text that is not in the prompt.

## The two ways to do this

**A. Add more named sub-parts, each measured by re-rendering that piece.** This is what
the current line does, extended. Cheap to write, and wrong in the same way: it is a
*second* description of the prompt, maintained by hand, which drifts the moment a
section is added to the builder and not to the reporter. M311 was exactly this bug one
level up — two copies of an asset load — and the fix was to have one.

**B. The builder reports what it built.** `jc_sysmsg_build` records each section's byte
count as it appends, into an optional out-parameter. The numbers are then *the same
bytes* the model receives, capping included, by construction.

**Decision: B.** With the property that makes it stay true:

> **The recorded parts must sum to the built prompt's length**, and a unit test asserts
> it across configurations (persona override, craft off, plan/auto mode, assignment
> loaded, tutor stance, board on).

A section appended without being marked makes the sum less than the total and the test
fails. That is the difference between a breakdown that is currently right and one that
cannot quietly stop being right — the house rule, prefer a lint to an audit, applied to
a data structure instead of a grep.

### Correction, after trying to break it

**The paragraph above is too confident, and testing it is what showed that.** I inserted
an unmarked section before the skills catalog to watch the sum check go red. It stayed
green: bytes appended between two marks are charged to the *following* slot, so the sum
still balances. `sum == total` only catches an append after the **last** mark.

So a second check carries the weight: **in a minimal configuration the twelve optional
slots must be exactly zero.** An unmarked append anywhere upstream of an inactive slot
lands in it and turns a `0` positive — which does catch the insertion above. With both
checks, the simulated section fails the suite.

What remains uncaught: an insertion between two *active* slots, credited to a
neighbouring section. That is a misattribution rather than an unexplained remainder — a
smaller error than the one being fixed, but real, and now written down in
`jc_sysmsg.h` rather than left as an implied guarantee. The alternative that would close
it — a writer object where every `append` names its part, so a section *cannot* be
appended anonymously — is a ~40-site rewrite of the most cache-sensitive code in the
project, and is not worth it to convert a misattribution into a compile error. Recorded
as the option, not taken.

## Shape

```c
enum jc_sysmsg_part {          /* prompt order */
    JC_SYSPART_PERSONA, JC_SYSPART_CRAFT, JC_SYSPART_SAFETY,
    JC_SYSPART_ENV, JC_SYSPART_EXTRA, JC_SYSPART_LANGUAGE,
    JC_SYSPART_STYLE, JC_SYSPART_RULES, JC_SYSPART_DESIGN,
    JC_SYSPART_CONSTRAINTS, JC_SYSPART_MEMORY, JC_SYSPART_GLOSSARY,
    JC_SYSPART_BOARD, JC_SYSPART_REPOMAP, JC_SYSPART_ASSIGNMENT,
    JC_SYSPART_SKILLS, JC_SYSPART_COUNT
};
struct jc_sysmsg_parts { jc_size bytes[JC_SYSPART_COUNT]; jc_size total; };

char *jc_sysmsg_build_parts(struct jc_app *app, struct jc_sysmsg_parts *parts);
/* jc_sysmsg_build(app) == jc_sysmsg_build_parts(app, NULL) */
```

`ENV` deliberately absorbs the environment block, the mode line, the read-only note, the
plan/auto addendum and the verify-gate note: they are one contiguous "your situation"
region, none of them individually actionable, and a user cannot shrink them separately.
`ASSIGNMENT` likewise covers the authoring nudge, the solving block and the tutor stance
— at most one is ever present.

Byte→token conversion goes through a new pure `jc_compact_estimate_bytes(n)` /
`_bytes_cal(app, n)`, so the report and the compaction trigger keep using **one**
definition of the estimate. The existing header already argues for this.

## Rendering

Non-zero parts only, **largest first**, so the report answers *what do I cut*:

```
  system prompt     ~15196
    rules           ~10442
    repo map         ~3978
    persona            ~432
    craft              ~198
    environment         ~86
    safety              ~60
```

Rejected: **prompt order** (reads like a table of contents; a user opening this report has
a full window, not curiosity); **keeping the one-line form** (it does not fit sixteen
parts, and truncating it to the top few would reintroduce an unexplained remainder);
**always printing all sixteen** (fourteen zeroes on a typical project buries the two lines
that matter).

The label `rules ~N` is kept verbatim — `tests/smoke/context_assets.sh` parses it, and it
is the right word anyway.

## What this does not do

- **It does not shrink anything.** It says where the tokens are. Acting on that (a smaller
  `craft`, `repoMap: false`, a trimmed `AGENTS.md`) stays the user's decision, and the
  knobs already exist.
- **It does not measure the tool definitions per tool.** That is a different report and a
  bigger one; `tool_names_lint` knows the names but nothing sizes them individually.
- **It does not touch `jc_sysmsg_build_sub`.** A subagent's prompt is short by
  construction and no surface reports on it.
