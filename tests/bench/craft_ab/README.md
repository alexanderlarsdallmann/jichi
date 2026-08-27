# `tests/bench/craft_ab/` — the frontier craft A/B

The experiment M318 raised and could not run: does the `craft` system-prompt section
(M299, config `craft`) change what a **large** model produces on a task whose **deliverable
is not stated**?

Design, hypothesis and conflict-of-interest note, fixed before any run:
**[`docs/proposals/2026-08-craft-ab-frontier.md`](../../../docs/proposals/2026-08-craft-ab-frontier.md)**.
Predecessor and its limits: [`docs/analysis/2026-08-06-craft-ab.md`](../../../docs/analysis/2026-08-06-craft-ab.md).

This is a **measurement, not a gate**. Like `tests/bench/`, it needs a live model and must
never run in `make ci`.

## Quick start

```sh
make                                        # the harness drives ./jichi
export JC_DEV_KEY=...                       # the key; never pass it on a command line

python3 craft_ab.py run   --pairs 3         # spends tokens — see the cost note below
python3 craft_ab.py blind --label <label>   # builds the blinded grading pack
#   ... fill in results/<label>/grading/FORM.md ...
python3 craft_ab.py score --label <label>
```

Start with `--only 02-more-callers --pairs 1` to see one pair before committing to a session.

## Layout

| Path | What it is |
|---|---|
| `tasks/<id>/prompt.md` | frontmatter (grader's notes: what is unstated, where the real problems are) + the prompt body. **Only the body is sent.** |
| `tasks/<id>/files/` | that task's fixture tree, copied fresh into a throwaway workspace per run |
| `craft_ab.py` | `run` / `blind` / `score` |
| `results/<label>/` | generated, git-ignored: per-run workspaces under **opaque ids**, the grading pack, and `.sealed/` |
| `results/<label>/.sealed/` | the only place the condition is written down: `runs.json` (which run was which arm) and `mapping.json` (which of A/B) |

## The three tasks

Each names a situation and no artifact. None asks for a design note, a test, or an
explanation — that is the entire point.

| id | ends with | what it is really about |
|---|---|---|
| `01-notes-grew` | *"Deal with it."* | five defensible fixes that differ in **what they destroy**; a year of irreplaceable notes nobody mentioned |
| `02-more-callers` | *"Do what you think is needed."* | three real defects; fixing the API breaks its one existing caller |
| `03-slow-report` | *"I need this back."* | the slow part is not the part everyone blames, and 40× the data means *some* slowdown is correct |

`02-more-callers` needs no grader for one thing: the fixture **prints its own bug**
(`make && ./report` → `host=sc1-w1-1 run=sc1-w1-1`, two `kv_get` results aliasing one static
buffer). Whether a run notices depends on whether it ran the code before changing it.

## Four things this harness does that are not optional

**It proves the two arms differ before spending a token.** `preflight` renders the system
prompt under each config and requires the craft marker present in one and absent in the
other. An A/B whose arms are secretly identical reports a null result forever and looks
exactly like a real one — this project has shipped that failure twice (M86's verify that
passed while running nothing; M201's truncating readers), which is why
[`docs/TEST_INTEGRITY.md`](../../../docs/TEST_INTEGRITY.md) exists.

**It refuses truncated pairs.** A run stopped by the token budget has an answer that stops
mid-thought, and it loses on every question whichever arm it came from. `blind` skips such
pairs and names them. The first pilot hit exactly this at a 150k budget: ~14k tokens per tool
call, because every call resends the history.

**It blinds properly, and "properly" took three passes.** A blind that survives only until
someone looks around is not a blind, and in this workflow the person who runs the session is
the person who grades it — so every channel from the run back to the grader had to be closed:

| Leak | Closed by |
|---|---|
| A/B always in the same order | randomised per pair, mapping sealed |
| `ls -l` on the pack (`copy2` keeps mtimes) | timestamps flattened to a fixed value |
| run directories named `…__p1__on` / `__off` | **opaque ids** (`r-1a2b3c4d`) |
| `meta.json` at the top of the tree, with `"craft": true` per run | public meta is redacted; the full record lives in `.sealed/runs.json` |
| run order deducible from timestamps + a documented alternation rule | order randomised, not alternated |
| the progress line printing `…__on  out=5753` beside a pack whose answer lengths are visible | the console names the run id, never the arm |

Renaming the directories alone would have been theatre while `meta.json` sat beside them
saying `"craft": true`. Each of these was found by auditing the artifacts rather than by
trusting the design.

**It refuses to re-blind over a part-filled form.** Re-blinding both deletes the pack and
re-draws the A/B assignment, so answers already written would silently stop meaning anything.
`--force` if you really do want to start over.

## Cost

Measured on `anthropic/claude-opus-4-5`, one pair (two runs): **~600k input, ~9k output
tokens**. A full 3 tasks × 3 pairs session is roughly **5–6M input tokens**. Decide that
deliberately. `--price-in` / `--price-out` put a currency figure on it if you know the
gateway's rates; without them, cost is reported in tokens, which is the honest unit here.

## Who grades

Not this script, and not the model that wrote the section, the tasks, or the questions.
`score` only counts what a human wrote in `FORM.md`.
