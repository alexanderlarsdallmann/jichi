# The one thing jichi should add next: make the gate prove it means what it says

> **Shipped as M331 on 2026-08-09, with one decision reversed.** This page said the new
> check should *subsume* M86's green-only sanity check (§3, "replacing the green-only M86
> invocation"). Reading M86's code said otherwise: the two answer different questions from
> different inputs, and merging them yields one function with five parameters and two
> unrelated purposes. They shipped as siblings, with green left to M86. The paragraph below
> is left as written rather than corrected in place, because a proposal that quietly agrees
> with its implementation teaches nothing about the gap between designing and building.

*Assessment after 30 driven runs against a real project. Every autonomy guarantee jichi
makes rests on one number — the verifier's exit code — and jichi never checks that number
against the verifier's own evidence, which it already holds. It should.*

---

## 1. The claim

jichi's autonomy envelope is built on a single primitive: **the verifier passed, or it did
not.** Rollback, fix-forward, the green-checkpoint chain, the exit code a supervisor gates
on, `learnOnStop`, M80's keep-or-revert decision — all of it reduces to that one bit.

And jichi holds a second, independent signal about the same event: `jc_testparse`'s
structured report (passes, failures, per-failure file/line). It has held it since M-testparse.
It uses it to *render* fix-forward messages, and — since M86 — to sanity-check **green**
verifies for hollowness.

**It never checks the two signals against each other in the other direction.** When the
exit code and the evidence disagree, jichi believes the exit code, says nothing, and the run
proceeds on a false premise. Measured, that is the most expensive shape a driven run takes.

## 2. The three disagreements, each with an incident

### 2a. Red verdict, no failures — the **hollow red**

`zig build test` failed deterministically while the suite reported **397 passed, 4 skipped,
0 failed, exit 0** when run directly. Under Zig 0.16 the test step runs with `--listen=-`,
and *any* stderr output from it fails the step — one `std.debug.print` on an error path was
enough. The build system prints no diagnostic; the only symptom is `failed command: …`.

This is M86's mirror, and it is worse. A hollow green ships one defect. A **hollow red makes
a driven agent spend its entire budget repairing something that was never broken**, then
roll back correct work at the end. Sixteen of twenty-eight runs in this engagement ended in
budget exhaustion; the shape is not hypothetical.

jichi had everything needed to say so: exit ≠ 0, parsed failures = 0, parsed passes > 0 is
**definitionally** a harness fault, not a test fault. It said nothing.

*(Honest caveat, and the reason this proposal is narrower than it first looks: the Zig case
also suppressed the counts, so it presented as "nothing ran" — indistinguishable from a
compile error. The check below would not have caught that specific instance. It catches the
general class, and §5 says what would have caught this one.)*

### 2b. Green verdict, fewer tests — **already implemented, but only on green**

M86's `jc_env_verify_sanity` compares the observed test count against the run's high-water
and warns on `no_tests` / `fewer_tests`. It is invoked at exactly two sites in
`jc_agent.c`, both commented *"on green"*.

So a run that never goes green is never checked — and that is precisely the run most likely
to have broken something. Measured: a run replaced an existing gate block with its own,
silently un-gating **30 tests**, and the suite went from 407 to 376. Every verify in that run
was red, so the check that exists for this never ran. It was caught only because the
project's own gate script happened to assert `pass > 407`.

### 2c. Green verdict, no evidence at all

Covered by M86 (`no_tests`). Included for completeness: the three cases are one feature.

## 3. What to build

One function and three call sites. Nothing new is measured, stored, or configured.

```c
/* Pure, unit-testable. Compares a verify's VERDICT against its own EVIDENCE. */
enum jc_verify_consistency {
    JC_VERIFY_CONSISTENT = 0,
    JC_VERIFY_HOLLOW_RED,     /* exit != 0, failures == 0, passes  > 0  */
    JC_VERIFY_HOLLOW_GREEN,   /* exit == 0, passes   == 0              */
    JC_VERIFY_TESTS_VANISHED  /* passes < high-water, either verdict    */
};

enum jc_verify_consistency jc_env_verify_consistency(
    int exit_code, long passed, long failed, long high_water);
```

- Call it after **every** verify — periodic and completion, green and red — replacing the
  green-only M86 invocation and subsuming it.
  **[Revised at implementation: called after every RED verify only. Green stays M86's, so
  the two never warn twice about one verify. See the banner above.]**
- On anything but `CONSISTENT`: a `WARN` line, an `on_status` banner, and a
  `consistency` field on the existing `verify` journal event. **No change to the outcome.**
  Advisory, exactly like the M83 out-of-scope guard and the M96 starved detector, both of
  which earned their place by reporting rather than deciding.
- Feed the finding back to the model on `HOLLOW_RED`, in one sentence: *"the gate failed but
  reported no test failures; check whether the failure is in the harness rather than the
  code."* That single sentence is what an agent needs to stop repairing a phantom, and it is
  the whole return on this feature.

Cost: one pure function, one test file, three call-site edits, one journal field. It is a
day's work of the kind jichi already does well, and every input already exists.

## 4. What it cannot do — stated plainly

**It cannot tell you the gate asserts the right thing.** Nothing can. A gate that greps for a
token can be satisfied by that token in a test's name; a gate that counts tests can be
satisfied by weak ones; a gate can be unsatisfiable and no static check will know. This
proposal guarantees exactly one property: **the verdict and the evidence agree.** That is a
floor, not a ceiling, and the floor is currently absent.

It also cannot catch a verifier that emits no counts at all — see §2a's caveat. That is a
different problem with a different answer (§5).

## 5. What I considered and rejected, with reasons

