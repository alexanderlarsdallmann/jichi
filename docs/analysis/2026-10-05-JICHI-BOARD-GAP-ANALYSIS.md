# JICHI BOARD — gap analysis (the prose that says the opposite of the code)

## TL;DR

*Read-only survey of `docs/BOARD.md` at HEAD `1b71247d`, cross-checked against
`src/chat/jc_board.c`, `src/tools/jc_tool_board.c`, `tests/test_board.c`,
`tests/smoke/board_concurrent.sh`, and `docs/ROADMAP.md`, 2026-10-05.*

The kanban board is one of the **best-implemented** small features in the
tree: a clean data model (id / title / note / state / phase, `jc_board.c`),
a named lock with a measured 5-second window (M761,
`JC_BOARD_LOCK_WAIT_MS`), a concurrency smoke test, a unit test with 34
assertions, a documented failure doctrine (M533, "only a board that exists
and cannot be read refuses a save"), and the focus-block injection BOARD.md
describes (`# Project board (focus)`, `jc_board.c:409`). `docs/BOARD.md`
itself is accurate on every claim I could reach.

The gap is **elsewhere, in the M436 ROADMAP prose that describes the board's
exclusion mechanism**: it says `todowrite`, `todoread` **and `board`**
"are `readonly: 1` — correctly, since they touch no filesystem and run no
command, only agent state." Two of those three words are wrong, and have
been wrong **since M436's own commit**:

- `board` is **`readonly: 0`** — the struct field is literally
  `0, /* mutating: writes .jichi/board.json -> permission-gated */`
  (`jc_tool_board.c:204`), and it has been **since the board's own
  creation** (2026-07-09, commit `82a15681`, where it was already
  `0, /* mutating */` — under the old project name `.jlu/board.json`).
  M436 (2026-08-14) wrote its "readonly: 1" five weeks after the fact.
- The board **does** touch the filesystem: every mutation is `jc_board_save`
  → `jc_write_file_atomic` to `<cwd>/.jichi/board.json` (`jc_board.c:290`).
  `todowrite`/`todoread` are genuinely `readonly: 1` (agent state,
  `jc_tool_todo.c:384,395`).
- The exclusion M436 describes — "refused at runtime by an `app->agent_depth
  > 0` check inside the tool's own body" — is not how the board is
  excluded: `jc_tool_board.c` has **no** `agent_depth` check in its body.
  The board is excluded by the **`main_agent_only: 1` flag**
  (`jc_tool_board.c:207`), enforced by the tool advertiser
  (`jc_tool.c:1112,1299`). That mechanism *is* correct and effective —
  subagents are not offered the board — but the prose names a different
  mechanism than the code implements.

## 1. The two concrete defects (both verified against source)

### 1.1 The M436 "readonly: 1" is false for `board` (prose error)

The M436 ROADMAP paragraph is a *defect-then-fix* narrative, and its
premise about the board is wrong:

| The M436 prose claims | The code says (at M436 **and** at HEAD) |
|---|---|
| `board` is `readonly: 1` | `readonly: 0` — `jc_tool_board.c:204` |
| the three tools "touch no filesystem" | `board` writes `.jichi/board.json` on every mutation — `jc_board.c:290` (`jc_write_file_atomic`) |
| excluded by "`app->agent_depth > 0` check inside the tool's own body" | excluded by the **`main_agent_only: 1` flag** — `jc_tool_board.c:207`; `jc_tool_board.c` contains **no** `agent_depth` reference; the enforcement is in the advertiser `jc_tool.c:1112` / `:1299` |

`todowrite`/`todoread` are genuinely `readonly: 1` and agent-state-only
(`jc_tool_todo.c:384,395`), so the sentence is *half* right. But it names
the board as a peer of the two todo tools, and the board is not a peer —
it is a **mutating, filesystem-writing** tool. A reader of the M436
paragraph who wants to verify "does `board` touch the filesystem?" will
find it does, and be told it does not.

This is the *M582 class* (a documented claim the code contradicts) applied
to the register's own milestone history: the M436 entry was written as a
recount of a defect, and the recount mis-states one of the three tools it
names.

### 1.2 The board's readonly/permission contract is asserted in the struct, not in the test

`tests/test_board.c` (34 assertions) covers the **data model** (add / move /
remove / set_phase / list / focus-block render) and one concurrency case.
It does **not** assert the two things that make the board *safe*:

- that a board **mutation is permission-gated** (i.e. that the tool is
  `readonly: 0`, so an untrusted or denied caller cannot write cards);
- that a **subagent cannot reach** the board (`main_agent_only: 1`).

The concurrency case is delegated to `tests/smoke/board_concurrent.sh`
("another writer's card survives" — the M761 lock). So the board's
*content* is well tested; its *access contract* rests on the struct
literal and the advertiser, which are correct but **unasserted**. The
readonly flag is the single bit that separates "shared read-only focus"
from "any agent can rewrite the board," and nothing in the suite would
catch it flipping.

## 2. What the board gets right (verified, so the finding stays honest)

- **Data model + persistence.** `jc_board.c` loads via `jc_read_file` +
  `cJSON_Parse`, saves via `jc_write_file_atomic` (atomic, no torn file),
  to `<cwd>/.jichi/board.json` (`jc_board.c:160,176,187,290`).
- **Failure doctrine (M533).** "Only a board that exists and cannot be read
  refuses a save" — `jc_board.c:248`; a board that can't be read is not
  silently destroyed.
- **Concurrency (M761).** `JC_BOARD_LOCK_WAIT_MS 5000L` on
  `<cwd>/.jichi/board.lock` (`jc_board.c:304`); `board_concurrent.sh`
  verifies two writers keep each other's cards.
- **Exclusion is effective.** `main_agent_only: 1` → `jc_tool.c:1112/1299`
  refuse the board at `agent_depth > 0`. Subagents are *not* offered it —
  the *outcome* the M436 prose wanted is achieved, by a flag rather than a
  body check.
- **The `jc_calib` reference in BOARD.md is fair.** Both use
  `jc_read_file` + `jc_write_file_atomic` + `cJSON` (`jc_calib.c:139,249`
  vs `jc_board.c:176,290`) — same load/save pattern, not a wrong citation.

## 3. Alternatives I considered and dropped

- **"BOARD.md's `jc_calib` reference is a wrong citation."** Dropped —
  false. Both modules use the identical `jc_read_file` +
  `jc_write_file_atomic` + `cJSON` pattern (`jc_calib.c:139,249` vs
  `jc_board.c:176,290`). It's a fair "same pattern" pointer, the kind the
  register uses throughout. I would have logged a defect that doesn't
  exist.
- **"The board is missing a permission test = the main gap."** Downgraded.
  `main_agent_only` and `readonly` are *flags* enforced in one advertiser
  (`jc_tool.c:1112/1299`); the same flags govern `write` and
  `apply_patch`. The test gap is real but shared with every mutating tool,
  so it's a *register-wide* property, not board-specific. Kept it as
  1.2 (a note), not as "the" finding.
- **"The board lacks a UI/TUI board view."** Checked — the doc's TUI
  behavior is implemented and the board is exposed via ACP
  (`jc_acp.c`). Not a gap.

## 4. The one thing the board surface definitely lacks

**A correct account of its own safety contract.** The board's *behavior*
(mutates `.jichi/board.json`, is permission-gated, is excluded from
subagents) is right and largely tested. What it lacks is the register's
*prose* matching that behavior: the M436 milestone — the document that
should be the authoritative record of "why the board is excluded from
subagents" — states the board is `readonly: 1`, touches no filesystem, and
sits behind the same exclusion story as the two readonly todo tools,
none of which matches the board's struct. The feature is more safely
built than its own changelog says.

## 5. What this means

- The M436 correction is a **one-paragraph rewrite** (it is a *reason*
  record; a wrong reason is worse than no reason, because it mis-teaches
  the mechanism for every future reader who trusts the milestone).
- It is also an instance of a register-wide risk: **milestone prose is
  written from memory of the defect, not re-read against the struct**, so
  a shared-sentence ("todowrite, todoread and board are readonly: 1") can
  be true for two of the three and wrong for the third without anything
  failing. The `jc_tool` flags are the ground truth; the ROADMAP is a
  paraphrase of it.
- The board's access-contract test gap (1.2) is worth one `JC_CHECK`
  asserting `jc_tool_board()->readonly == 0` and that the advertiser drops
  it at `agent_depth > 0` — cheap, and it pins the contract the doc
  (correctly) describes.

## 6. The corrections (ready to apply)

1. `docs/ROADMAP.md` M436 (line 23107-23112): correct the shared premise
   — `todowrite`/`todoread` are `readonly: 1` (agent state), but
   `board` is `readonly: 0` and writes `.jichi/board.json`, and it was
   never behind the body-level depth check M436 describes (that check
   lived in the todo tools pre-M436). All three are excluded from
   subagents by the `main_agent_only` flag enforced in `jc_tool.c`.
2. `tests/test_board.c`: add two assertions — `jc_tool_board()->readonly
   == 0` and `jc_tool_board()->main_agent_only == 1` — pinning the
   contract the M436 prose currently mis-states.
3. Optional: `docs/BOARD.md` "subagents do not get the board" — cite
   `jc_tool.c:1112` / the `main_agent_only` flag so the mechanism matches
   the code (it currently just asserts the outcome, which is fine).

## 7. Limitations (honest)

- I verified the struct flag, the persistence calls, the lock, and the
  advertiser exclusion against source at HEAD. I did not *run*
  `test_board.c` or `board_concurrent.sh` this session (read-only mandate);
  the claims about what they assert are read from the test files.
- "Since M436" is bounded: the board was created 2026-07-09 already
  `readonly: 0` (`.jlu/board.json`, the pre-rename name), so the flag has
  been `0` the board's whole life; M436 is when the *wrong prose* was
  written.
- The M436 `agent_depth`-in-body claim: I searched `jc_tool_board.c` and
  found no depth reference; the enforcement is the flag at
  `jc_tool.c:1112/1299`. That is the whole board tool, so the search is
  exhaustive for it.

## 8. Appendix: reproducible (all from the jichi checkout @ 1b71247d)

    # the board's actual readonly flag (0 = mutating)
    grep -n "mutating: writes" src/tools/jc_tool_board.c          # :204
    grep -n "main_agent_only" src/tools/jc_tool_board.c           # :207

    # the todo tools' flag, for contrast (1 = readonly)
    grep -n "readonly" src/tools/jc_tool_todo.c                   # :384, :395

    # the M436 prose under test
    sed -n '23107,23112p' docs/ROADMAP.md

    # the board was 0 from creation (2026-07-09), not since M436
    git log --diff-filter=A --format="%ad %h" --date=short -- src/tools/jc_tool_board.c | tail -1
    git show 82a15681:src/tools/jc_tool_board.c | grep -A1 board_schema,

    # persistence: board writes .jichi/board.json (jc_read + atomic write)
    grep -n "jc_write_file_atomic\|jc_read_file\|board.json" src/chat/jc_board.c   # 160,176,290

    # the subagent exclusion mechanism (the flag, in the advertiser)
    grep -n "main_agent_only &&" src/tools/jc_tool.c              # :1112, :1299

    # the test that asserts the data model, not the access contract
    grep -c "JC_CHECK" tests/test_board.c                          # 34
    grep -n "readonly\|main_agent\|permission" tests/test_board.c  # none

## 9. Verdict

The board is **built correctly and documented correctly in its own
page** — the data model, the M533 failure doctrine, the M761 concurrency
lock, and the subagent exclusion are all verified true in source, and
`test_board.c` + `board_concurrent.sh` cover the content. The one thing it
lacks is a **correct account of its own safety contract in the milestone
record**: the M436 entry that should explain "why the board is kept away
from subagents" mis-states the board as `readonly: 1`, filesystem-untouching,
and excluded by a body check, when it is `readonly: 0`, writes
`.jichi/board.json`, and is excluded by the `main_agent_only` flag. The
fix is a one-paragraph rewrite plus two test assertions. It is the
register's prose lagging its own code — the class of drift this audit
keeps finding, and here the code is the *more* careful of the two.

*Analysis 42. Read-only against jichi @ 1b71247d; no source changed.*
