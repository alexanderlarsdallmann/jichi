# Plan: the TUI fence-grant affordance — design decisions, implications, recommendations

*Status: DESIGNED, not built. The implementation design + decisions for D1 of
[proposals/2026-08-fence-exceptions.md](../proposals/2026-08-fence-exceptions.md)
(M385), which decided the *what*; this decides the *how*, with the rejected
alternatives. Companion surfaces: [AUTONOMY.md](../AUTONOMY.md) §2 (the fence),
[GATE_INTEGRITY.md](../GATE_INTEGRITY.md) §9 (the shell reach), [ASK.md](../ASK.md)
(the delegate pattern this copies), [CONTROL.md](../CONTROL.md) (the never-widen
doctrine this must not violate).*

## 1. What is being built, and for whom

When a file **read** hits the path fence in an **interactive TUI session**, replace
the flat refusal with an offer: *grant read of this path for the rest of the
session?* A `y` adds the path (or its parent directory) to a session-scoped,
in-memory read-allow list — the M54 reference-root mechanism, granted live instead
of pre-declared at startup. For the solo developer who hits an unforeseen external
read mid-task and does not want to kill the run and relaunch with `--reference-root`.

Only D1, only reads, only the TUI. Writes (D2) and the ACP variant stay deferred
per the proposal; §6 says why.

## 2. The safety invariant — the one thing that must not break

**A grant can happen only when a human at an interactive TUI answers `y`.**
Headless, `--auto`, ACP, and subagents must be *structurally* incapable of
granting — not "refused by policy" but "the code path does not exist," because
policy can be misconfigured and structure cannot. This is the same shape as the
control channel's refusal to widen ([CONTROL.md](../CONTROL.md): *"tightening needs
no trust; loosening needs all of it"*) and `ask_user`'s no-delegate proceed path
([ASK.md](../ASK.md)): the capability to loosen lives at exactly one surface and is
absent everywhere else. Every decision below serves this invariant, and the test
that matters most (§7) is the negative one — the same denial, headless, must never
grant.

## 3. Design

**Data.** A `struct jc_vec runtime_read_roots` on `jc_app` (of canonicalized
`char*`), session-lived on `app->arena`, empty by default, freed at teardown.
In-memory only — never written to config, never persisted to the session store, so
a `--resume`d or `/fork`ed session starts empty. (That is a security property, not
an oversight: a grant is a live human decision, not a saved capability — §4-D.)

**Delegate.** A `confirm_path` function pointer + ctx on `jc_app`, NULL by default,
installed only by `jc_tui.c` and only when interactive (a TTY, not `-q`). Headless
(`jc_oneshot.c`), ACP (`jc_acp.c`), and the subagent path leave it NULL. It copies
`app->ask`'s installation shape exactly, so the wiring is a known pattern, not a new
one.

**Chokepoint.** In `jc_app_path_denied_ex` (`src/chat/jc_app.c`), in the
**read-only** branch (`for_write == 0`) only, after the reference-root check fails
and before the final deny: (1) check `runtime_read_roots` the same way
reference_roots are checked (resolved containment); a previously granted path allows
silently. (2) if still denied AND `confirm_path != NULL` AND `agent_depth == 0`,
call the delegate; on grant, append the resolved path and allow. Writes
(`jc_app_path_denied`, `for_write == 1`) never consult the delegate.

**Prompt.** A `cb_confirm_path` in `jc_tui.c`, beside `cb_confirm`: drain type-ahead
first (the M254 rule — a stray keystroke must never auto-answer a security prompt),
then a blocking single-key prompt `[y]es [n]o [d] this directory`, no timeout.

**Visibility.** `/status` (and the `status` line) lists `runtime_read_roots` when
non-empty. This ships in the same milestone, not after — see §4-E.

## 4. Design decisions (each with the alternative rejected)

**D-A — The grant is the M54 reference-root mechanism, granted live; not a new
capability.** Rejected: a distinct "temporary grant" concept with its own storage
and checks. It would be a second read-exception mechanism the reader must hold
beside reference roots, meaning two things by "allowed to read outside the fence."
Reusing the exact containment check keeps one concept; the only new thing is
*timing* (grant when the need is discovered vs. predict it at launch), which is the
whole feature and nothing more.

**D-B — Reads only; writes deferred.** Rejected: a symmetric write grant. A write
outside the workspace is the one action rollback cannot undo — the file is outside
the git tree, so `git reset --hard` never sees it ([GATE_INTEGRITY.md](../GATE_INTEGRITY.md)
§9.1). A read grant's worst case is disclosure of a file the human just chose to
disclose; a write grant's worst case is unrecoverable damage the human under-weighed
in a single keypress. The asymmetry is real, so the affordances are not symmetric.
Writes stay a deliberate pre-run decision (`--reference-root` is read-only by
design; widening writes needs `editScope` or leaving the fence off, both chosen
before the run).

**D-C — Interactive front-ends only; unattended has no grant path at all.**
Rejected: a config flag or an `--auto` posture that permits self-granting. That is
precisely the control channel's refused `approve` verb wearing a config hat: a run
nobody is watching must not be able to widen its own fence, or the fence means
nothing on the runs where it matters most. Unattended runs pre-declare exceptions
(`--reference-root`) before launch; if one turns out to need an unforeseen external
read, it fails and names the remedy (the M360 way-forward), and the human relaunches.

