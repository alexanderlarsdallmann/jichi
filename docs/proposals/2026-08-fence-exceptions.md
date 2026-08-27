# Proposal: one-off exceptions to the agent fences

**Status:** proposed (design only; no code in this milestone).
**Date:** 2026-08-11
**Follows:** [AUTONOMY.md](../AUTONOMY.md) (the fence + envelope), [AGENT_MODES.md](../AGENT_MODES.md)
(the permission model), [CONTROL.md](../CONTROL.md) (the never-widen doctrine),
[ASK.md](../ASK.md) (the clarifying-question tool), [GATE_INTEGRITY.md](../GATE_INTEGRITY.md) §9
(the shell reach), [HARDENING.md](../HARDENING.md) §1 (the semi-trusted model).

## Motivation

The question, from the operator: *can a user allow a one-off exception to access outside the
agent's fences — either when the agent asks mid-run, or pre-declared in the prompt — and if the
agent must stop and ask, how long does it wait before treating silence as deny?*

Today the answer is **no, and there is no clock.** The path fence, the reference roots, and the
edit scope are all resolved from startup state (config + flags) and are never re-asked;
`jc_app_path_denied_ex` returns a bare int and consults no approval callback. No approval prompt
anywhere has a timeout — every gate is a fully blocking read, so silence blocks forever and
never becomes deny. This proposal decides what *should* exist, and — as much of the answer — what
deliberately should not.

## Non-goals

