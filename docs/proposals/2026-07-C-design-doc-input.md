# Proposal C — a design doc as a first-class drive input (`--design <file>`)

**Status: BUILT (M106, 2026-07-10).** Shipped as `--design`/`--spec` — a v1 with
the CLI flag, the authoritative system-prompt section (+ surface-conflicts
preamble), a static `JC_DESIGN_MAX` cap, and `sysmsg` inspection. The config key,
repeatable flag, dynamic M73 fitting, and a `/design` TUI command are deferred (see
"Not yet" in docs/DESIGN_INPUT.md). Original proposal below.

## The evidence (grounded, this session's dogfood)
The single strongest *positive* finding across the zigodot drives: **a grounded
design doc turns VM-internal work — the category `--auto` otherwise thrashes on —
into work jichi completes autonomously.**

- Object lifecycle + first-class utility callables (a 4-layer parser/codegen/VM
  subsystem) both landed **fully autonomously, zero last-mile**, when the brief
  pointed at a `docs/plans/*.md` design doc with exact `file:line` seams, a
  reuse table, and pitfalls.
- The *earlier* drives, briefed only with a task description (no design doc),
  **thrashed to budget with no green increment** (ANECDOTES #14) on the same class
  of work.
- Even the one drive that reverted (analyzer warnings) had banked the design's
  scaffolding correctly; the failure was a Zig type-identity detail the doc hadn't
  called out — i.e. the doc's *coverage*, not the mechanism, was the variable.

Today the design doc reaches the agent only as pasted text inside the `-p` brief:
it competes for attention with the task framing, isn't structurally marked as
authoritative, and isn't size-managed against the context budget.

## Proposal
Add a first-class **design input**, distinct from rules/memory/glossary:

- **Surface:** `--design <file>` (repeatable) + config `design: [paths]`. (`--plan`
  is taken — it selects plan *mode*. Suggested name: `--design`, alias `--spec`.)
- **Injection:** `jc_sysmsg_build` adds a `# Design specification (authoritative
  for this task)` section AFTER rules and BEFORE the repo map, with a fixed
  preamble instructing the agent to treat it as the authoritative plan: follow its
  seam, reuse the paths it names, and honor its pitfalls; if the design conflicts
  with the code, surface the conflict rather than silently diverging.
- **Size management:** the design competes for the always-sent prefix (the
  cacheless-cost lever), so it is bounded by `jc_sysmsg_fit_caps` (M73) as a third
  shrinkable section (after instruction files + repo map), truncated with a note
  when it would overflow ~a third of the effective budget. A `doctor` check reports
  a design that doesn't fit.
- **Scope:** session-scoped (a `--auto` increment is one design); cleared on
  `/clear`. Surfaced in `/status` and `/context` as its own line.

## Why a section, not just "paste it in the brief"
1. **Authority:** a labeled, preamble-framed section is treated as a spec, not
   conversational context — the same reason rules/memory are separate sections.
2. **Persistence:** it survives compaction (system prompt isn't compacted), so a
   long turn doesn't lose the plan the way a pasted brief scrolls out of the
   window mid-turn.
3. **Budget honesty:** routed through M73 fitting, it can't silently blow the
   prefix on the cacheless backend; a pasted brief has no such guard.
4. **Reuse of the win:** it operationalizes the pattern that already works,
   lowering the bar to invoke it (one flag vs. hand-assembling a brief).

## Risks / open questions
- **Prefix cost:** a large design doc enlarges the always-sent prefix — the exact
  cost the M-band prefix levers fight. Mitigated by M73 fitting + a size cap, but a
  verbose design partly offsets its own benefit on a cacheless backend. (A design
  doc is typically 3–6 KB — small vs. a 12 KB repo map.)
- **Over-trust:** telling the agent the design is "authoritative" could make it
  follow a wrong design past evidence. The preamble must include "surface conflicts
  with the code rather than diverging silently" (as above), and the supervisor
  audit still applies.
- **Overlap with output-styles / persona:** distinct — a design is task-specific
  and authoritative-for-the-task; an output style augments the persona for the
  session. Keep them separate sections.
- **ACP/headless parity:** the section is built in `jc_sysmsg_build`, so all
  front-ends get it identically (like rules).

## Rough shape (if approved)
- `jc_config` gains `design` (string list); `main.c` parses `--design`/`--spec`.
- `jc_app` loads each design file into `app->design` (arena, bounded).
- `jc_sysmsg_build` emits the section + preamble; `jc_sysmsg_fit_caps` extended to
  a third section; `jc_context_report` + `/context` account for it.
- `doctor`: report configured design files + a fit warning.
- Tests: `jc_sysmsg_fit_caps` three-section fitting (pure); a build-includes-design
  assertion; a doctor check. Docs: `docs/DESIGN_INPUT.md`.

**Recommendation:** worth building — it is the one change that operationalizes the
biggest positive lever the drives found, is front-end-uniform, and is guarded
against the cacheless prefix cost. Gate it behind review of the size/authority
trade-offs above.
