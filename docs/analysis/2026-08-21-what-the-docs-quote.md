# What the documentation quotes, and what checks it

**Date:** 2026-08-21 · **Milestone:** M510 · **Task:** decide whether the M508
quote lint should be widened from `docs/reading/` to the whole doc tree, by
measuring the population first. · **Outcome:** it should not — 1 of 110 code
blocks outside `docs/reading/` is a verbatim source quote. Two audits, both
negative, and one real gap found on the way: `config_keys_lint.sh` was checking
71 of the 94 top-level config keys it claims to cover. Fixed. One false alarm,
caused by the measuring tool, recorded in §4 because it cost the most time.

---

## 0. The question, and why it needed a measurement

M508–M509 built `tests/smoke/reading_quotes_lint.sh`: a fenced block tagged with
its origin file (```` ```c src/chat/jc_message.c:jc_history_add_tool_result ````)
must match that file line for line. It holds **41** blocks, all in
`docs/reading/`.

The obvious next move is to point it at the other ~150 pages, on the reasoning
that prose quoting code rots wherever it lives. That reasoning is sound and the
conclusion is still wrong, because it assumes a population that does not exist.
Two audits, before any machinery.

## 1. Audit one: is documented code quoted, or illustrated?

**Method.** Extract every fenced block with an info string of `c`, `h`, `json`,
`jsonc`, `make` or `awk` from `docs/*.md` (top level; `docs/reading/` is already
gated). Drop trivial lines (under 8 characters, or one of `{` `}` `};` `...`
`*/` `/*`). For each block, score every file under `src/ include/ tests/
examples/ completions/ man/ .jichi/ scripts/ editors/` plus `Makefile`,
`CLAUDE.md`, `CONTRIBUTING.md`, `README.md` by how many of the block's six
longest lines it contains **verbatim as whole lines**; then count how many of
the block's lines the best-scoring file contains.

| Classification | Blocks |
| --- | --- |
| **verbatim** (every non-trivial line present in one file) | **1** |
| partial (some lines present in the best-matching file) | 30 |
| no match | 79 |
| **total examined** | **110** |

The single verbatim hit is a two-line C block in `ROADMAP.md` — a lab notebook,
where quoting the code *as it was at that milestone* is correct and must not be
gated.

The 30 "partial" hits do not survive inspection. Their ratios are 1/2, 2/5,
1/7, and the best-matching file is frequently absurd for the content:
`scripts/a11y-session.sh` for a JSON config block, `README.md` for another. They
are coincidental single-line collisions on lines like `"models": [` — an
artifact of the scoring, not drift.

**Finding.** The doc tree **illustrates** code; it does not **extract** it. Its
JSON blocks are hand-written examples, its C blocks are sketches written to be
read rather than copies of anything. Widening the quote lint's glob would add a
gate over a population of one — and that one is a page that should be exempt.

**Decision: not done, deliberately.** The scope chosen at M508 was already the
right one. The trace chapters quote for a living because a trace *is* an
extract; reference pages do not, because an example tuned for explanation beats
a verbatim one.

## 2. Audit two: do the documented config examples name real keys?

Audit one's real finding was that the doc tree's code blocks are mostly
**configuration**, and configuration keys do rot — they get renamed, or a
feature is withdrawn. `config_keys_lint.sh` already checks one direction (every
key jichi parses is documented somewhere). Nothing checked the reverse: that a
key in a documented example is a key the parser knows. A reader copies an
example; a stale key in it is silently ignored.

**Method.** For every `json`/`jsonc` block in `docs/*.md`, extract the keys at
brace depth 1 (comments stripped, strings and escapes tracked). Treat a block
as a jichi config when **two or more** of its top-level keys are keys the parser
reads (a single coincidence is not evidence — `docs/DAEMON.md`'s protocol
messages and `docs/AUTONOMY.md`'s JSONL events both matched on one key each in a
looser first pass). Then list the keys that are *not* in the parser's universe.

| | |
| --- | --- |
| `json`/`jsonc` blocks in `docs/*.md` | 86 |
| confidently a jichi config (≥2 known keys) | 20 |
| **with an unknown top-level key** | **1** |

The one hit is `_comment` in `docs/SETUP_WIZARD.md` — a deliberate annotation
inside an example, ignored by the parser by design.

**Finding: clean.** No stale config key is documented anywhere in the tree, and
no new lint is warranted for this direction either.

## 3. What the second audit did find: a lint half its own size

Audit two needed the parser's key universe, and building it exposed the gap.
`config_keys_lint.sh` extracted only the **scalar** readers —
`jc_json_get_bool|int|long|num|str|double` and `jc_json_dup_str` — giving 71
keys. Every **container** key is read with `jc_json_get_obj(root, ...)`, and
there are 23 of them:

```
aliases control design docs editScope hooks instructions kineticCommandsAllow
kineticShellPrefixes logging lspServers mcpServers model models permissions
privilegedCommandsAllow referenceRoots retrieval routing search sound timeouts
tools
```

**71 of 94**, while the lint's own header claimed *"TOP-LEVEL keys read straight
off `root`"* — which is exactly what those 23 are. They are also the keys a
config example is most likely to name, so an undocumented one was the most
likely kind to slip through.

All 94 turned out to be documented, so the fix cost nothing but would have cost
a shipped-and-undiscoverable key to leave. The extraction now matches the two
container shapes as well, the count moved 71 → 94, and the floor moved 30 → 70.
The newly admitted keys enter a code path whose teeth are already proven by the
lint's own checks 3 and 4 (an invented key must not be found; a genuinely
documented one must be).

**The shape of this defect is worth naming**, because it is the second one this
week: a check that passes while covering less than it claims. M508 found the
reading guides' anchor count summing three series while each index page
presented it as its own. Both were green. Both were found by asking *what
exactly is in this universe?* rather than *does this check pass?*

## 4. The instrument, which produced a false alarm

The first run of audit two reported the `docs` config key as **documented
nowhere**. That is plainly false: `docs/DOCS.md` documents the `docs` array at
length.

The audit was searching the doc corpus with the same expression
`config_keys_lint.sh` uses:

```sh
grep -qE "(^|[^A-Za-z0-9])$k([^A-Za-z0-9]|\$)"
```

Measured over a 3.2 MB corpus containing 1,443 lines with the word:

| Invocation | Lines matched |
| --- | --- |
| bare `grep -cE '(^\|[^A-Za-z0-9])docs'` in the session shell | **0** |
| `/usr/bin/grep -cE` same pattern, same file | **1443** |

Bare `grep` in that shell is not GNU grep. It is a **shell function** (a
harness shim, ugrep-backed) that the interactive profile installs, and its
evaluation of the pattern returned nothing.

**The test tier is unaffected, and this is worth stating precisely rather than
reassuringly.** Every driver is `#!/bin/sh`; `sh` does not inherit an
interactive bash function, so `grep` resolves through `PATH` to
`/usr/bin/grep` (GNU 3.11). That is why `config_keys_lint.sh` was green
throughout, and why its own teeth — checks 3 and 4, which require the matcher to
miss an invented key and to find `autoCompact` — kept working. Seven drivers use
the `(^|` construct; all seven are `#!/bin/sh`.

**The lesson is about hand-verification, not about the tier:** check a gate's
pattern with the gate's own tool. `sh -c '…'`, or an absolute `/usr/bin/grep`,
reproduces what the tier computes; a bare command at a prompt may not. This is
the same family as M461's GNU-only `--exclude-dir` and M471's `\xNN` in sed —
the pattern language is not the same everywhere — except that this time the two
dialects were **inside one machine**, which is harder to suspect. Cost: about
twenty minutes and one paragraph of a nearly-published claim that an existing
lint was broken.

## 5. What would change the answer

If a page starts quoting source verbatim — a design note walking a function, a
porting guide showing a real signature — tag it with its origin
(```` ```c path.c:function ````) and the existing lint covers it: the **glob is
the only thing scoped to `docs/reading/`**, and widening it is a one-line
change. The measurement says wait for the population rather than build for it.

The same holds in the other direction. If a config key is ever *removed* while
an example still names it, audit two's method catches it in one pass — the
script is twenty lines of key extraction and a set difference. It is not a lint
today because there is nothing for it to hold.

## 6. Residue, stated

- Audit one's "partial" bucket was **classified by eye** (30 blocks read, all
  judged coincidental). A different reader might promote one or two to
  "drifted"; the raw scores are reproducible from the method in §1, and the
  conclusion does not turn on those 30 — it turns on the 1.
- Both audits were scratch Python, not committed. The method is written out
  above precisely enough to rebuild; there is no lint to keep them honest,
  because there is nothing for them to check.
- `docs/reading/` was **excluded** from both audits, since it is already gated.
  Its 41 tagged blocks are the only quoted code in the tree under a check.
- Audit two's classifier needs **two** known keys to call a block a config, so a
  hypothetical config example naming exactly one real key and several invented
  ones would be skipped as "not a config". That threshold bought precision
  against `DAEMON.md` and `AUTONOMY.md`, which are not configs at all.
