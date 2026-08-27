# Role and journey: making orthogonal true

*Design note written before the change. The decision it implements is the operator's, taken
after the ambiguity below was put to them explicitly — which is the part of this that should
have happened at M326i and did not.*

---

## The defect is in the model, not the screen

`setup`'s first screen asks two questions — *"who are you?"* (12 role presets) and *"what are
you walking into?"* (5 project journeys) — and the source says of the journeys:

> *The five project JOURNEYS (M183) — **orthogonal** to the roles above: not who you are, but
> what you are walking into.*

**Orthogonal means combinable, and they are not.** All 17 are entries in one `PRESETS[]`
array; `setup` picks one index, `--preset` takes one name, and `jc_setup_apply_preset` is
called exactly once. Choosing `contributor` means you do not get `developer`.

They are also structurally identical — both set scaffold pack, output style, mode, feature
bitmask, script profile and the language-pack flag — so a "journey" is a role wearing a
different sentence. And they overlap enough to make the choice genuinely hard:

| | kind | pack | mode | features |
|---|---|---|---|---|
| `tester` | role | default | chat | TESTCMD VERIFY SNAPSHOTS |
| `small-project` | journey | default | chat | SNAPSHOTS TESTCMD VERIFY |

Those are the same configuration. A user asking *"am I a tester, or am I starting a small
project?"* is choosing between duplicates. M326i improved how that list **looked** while
leaving the reason it was confusing untouched.

**Decision (operator, 2026-08-07): make orthogonal true.** Two questions, both applied.

## The design

**A journey layers onto a role, exactly as `--profile` already does.** The precedent exists:
`jc_setup_apply_complexity` is applied *after* the preset and adjusts what it produced. That
is the shape a second dimension already has in this codebase; journey becomes the second such
layer rather than a fork of the first.

**Order: role first, then journey.** `jc_setup_apply_preset` only ever fills what is unset
(`if (a->mode == NULL) …`, features OR in), so applying it twice composes without new merge
code. Journey second means the journey wins wherever the role left a gap — right, because the
role describes the person and the journey describes the work in front of them.

*Today there is no true conflict to arbitrate:* every journey's mode is either unset or
`plan`, and no journey sets `chat` explicitly. `reviewer` + `small-project` keeps plan mode;
`developer` + `contributor` gains it. Stated so that whoever adds a journey that does
conflict knows the rule they are working against.

**Both scaffold packs are written, role first.** `scaffold_write_pack` already skips files
that exist, so a second pack composes by construction — the journey's assets land beside the
role's, and anything both packs ship is written once. Writing only one would reintroduce the
either/or this change exists to remove.

**The language pack still replaces the ROLE's pack**, and is offered when *either* half asks
for it. `developer` + `rewrite` therefore gets `c-cli` (the role's `default` swapped) *plus*
the `rewrite` pack, which is the composition a user picking both would expect.

## Back-compatibility, which is not optional here

`--preset rewrite` is **documented in `SDLC.md` and `WORKFLOWS.md` and exercised by
`tests/smoke/setup.sh`**. Checked before designing, not after.

So journeys stay addressable as presets:

| invocation | meaning |
|---|---|
| `--preset developer` | role only, as today |
| `--preset rewrite` | that journey alone, as today — no role |
| `--preset developer --journey rewrite` | **new**: the composition |
| interactive | two questions; the second offers *"nothing in particular"* and defaults to it |

*Rejected: moving journeys out of `PRESETS[]` into their own table.* It is the tidier data
model and it breaks a shipped test and two documented invocations for an internal
aesthetic. The `journey` flag added at M326i already distinguishes them within one table,
which is what the two questions read from.

## What could go wrong

- **60 combinations, three tested.** Roles × journeys is 12 × 5 and no one will exercise them
  all. The mitigation is that composition is *additive by construction* — features OR, packs
  skip-exist, gaps fill — so a combination cannot produce less than either half alone. The
  unit test asserts that property rather than enumerating pairs.
- **A bigger `.jichi/`.** Two packs means more agents and skills than either alone. That is
  the point of asking for both, but it is a real cost for someone who wanted a small tree.
- **The duplication remains.** `tester` and `small-project` still describe the same
  configuration; they are now combinable rather than rival, which resolves the user's
  question without resolving the redundancy. Left deliberately: merging them is a curation
  pass over the preset table, not part of making the model orthogonal.
- **The second question is one more screen.** Accepted; it defaults to "nothing in
  particular" so Enter costs one keystroke and reaches today's behaviour.

## Verification

- Unit: applying role-then-journey produces a superset of either alone (features, and no
  field regressing to unset), and `--preset <journey>` alone still resolves.
- Smoke (PTY): the wizard asks two questions, the second defaults to "nothing in particular",
  and picking one writes both packs.
- The existing `tests/smoke/setup.sh` must pass untouched — it is the back-compat check.
