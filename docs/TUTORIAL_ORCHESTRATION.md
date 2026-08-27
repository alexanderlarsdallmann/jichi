# Tutorial: delegating to sub-agents — one, many, and when not to

This is a hands-on, step-by-step tutorial: how an agent comes to *use*
`spawn_subagent` and `spawn_parallel`, how you steer that, and — just as
important — when delegation is the wrong tool. It assumes the working bench
from [TUTORIAL_BEGINNER.md](TUTORIAL_BEGINNER.md); the reference pages are
[SUBAGENTS.md](SUBAGENTS.md) and [PARALLEL.md](PARALLEL.md). Like
[PROJECT_RECORDS.md](PROJECT_RECORDS.md) this is **ungraded reading**: knowing
*when* to delegate is a judgment built by practice, and a checker could only
grade the shape of it.

**One prerequisite that bites:** the spawn tools exist only under the **full**
tool profile. `toolProfile: core` — and `--lite`, which implies it — drops them
deliberately (no fan-out on a tiny context window). If nothing in this tutorial
works, run `jichi doctor` and check the resolved profile first.

## 0. The mental model: may, should, must

An agent learns it **may** delegate from one place only: the tool being
advertised at all. It learns it **should** from three, in increasing strength:

1. **The tool description** — travels with the capability, always present.
   jichi's now name the trigger shapes: *self-contained subtask whose detail
   would flood your context* (one sub-agent); *three or more independent
   look-ups that share no state* (parallel).
2. **Your project's rules** (`AGENTS.md`) — policy the model reads every turn.
   Prose is advice: expect compliance, not obedience.
3. **Structure** — a command or skill that *demonstrates* the pattern. This is
   the strongest signal there is, and it is why the default `init` pack ships
   `/investigate` (step 4).

And it learns it **must** when you say so — in the prompt ("fan out over the
four subsystems"), or deterministically via the `workflow` subcommand when you
already know the shape and want a script, not a mood.

## 1. Your first sub-agent

In a project you have run `jichi init` in, ask for a delegation explicitly —
the "must" channel is the right place to start, because you will see every
moving part:

```
Use spawn_subagent to summarize what src/ contains: one paragraph per
subdirectory, read-only.
```

**You should see:** an approval prompt (spawning an autonomous agent is a
mutating action — approve it), then a `subagent <model> · ro: <task>` banner,
then the sub-agent's own tool calls streaming *indented* under yours, and
finally its answer returned as one tool result. That is the whole contract:
a fresh agent, one task in, one answer out.

## 2. Fresh context is the whole contract

Now the teachable failure. In a conversation where you have just discussed a
specific file, send:

```
Use spawn_subagent with the task: "fix the bug we discussed".
```

**You should see** the sub-agent flail — it has no idea what "we discussed".
A sub-agent starts fresh and sees *only the task text* (plus the project's
rules and memory). Everything it needs must be written into the task: paths,
the question, the expected output shape. This single fact explains most
delegation failures, and it is why the sharpened tool description ends with
exactly this warning.

## 3. Delegate to a specialist

`init` shipped named profiles under `.jichi/agents/`. Delegating to one is
more useful than delegating to "yourself, but smaller":

```
Use spawn_subagent with agent "reviewer" to review the diff of my last
commit for correctness problems.
```

**You should see** the reviewer profile's persona applied, and — because
profiles can carry a `tools:` fence and `readonly` — a sub-agent that
*cannot* edit even if it wanted to. The fence is enforced, not advisory
(unlike a skill's `tools:` hint at top level; see [SKILLS.md](SKILLS.md)).

## 4. The worked example: `/investigate`

The default pack ships the demonstration:

```
/investigate how does configuration loading decide which file wins?
```

**You should see** the model (a) name two to four independent angles, (b) call
`spawn_parallel` once with one read-only task per angle, (c) a **live board**
of the children with their current tool and token count, and (d) a synthesis
naming agreements, contradictions, and gaps, with `file:line` citations.

Read `.jichi/commands/investigate.md` afterwards — it is short, and it is the
template for every orchestration command you will write yourself: the shape,
the fresh-context rule, and the counter-case ("only one angle: skip the
fan-out") all stated.

## 5. Parallel by hand, and what bounds it

The semantics that matter when you fan out:

- **Read-only by default.** A task with `write:true` runs in an isolated git
  worktree; changed files merge back **file-level, first wins**, conflicts are
  reported, never auto-merged. Give write tasks *disjoint* files.
- **Budgets slice.** Each child gets a share of the remaining budget, so N
  children cannot overspend N×. A child that dies at its slice says so.
- **A wedged child is killed** after `parallelTaskTimeout` (default 300 s) and
  the message names that knob — half a swarm dying at exactly 300 s is a
  setting, not a mystery.
- **Verification per child** (opt-in `parallelVerify`): a red child is
  quarantined, a green one merges.

## 6. When NOT to delegate

The counter-cases are half the skill:

- **One thing to look at.** A single read is cheaper than a sub-agent reading
  it and reporting back. The `/investigate` command says this itself.
- **Work that depends on the conversation.** Step 2's lesson: if writing the
  task means transcribing your whole context, do the work yourself.
- **Deep recursion.** Depth is capped (`maxSubagentDepth`, default 2) and each
  level's iteration budget is *halved*, so towers of delegates cannot multiply
  the spend. A sub-agent that stops at its cap says `[stopped at its iteration
  limit]` — treat its partial answer as partial.
- **Cost.** Every child re-bills its own prompt. On an uncached backend, three
  children investigating what one grep answers is three times the price of
  being wrong.

## 7. Make it durable

When a delegation pattern earns its keep, promote it from prompt to structure:

- **A command** (`.jichi/commands/<name>.md`) for human-triggered patterns —
  copy `/investigate` and change the shape.
- **A skill** with `restrict-tools: true` for a recipe the *model* should find
  by name: seeding a sub-agent with that skill enforces its tool fence.
- **A line in `AGENTS.md`** for standing policy ("investigations across three
  or more subsystems: fan out read-only") — advice, but read every turn.

## Troubleshooting

| Symptom | Cause, and the lever |
|---|---|
| The spawn tools never appear | `toolProfile: core` or `--lite` drops them; `jichi doctor` names the resolved profile |
| "cannot spawn" from inside a sub-agent | the depth cap (`maxSubagentDepth`); by design, `spawn_parallel` is top-level only |
| children all die quickly | budget slices — the parent's remaining budget was already thin |
| children die at exactly 300 s | `parallelTaskTimeout` |
| a write child's work vanished | it lost the first-wins merge or was quarantined by `parallelVerify` — the report says which |

## Exercises

1. Run step 2's teachable failure, then rewrite the task so it succeeds.
   *You should see* the difference is entirely in what the task text carries.
2. `/investigate` a question you already know the answer to, and grade the
   synthesis yourself. *You should see* where angle-splitting helps and where
   it pads.
3. Write a `/compare-approaches` command that fans out two read-only tasks —
   one arguing for, one against a refactor — and synthesizes. Copy
   `/investigate`'s counter-case line into it.
4. Add one delegation-policy line to your `AGENTS.md`, work for a day, and
   note in your record ([JOURNEY.md](JOURNEY.md)) whether it changed anything.
   Prose is advice — measure it like one.
