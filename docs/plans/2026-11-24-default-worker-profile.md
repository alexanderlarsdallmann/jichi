# 2026-11-24: a default worker profile, so the fence does not depend on the lead remembering it

Registered as [def-426](../DEFERRED.md#def-426) at the operator's word (SV:0634, 2026-11-24:
"Yes, send the default worker profile card"). Design now; build when a drive needs it.
Not in H12; not a blocker for H12's READY.

## The gap (read in the code at master `a0a8b4ae`)

A delegated worker is fenced only when the lead names a profile in the call:

- `spawn_parallel` reads each task's profile at `src/tools/jc_tool_parallel.c:3517`:
  `agent_name = jc_json_get_str(e, "agent", NULL);`
- `spawn_subagent` reads it at `src/tools/jc_tool_subagent.c:232`:
  `jc_agentdef_merge(def, arg_model, has_arg_ro, arg_ro, &model_sel, &ro);`
  with `def` from `tu_arg_str(args, "agent")`.
- No config key supplies a default. A write task that names no profile gets the full tool
  set, the shell included: `include_mutating = (t->write && !app->readonly)`
  (`jc_tool_parallel.c:1273`) gates only on `write` and the run's readonly, never on the
  profile's `tools:` list.

The drive's `edit-worker` profile (`.jichi/agents/edit-worker.md`, committed `eabdf9e73`)
removes the shell, but naming it is still an instruction the lead must remember. Run 0109's
first dispatch (SV:0533) omitted it on the first run after the lead had committed to it —
the same failure as the "never run park by hand" briefs, one level up: the product cannot
hold the rule for the operator.

## Shape (the card's ask; the details are decided at build time)

**1. A config default for delegated write workers.** A profile name applied when a write
task names none. Candidate surface (decide at build time): the `strictness` object (see
item 2) gains a sibling key, or a top-level `delegatedWorkers.profile`; it resolves
`jc_agentdef_find` the same way the call's `agent` does, and an unknown name is named at
load (the M1006-a idiom). The default must be overridable per task (a named `agent` wins),
and a read-only task is never escalated by it.

**2. A dial for a write task that would run unfenced.** In the `strictness` object M1006
started (`src/config/jc_config.c:2077-2115`, `dirtyDispatch`, the modes read leniently, an
unknown value named at load): off / warn / refuse, the same lenient-read + name-at-load
idiom. `refuse` means a write task with no profile resolves to the profile's refusal, not
to the full tool set — the house rule "configuration allows for stricter refusals".

**3. The guard's house rules** (the operator's 2026-10-20 direction):
- a telemetry event on **every checked dispatch, in every mode** (off, warn and refuse), so
  the census exists before the default is ever switched; its reader in the same milestone;
- the default marked **PROVISIONAL in DECISIONS**, with the measurement that will decide it.

**4. SV:0243 item 3 in the same note (a per-command fence for a delegated shell).** The
related gap: a worker WITH a shell is fenced only by the command-prefix allowlist
(`shellFenceAllow`, M852), not per-command — a hang in a long compound is possible, as
seven hang stops showed (SV:0243's incident class). Weighed as a related alternative to
this card: a per-command fence bounds blast radius of a SHELL worker, while this card
removes the shell from the default worker. They compose (default = no shell; an explicit
shell worker = per-command fence) and are registered together so the decision sees both
gaps in one place.

## The PROVISIONAL default (the measurement that decides it)

Telemetry first, default second: until the census exists, the product default stays
**off** (today's behaviour: an unnamed write task gets the full set). The drive config can
set **warn** immediately on a build — warn is non-blocking, so a drive that has never
fenced a dispatch sees every one, and the measurement is the switch to refuse. DECISIONS
records the default PROVISIONAL; the measurement that decides it is the census of
unnamed-write dispatches per drive over one week of runs (the telemetry's reader reports
the count, the mode and the named profile, per run), which the operator reads to set the
product default: refuse (a shell worker is the exception, named), or stay off (the lead's
instruction holds).

## Not in scope

- The drive's `edit-worker` profile itself (already built, `eabdf9e73`): this card makes a
  profile the DEFAULT, not a new profile.
- Read-only tasks (they never mutate; the fence is about the shell and the file writes).
- The switch-over of this drive's dispatches to the dial (the drive config is the
  supervisor's host job; the card's build lands the product, the drive turns it on).
