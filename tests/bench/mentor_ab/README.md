# `tests/bench/mentor_ab/` — the mentor before and after its instructions arrived

M596 made a command's `agent:` persona reach its `subtask: true` run; until then the
scaffolded `/learn` mentor ran under the generic sub-agent prompt and never saw its
own `FORMAT IS STRICT` block (`docs/analysis/2026-08-27-the-language-of-lessons.md`
§5.3). Whether a mentor that *receives* its instructions drafts something a human
would rather `learn apply` is a grader's question. This harness produces the drafts,
one per arm per workspace, and seals the condition so a person can grade them blind.
First run and its result: that page's §13.

**A measurement, not a gate.** Needs a live model; never runs in `make ci`. Free
models only (`CLAUDE.md`): the config you pass must name a `jlu/*` or local model.
**No caps but connect** — a cap that fires manufactures an answer (ANECDOTES #64).

## Run

```sh
# in the jichi checkout
make
scripts/pin-driver.sh --prefix /tmp/mentor-ab/new            # the binary under test
git worktree add /tmp/mentor-ab/old-src <old-commit> && \
  (cd /tmp/mentor-ab/old-src && make -j4) && \
  cp /tmp/mentor-ab/old-src/jichi /tmp/mentor-ab/old-jichi   # the comparison arm

sh tests/bench/mentor_ab/run-arms.sh \
    --old /tmp/mentor-ab/old-jichi --new /tmp/mentor-ab/new/jichi \
    --config ~/.jichi --key-file ~/.jichi.key --label mentor-ab-02 \
    /path/to/project-a:/path/to/telemetry/project-a.jsonl \
    /path/to/project-b:/path/to/telemetry/project-b.jsonl
sh tests/bench/mentor_ab/blind.sh --label mentor-ab-02
#   ... a person fills results/mentor-ab-02/grading/FORM.md ...
cat tests/bench/mentor_ab/results/mentor-ab-02/.sealed/mapping.json
```

`--pair 2` runs a second pair of the same workspaces under `<ws>2-*` names. Pin both
binaries outside the tree: `make clean` deletes `./jichi` under a run in flight
(`docs/SESSION_RUNBOOK.md` §0).

## What it isolates

| Concern | How |
|---|---|
| the operator's state | each arm runs from a scratch `HOME` holding a copy of the config and the one telemetry log |
| the operator's draft | moved **out of the workspace** for the arm and restored after — kept beside `.jichi/` it was found and read by the mentor within three tool calls, on the first attempt |
| a workspace without mentor assets | gets the scaffolded `learn.md` + `mentor.md` for both arms, removed afterwards (`arms.log` says `scaffolded`) |
| the installed jichi as a third arm | `learn.md`'s embedded `learn analyze` runs `$JICHI_BIN`, pinned per arm |
| silent edits under `.jichi/` | every file is checksummed before and after; `other_changes=N` in `arms.log` |
| the author grading their own work | `blind.sh` writes the condition only to `.sealed/mapping.json`; `counts.txt` is by A/B |

## What it cannot do

- **An empty draft cannot be blinded.** When an arm writes nothing, the pair is a
  fact, not a test; the write-up must say so (the first run's zigodot pairs).
- **"Old" is not "no instructions."** The mentor's prompt file is on disk in the
  workspace, and a curious model can `read_file agents/mentor.md` itself; measured on
  the first run, two of four old arms did. The contrast is one of *delivery*.
- **n is what you ran.** Two pairs per project support a direction, never a magnitude
  (`tests/bench/craft_ab/README.md` says the same of nine).
- **It does not vary the language.** Both arms inherit the config's `language`; a
  German-drafting comparison needs a config that sets it.

## Layout

```
run-arms.sh              the arms; results/LABEL/{drafts,journals,homes,arms.log}
blind.sh                 the pack; results/LABEL/{grading,.sealed}
results/                 generated, git-ignored
```