- **Not a widening of the control channel.** The mid-run steering socket has no `approve` verb
  by design (M159: *"tightening needs no trust; loosening needs all of it… a socket that could
  loosen a running agent would be a privilege-escalation surface wearing a convenience hat"*).
  Nothing here adds one.
- **Not a model-requestable grant from prompt text.** See the design: a grant is a *human*
  action at an interactive front-end, never something the model talks itself into.
- **Not write access outside the workspace.** Reads are the case with a real, safe answer;
  mid-run write grants are deferred (see Honest limits).

## What already exists, and why none of it is a one-off exception

| Mechanism | What it grants | Why it is not the ask |
| --- | --- | --- |
| `referenceRoots` / `--reference-root` (M54) | read access to trees outside the workspace | **Startup-only, read-only, coarse** (a whole subtree), and set by the human before the run — the closest thing, but not *one-off* and not *mid-run*. |
| `editScope` / `--edit-scope` | which paths the file-write tools may touch | A *narrowing* of writes inside the workspace, not an *exception* reaching outside it. |
| `pathFence: false` / `--no-path-fence` | turns the fence off wholesale | The blunt instrument the reference-root mechanism was built to avoid; not an exception, an abdication. |
| `ask_user` | a clarifying answer from the human | Returns text into the conversation; grants no capability, and no-ops to "proceed" when unattended. |
| the approval prompt (`confirm_tool`) | run *this advertised tool* once / always | Gates tool *use*, not fence *scope*; a fence denial happens *before* any approval prompt and cannot be answered. |

The gap is precisely: **a per-path, mid-run, human-granted read exception, in an interactive
session** — and a stated policy for silence.

## The design

### D1 — Mid-run grants exist only where a human is present, and only for reads

When a file **read** tool hits the fence in an **interactive front-end** (the TUI; and, as a
follow-up, an ACP client that advertises the capability), the flat refusal becomes an offer:

```
▸ read_file  /home/user/reference/spec.md
  outside the workspace and any referenceRoots.
  Grant read of this path for the rest of the session? [y]es  [n]o  › 
```

A `y` adds the path (or its parent directory — a design sub-choice, see Open questions) to a
**session-scoped, in-memory reference-root list** — exactly the M54 read exception, but granted
live instead of at startup, and gone when the process exits. It never persists to config. This
is coherent because the decision is made by the human the fence exists to represent.

Mechanically: a new `for_write=0` denial path in `jc_app_path_denied_ex` consults an optional
`app->ask`-style `confirm_path` delegate (NULL in headless/ACP-without-the-capability), and on
a grant appends to a runtime reference-root vec that the same function already checks.

### D2 — Writes are deferred, not granted

A mid-run write grant outside the workspace is a genuine escalation: it is the one action
rollback cannot undo (the file is outside the git tree, so `git reset --hard` never sees it —
GATE_INTEGRITY.md §9.1). The proposal **defers** write exceptions. If ever built, they must be
per-path, per-call, never "always", never over any socket, and audited to the privileged log —
but the recommendation is to require the operator to widen the fence *before* the run (a
`--reference-root` sibling for writes, or `editScope`) rather than mid-run.

### D3 — In-prompt "you may access X" is advisory, never a grant

The model reads untrusted content (HARDENING.md §1); "the prompt said I could" is the exact
shape of an injection that turns fetched text into capability. So a path named in the prompt —
even in the *user's* own prompt — does **not** open the fence. It may inform the model's plan,
but the fence still enforces, and the model reaches the path only via D1's human-answered prompt
or a pre-run `--reference-root`. Pre-declaration is a human action on the command line/config,
where it cannot be forged by content. This is stated so no future reader "helpfully" wires the
prompt into the grant.

### D4 — Silence has no clock; it is structural, not temporal (the operator's decision)

The question "how long before silence = deny?" is answered: **there is no timeout.**

- **Interactive:** the prompt blocks until the human answers. A human is present by definition;
  the completion-notification bell (M34f) already exists to summon them if they stepped away. A
  clock that turned an unanswered grant into a *denied* one would be a footgun — the human
  returns from coffee to find the agent took a different, worse path around a fence it could have
  been let through — and a clock that turned silence into an *allow* would be the escalation D1
  exists to prevent. So: block, and let the human decide, however long that takes.
- **Unattended (`--auto` / headless / ACP-without-the-capability):** there is **no prompt at
  all**. A fence exception must be pre-declared (`--reference-root`, `editScope`) before the run.
  This preserves the M159 invariant exactly: an unattended run can only be *narrowed* from
  outside, never widened, and silence is not even a question because nothing is asked.

**Rejected alternative:** an opt-in prompt timeout that fails closed (silence → deny) after a
generous, explicit duration. It has a real use (a semi-attended run where the operator wants the
agent to stop waiting and move on), but it introduces a *temporal* silence semantics the codebase
deliberately does not have anywhere, it complicates the "no prompt when unattended" line, and its
value is thin: a semi-attended operator can answer `n`. Recorded here so the choice is visible;
revisit only if a concrete semi-attended workload asks for it.

### D5 — Never over the control channel

The grant lives at the interactive front-end, never on the steering socket. Adding a `grant`
verb to the control channel would be adding the `approve` verb M159 refused, under a new name.
Restart the run to widen it; steer it only to narrow.

## Honest limits

- **Reads only.** Writes outside the workspace stay a pre-run decision (D2).
- **Interactive only.** Unattended runs get no live grant, by design (D4). This is a feature —
  it is what makes "loosening needs all of it" true — but it means an autonomous run that turns
  out to need an external read fails and must be re-launched with a `--reference-root`. The
  failure names the remedy (the M360 way-forward discipline).
- **A granted read is still a read of untrusted content.** The M300 fence (external content is
  data, not instructions) applies to whatever the granted path returns.
- **The human can grant the wrong thing.** D1 trusts the interactive human absolutely; that is
  the same trust the approval prompt already places. The session scope bounds the blast radius.

## When this is the wrong idea

If a run needs many external reads, the answer is `--reference-root` (or a broader read root) at
launch, not a stream of mid-run grants — the same way a run that needs the shell everywhere
should not be answering an approval prompt every call. D1 is for the *occasional, unforeseen*
external read, not the planned one.

## Milestone status

| # | Milestone | State |
| --- | --- | --- |
| — | This proposal (design) | proposed |
| — | D1 TUI read-grant affordance (`confirm_path` delegate + runtime reference-root vec) — implementation design + decisions in [plans/2026-08-tui-fence-grant.md](../plans/2026-08-tui-fence-grant.md) | designed, not built |
| — | D1 for ACP (a `session/request_permission` variant, capability-gated) | not built, follow-up |
| M385 | D3 documentation in AUTONOMY.md (in-prompt grants are advisory) | **done** — shipped with this proposal; the row said "not built" for a day because the table was written before the same commit's AUTONOMY.md edit |

Deferred, with the trigger: **D2 write exceptions** — revisit only with a concrete workload that
needs a mid-run write outside the workspace *and* a design for auditing/undoing it; **D4's opt-in
timeout** — revisit only with a semi-attended workload that a plain `n` does not serve.

## Open questions

1. **Grant granularity:** the exact path, or its parent directory? A single file is the least
   surprising; a directory saves repeated prompts for a reference tree. Recommendation: the exact
   path, with the prompt offering the parent as a second key (`[y] this file  [d] this directory
   for the session  [n] no`).
2. **Should a session grant be visible in `/status`** (like the active reference roots are in
   `doctor`)? Recommendation: yes — a granted exception the operator has forgotten is a standing
   widening they should be able to see.
3. **ACP timing:** the ACP `confirm_path` follow-up needs a capability flag so a client that
   cannot render the prompt is treated as unattended (no grant), not as a hang. Confirm the ACP
   capability-negotiation shape before building it.