**D-D — Grants are in-memory and session-scoped; never persisted.** Rejected:
remembering grants across sessions (a `.jichi/` allow-list). A persisted grant is a
standing widening the human approved once and then forgot — indistinguishable, a
month later, from a fence that was never there. Ephemerality is the bound on the
blast radius: the worst a mistaken grant can do is last until the process exits.

**D-E — `/status` visibility ships WITH the feature, not after.** Rejected:
shipping the grant now and the visibility "later." A directory grant (`[d]`) widens
reads to a whole subtree for the rest of the session; a human who granted it and
moved on has an invisible hole they cannot see or revoke. Without the status line
the affordance is a footgun, so the line is part of the feature, not polish. (This
was the proposal's open question 2, resolved here: yes, and in the same milestone.)

**D-F — `[d]` grants the parent directory, offered as a distinct key, not the
default.** Rejected: granting the containing directory automatically (fewer
prompts). A directory grant is strictly more than the human was asked about; making
it the default trades a real widening for a saved keystroke. The exact path is the
least-surprising grant and the default; `[d]` is there for the reference-tree case
but must be chosen deliberately.

**D-G — No timeout; interactive silence blocks.** Rejected: an opt-in deny-default
prompt timeout. Settled in the proposal (D4) and reaffirmed here: a human is present
by definition at this prompt (the notify bell summons them if they stepped away),
and a clock that turned an unanswered grant into a *denied* one would send the agent
around the fence on a different, worse path while the human was refilling coffee. A
semi-attended operator can simply press `n`.

## 5. Implications

- **A security chokepoint stops being a pure predicate.** `jc_app_path_denied_ex` is
  the function every read routes through; today it is pure over config. The delegate
  makes it interactive and side-effecting (it can append to the allow-list). This is
  the real cost of the feature: the enforcement point of the read fence now has an
  I/O path. Mitigation: keep the containment math (`jc_path_*`) pure and unit-tested
  as it is; the delegate is I/O at the edge, tested by the PTY/e2e driver; and
  document the shift in the header, because a chokepoint that can now prompt deserves
  a comment a future reader cannot miss.
- **The blast radius of a wiring bug is the invariant itself.** If the delegate is
  ever non-NULL where it must not be (a headless path, a subagent), an unattended run
  could self-grant — the exact thing D-C forbids. This is why the negative test (§7)
  is the load-bearing one and why the implementation order (§6) proves it first.
- **Value is bounded to the human-present case.** The fence's highest-stakes use is
  unattended, and this feature deliberately does not touch it. It adds ergonomic
  comfort where a human is watching (the fence's lowest-stakes moment) and nothing
  where the fence matters most. That is the honest scope: convenience, not safety.
  Anyone weighing whether to build it should weigh it as convenience.
- **No cached-prefix concern** (unlike STATE-THE-REACH/M387): this changes a runtime
  tool path, not the system prompt, so prompt-cache stability is untouched.
- **ACP is a separate, harder piece** and stays deferred: it needs capability
  negotiation so a client that cannot render the prompt is treated as unattended (no
  grant), never left hanging — the `ask_user`-never-blocks lesson on a new surface.

## 6. Recommendations

1. **Build it as convenience, and only if the interactive friction is real.** The
   honest value (§5) is saving a relaunch on an unforeseen read. If that friction has
   not actually been felt, the feature can wait behind higher-value work; it fixes a
   comfort gap, not a safety gap.
2. **Implement in this order, and stop if the first step cannot pass:** the data +
   the chokepoint read-branch + the **negative headless/subagent test** first — prove
   nothing can grant before a prompt exists. Then the TUI prompt. Then `/status`. The
   invariant is the feature; if the negative test cannot be made green before the
   prompt is written, the design is wrong, not the test.
3. **Ship `/status` visibility in the same milestone** (D-E). Non-negotiable.
4. **Never extend it to writes or unattended without a new proposal** — those are
   D-B and D-C, and each is a different, larger risk with its own design story
   (writes need an audit + undo story; unattended is the refused `approve` verb).
5. **This is not a substitute for OS isolation.** For untrusted work the answer
   remains DEPLOYMENT.md §5 (run as non-root, in a container/VM) — the fence, granted
   or not, constrains the agent, not the process ([GATE_INTEGRITY.md](../GATE_INTEGRITY.md)
   §9.3-B). A grant affordance must never be sold as making the fence safe to relax.

## 7. Testing

- **Unit** (`tests/test_app.c` / `test_path.c`): a `jc_app_grant_read_root` helper +
  the runtime-root containment check — a granted path is contained, a sibling is not.
  A stub delegate that always grants proves the read-branch wiring; one that always
  denies proves refusal still refuses. Keep the pure `jc_path_*` tests unchanged.
- **PTY/e2e**: fence denial → prompt → `y` → a later read of the granted path
  succeeds; `d` grants the directory; a granted path survives to the next turn but
  not across `--resume`.
- **The load-bearing negative test**: the SAME denial, headless (`-p`, no delegate),
  refuses and does NOT grant; and a subagent at depth > 0 does not grant even in a
  TUI session. **Teeth**: revert the `confirm_path != NULL` / `agent_depth == 0`
  guard and show the negative test goes red — a grant leaked where it must not.

## 8. Open questions

1. **Delegate signature** — a bare function pointer + ctx (matching `app->ask`), or a
   small `struct jc_path_delegate`? Follow whichever `ask` uses, for one pattern.
2. **`/status` wording and revocation** — list the grants read-only, or also offer to
   revoke one (`/status` is currently read-only; a revoke verb is a small scope
   creep worth deciding deliberately). Recommendation: list-only in v1; revocation is
   "restart the session," consistent with D-D's ephemerality.