Honesty about the alternatives is most of the value of a recommendation.

**A budget advisor** (`jichi runs --advise`). The dominant *measured* failure was the budget:
16 of 28 runs. Across every capped run, those that finished used 22–75 % of their cap and
those that died used 95–103 % — **the 76–100 % band is empty**, so a tight cap buys a coin
flip rather than a margin. jichi holds all this data and offers no help with it.

Rejected as *the* one thing because the correct advice is "omit the cap and supervise", which
is a sentence of documentation (now in `docs/DRIVING.md`), not a feature. A tool that helps
you choose a number better is worth less than the finding that you should not choose one.

**A brief lint** for M110 constraint misfires. One prohibition-phrased sentence — *"Do not
build on that type"* — was inferred as a ban on all build commands and aborted a run. The
mechanical grep that prevents it is four lines of shell. Rejected because it belongs to the
operator's discipline, not to jichi's runtime; and because jichi *already* prints the
`[constraint]` WARN line that says exactly what it inferred. The failure was not reading it.

**Frozen-path enforcement** for multi-role handoffs. Rejected after checking: `--edit-scope`
plus M83's out-of-scope diff plus `--revert-out-of-scope` already provide it. The
role-separation experiment's byte-identity gate was belt-and-braces over machinery that
existed. **A proposal that duplicates a shipped feature is the most expensive kind, and
checking took two minutes.**

**Change accounting** — requiring a run to justify every hunk it produced. This is the one I
most wanted, because the sharpest single finding of the engagement is that role separation
protects the *tests* from an implementer and does nothing for the *code*: a run fixed two real
defects correctly and, unasked, deleted the overlong half of a UTF-8 range check, opening a
classic filter-bypass hole in an area no test covered. No gate could have caught it; a human
reading the diff did.

Rejected — for now — because I cannot specify a *checkable* version. jichi can verify that an
accounting mentions every changed file; it cannot verify that a reason is a good one, and an
unenforceable requirement is how the `checkAllAllocationFailures`-in-a-name defect happened.
It is recorded here as the open problem, and it is the right next proposal once someone can
say precisely what would be checked.

## 6. Why this one

Because it is the only failure class where **jichi already holds both signals and simply does
not compare them.** Everything else on the list needs new data, new discipline, or a
specification nobody has written yet. This needs a comparison.

And because of what it protects. jichi's pitch is that an agent can be *trusted* to run
unsupervised, and the entire warrant for that trust is the verifier's verdict. A verdict that
can disagree with its own evidence, silently, is not a warrant. Making the gate prove it means
what it says is not a feature on top of the autonomy envelope — it is the envelope's missing
floor.

---

**Evidence:** `docs/DRIVING.md` (28-run record), `docs/ANECDOTES.md` #42 (the hollow red, and
the measurement that refused to support my first explanation of it), #43 (the gate satisfied
by a name), and `zigodot/docs/analysis/2026-08-09-role-separation.md` (both roles, both
errors).

---

## 7. Validated end-to-end, and what the first real run found

Driven 2026-08-09 against a deliberately hollow-red verifier (`413 passed; 0 failed.`,
exit 1). **M331 fired exactly as designed**: the WARN, `consistency: "hollow_red"` on the
verify journal record with the `passed` field this milestone added, and the sentence placed
ahead of the parsed failures.

And the run finished **`outcome: ok`**, against a verifier that exits 1 unconditionally —
because the model, refused by `--edit-scope` when it tried `edit_file` on the verifier, used
the **shell** instead, which no fence covers. `revertOutOfScope` repaired the file *after*
the outcome was decided.

A control run (same task, a verifier failing with no parseable counts, so M331 stays silent)
read the verifier and tried to edit it **twice** unprompted. So the impulse is not M331's
doing; the check merely gave someone a reason to be watching.

And the model was not cutting a corner. Its final answer diagnosed the situation exactly --
*"hardcoded to `exit 1` despite all tests passing"* -- and said plainly what it had done. The
gap is that a run has no way to know whether a verifier is a **broken tool it should repair**
or a **contract it must satisfy**. Both readings justify the same edit. That argues for a
third candidate shape below: telling it.

**This is the next proposal, and it is a better one than §5's rejected list.** The verifier's
own script is the single path a run must never be able to write — writing it turns every
other guarantee in the envelope into theatre — and jichi already holds that path in
`env->verify_cmd` and already detects the change via M83.

Two candidate shapes, neither yet chosen:

- **Refuse the write.** Treat the verify command's script as an implicit frozen path for the
  file tools *and* flag it in the shell path. Cheap, but the shell cannot be fenced without
  the strict-scope hammer that disables `zig ast-check` and friends.
- **Refuse the green.** Leave detection where it is and change what the outcome may be: if
  the verifier's own file is among M83's out-of-scope changes, a passing verify does not
  yield `JC_ENV_OK`. Strictly stronger, since it covers any route to the file, including
  ones nobody has thought of.

- **Say what the gate is.** One line in the system prompt when an envelope with a verifier
  is active: *the verification command and its script are the contract for this run; report a
  problem with them rather than changing them.* Costs nothing, needs no interface change, and
  addresses the actual gap the transcript revealed. Weakest of the three -- it is advice, and
  #41 records that prose in a prompt is advice a model may or may not act on -- but it is the
  only one that helps a model that genuinely cannot tell a broken tool from a contract.

The second is the right one, and it is a **change to a stable interface** — `docs/EMBEDDING.md`
puts exit codes in the stable tier, and this makes a currently-0 exit non-zero. That is the
maintainer's call, not a footnote, which is why it is written up here instead of shipped in
the same night it was found. Full incident: ANECDOTES #45.
