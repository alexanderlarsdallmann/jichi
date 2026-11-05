# JICHI reading course — documentation review

*Read-only review of `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) at HEAD
`1b71247d` (2026-10-04). The subject: `docs/reading/` — the four guided
reading courses over jichi's own C source (Annai the guided tour, Fukabori
the deep dive, Tsuiseki the traced run, Kiroku the record), 31 files.*

## TL;DR

The review's headline is good news with two real catches: **the course is
99% current, and one of its two catches is a bug the existing guard should
have caught.** `reading_refs_lint.sh` (M222) was run this review and is
green (4/4: 31 files scanned, every backticked `path` and `file:function`
anchor resolves). I went further than the lint — it only proves every cited
name *exists*, so I re-checked all 79 `file:function` anchors against their
*definition sites*, and probed the behavioral claims the lint structurally
cannot read. Result: **one broken anchor** (`fukabori-07:91` cites
`jc_tool_subagent.c:jc_subagent_can_spawn`; the function is defined in
`jc_agent.c:5695` — and the *file* exists, which is exactly why the lint's
path check passes; the name merely appears once in it as a call), **one
inaccurate sentence** (`fukabori-09:14`: "the actual bytes on disk before
*and after* each mutating turn" — the code takes no after-turn snapshot;
only one, lazily, before the first mutation), and **one incomplete model**
(`fukabori-09` never mentions the green-checkpoint class — `jc_agent.c:4768`
"green: periodic", 2461 "green: %s" — which fukabori-06's own flowchart
references, making the omission an internal inconsistency between the
course's own chapters). Everything else probed (the three-arena model, the
loop ladder order, the envelope bounds, the fork-based-parallelism thesis,
the C89/one-dependency claims, all definition sites of the 79 anchors)
**holds against the current source.**

## 1. What was reviewed, and how

- **The guard, first.** `sh tests/smoke/reading_refs_lint.sh` → 1..4 all ok
  (31 files, every anchor resolves, index pages complete, stated anchor
  counts match). The M222 house rule ("this lint exists BEFORE the first
  chapter's prose") is kept.
- **Beyond the guard.** Three passes the lint does not do: (a) *definition
  sites* — all 79 unique `file:function` anchors checked that the function
  is actually defined in the cited file (not merely mentioned there);
  (b) *behavioral claims* — the model each chapter teaches, checked against
  the current code (checkpoint taxonomy, spawn backend, arena accessors,
  ladder order, envelope predicates); (c) *internal consistency* — whether
  the chapters agree with each other.
## 2. Findings — the two catches and one omission

### 2.1 Broken anchor (a lint bug, not a prose bug)

`fukabori-07:91` cites
`src/tools/jc_tool_subagent.c:jc_subagent_can_spawn` and paraphrases it as
"(`agent_depth < max_depth`)". The function is **defined in
`src/chat/jc_agent.c:5695`**; `jc_tool_subagent.c` merely *calls* it once.
The reader following the anchor to the cited file will find a call site,
not the definition.

**Why the lint missed it — the register's own trap, one step down.**
`reading_refs_lint.sh` checks each anchor two ways (its own header, line 13):
the *file* must exist, and the *name* must "appear followed by `(`" in that
file — a grep-level proxy for a definition. Both hold here (the file exists;
the name appears once in it, at a *call site*:
`jc_subagent_can_spawn(app->agent_depth, ...)`). The lint therefore passes an
anchor
whose **file half is wrong** even though every token individually resolves.
The three contracts the lint enforces are *token-level*; a
`file:function` anchor is a *pair*, and the pair is only sound when the
function is **defined** in the file. This is the same class M530 closed for
the approval preview ("a preview must read every argument exactly as the
executor will") applied to documentation: the *displayed* reference and the
*resolved* reference differ.

### 2.2 Inaccurate sentence (a true prose drift)

`fukabori-09:14` teaches the two-histories split and, for the file history,
says the workspace captures "the actual bytes on disk **before and after
each mutating turn**". The current code (`jc_agent.c`) takes **three**
snapshots, none of which is an "after each mutating turn" one:
- `:3658` — once, lazily, **just before the first mutating tool** of a
  top-level turn (this is the `/undo` story the chapter otherwise tells
  correctly);
- `:2461` — a **green** checkpoint when the verifier passes;
- `:4768` — **"green: periodic"**, a green checkpoint advanced on a
  passing verify in the unattended loop.

So the chapter's headline model ("one checkpoint per turn, before *and*
after") is wrong in both its count and its timing. The *before-first-
mutation* half is stated correctly in the flowchart on `:54`; it is the
prose on `:14` that over-claims.

### 2.3 Incomplete model — the green checkpoints

`fukabori-09` (sessions/snapshots) is the course's authoritative chapter
on the shadow git repo, yet the word "green" appears **zero times** in it.
The green-checkpoint class is real and load-bearing: it is the
*envelope's* known-good baseline that a failed unattended run rolls back
to, and it is referenced by **fukabori-06's own flowchart**
("pass → advance the green checkpoint"; "fail → … roll back to green").
A reader of chapter 9 comes away believing the snapshot repo holds only
per-turn `/undo` checkpoints; the rollback-to-green behavior they were told
about in chapter 6 has no home in the model. The two chapters are
internally inconsistent about the checkpoint taxonomy.

## 3. What holds (verified, not assumed)

- **Definition-site check, all 79 anchors.** Re-extracted every
  `file:function` anchor from the 31 files, de-duped to 79, and confirmed
  each name is *defined* in the cited file. The single failure is 2.1.
  Spot-checks that initially looked suspicious and proved fine:
  `jc_priv_detect` (defined `jc_priv.c:222`), `jc_kinetic_shell_match`
  (`jc_kinetic.c:200`), `jc_perm_for_tool`, `jc_env_path_in_scope`,
  `jc_env_run_verify`, `jc_env_budget_rollback_decision`,
  `jc_env_journal_begin` (all defined in the files the chapters cite).
- **The three-arena model (fukabori-03).** `app->arena` (session-lived),
  `jc_app_scratch()` (reset each top-level turn), and
  `jc_app_tool_scratch()` (per tool, reset after; subagents use scratch,
  then turn-arena) — all three accessors present in `jc_app.h:725-750`
  with the chapter's exact lifetime descriptions.
- **The loop ladder (fukabori-04).** Privileged (M802), kinetic
  (M817/M818/M820), permission, audit, checkpoint, control-channel — each
  named function exists and the "checkpoint lazily, just before the first
  mutating tool" claim matches `jc_agent.c:3652-3660` verbatim in intent.
- **The envelope (fukabori-06).** Four bounds (budgets / edit-scope fence /
  verify gate / JSONL journal) all named functions exist;
  `jc_env_budget_rollback_decision` is the pure predicate the chapter says
  it is.
- **Fork-based parallelism (fukabori-07).** The chapter's thesis — children
  are `fork`-ed, not threads — is *still true as the default*:
  `jc_spawn_backend()` (`jc_spawn.c:42`) returns the fork path unless
  `JICHI_SPAWN_BACKEND=spawn` selects `posix_spawn`. The chapter predates
  the M832 backend split and does not mention it, but it is not wrong; see
  §4 fix 3 (note the env knob, now that #33's env-var gap makes
  `JICHI_SPAWN_BACKEND` a first-class knob).
- **C89 + one dependency (fukabori-01).** The only external header is
  `<curl/curl.h>` (one `#include` form across `src/`). Holds.
- **Streaming invariants (fukabori-08).** The no-buffering / SSE
  exhaustiveness invariants are described as enforced structurally; the
  TTY detection (`jc_term.c:25` `isatty`) and `jc_sse_feed` the chapter
  cites are present and unrenamed.

## 4. The correction set (ready to apply to jichi's source)

> The user asked whether updates/corrections are necessary. They are — four
> small ones, all in `docs/reading/` plus one lint change. None is a rewrite;
> the course is otherwise sound.

1. **`fukabori-07:91`** — point the anchor at the definition:
   `src/chat/jc_agent.c:jc_subagent_can_spawn` (the `jc_tool_subagent.c`
   line can stay as "the tool calls it"). One-line fix.
2. **`fukabori-09:14`** — replace "before and after each mutating turn"
   with the actual model: a checkpoint is taken **once, just before the
   first mutating tool of a turn**, plus a **green** checkpoint advanced on
   a passing verify. (The flowchart on `:54` already says "before first
   mutation"; make the prose agree with its own diagram.)
3. **`fukabori-09`** — add a short "green checkpoints" paragraph so the
   chapter 6/9 taxonomy matches (rollback-to-green baseline), and note the
   `JICHI_SPAWN_BACKEND` fork-vs-posix_spawn split in chapter 7 now that it
   is a first-class knob.
4. **`tests/smoke/reading_refs_lint.sh`** — tighten contract 2 from
   "name appears in the file" to "**name is defined in the file**"
   (a C definition: `name(` at statement start, not a call). This is the
   class-fix: it makes 2.1's error *unrepresentable*, not just patched.
   Red today: `jc_tool_subagent.c:jc_subagent_can_spawn`. Green after fix 1.

## 5. Verdict

The reading course is in **excellent shape**. It is one of the most
disciplined documentation surfaces in the register: a dedicated existence
lint predates the first chapter, every index states its ground rules, and
a 79-anchor definition-site sweep turns up exactly **one** broken anchor
(a lint blind spot, not carelessness). The prose drift is real but
contained to **two sentences** (fukabori-09's "before and after" line and
its silent omission of green checkpoints), and both are the *stale
description* class the user was right to suspect — the code gained a
checkpoint taxonomy the chapter never updated for. Applying the four fixes
above closes the gap and, in #4, makes the broken-anchor class
structurally impossible. Revisit when: the definition-site lint passes on
all anchors and fukabori-09 names the same checkpoint classes fukabori-06
does.

## 6. Appendix: the review, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

    # the existence lint (green, 4/4) and what it checks
    sh tests/smoke/reading_refs_lint.sh
    sed -n '1,30p' tests/smoke/reading_refs_lint.sh     # the three token-level contracts

    # every file:function anchor, de-duped (79 real; 2 more are the
    # `file.c:function_name` placeholders the convention sections quote)
    grep -rhoE '`[a-zA-Z0-9_/.]+\.(c|h):[a-zA-Z_]+`' docs/reading/*.md | tr -d '`' | sort -u | grep -v '^file.c' | wc -l

    # the one bad anchor: cited file vs real definition
    sed -n '91p' docs/reading/fukabori-07-fork-based-parallelism.md
    grep -rnE '^[a-zA-Z_][a-zA-Z0-9_ *]*\bjc_subagent_can_spawn\(' src/ --include=*.c   # jc_agent.c:5695
    grep -c 'jc_subagent_can_spawn' src/tools/jc_tool_subagent.c                          # 1 (a call)

    # the two-histories drift: prose vs code
    sed -n '14p;54p' docs/reading/fukabori-09-sessions-snapshots-two-histories.md
    grep -n 'jc_snapshot_take' src/chat/jc_agent.c        # 3 sites: 2461 green, 3658 first-mutation, 4768 periodic-green

    # the green omission (internal inconsistency across ch.6 and ch.9)
    grep -c green docs/reading/fukabori-09-sessions-snapshots-two-histories.md   # 0
    grep -n 'green checkpoint' docs/reading/fukabori-06-the-autonomy-envelope.md

    # the holds (spot-verified)
    grep -n 'jc_app_scratch\|jc_app_tool_scratch' include/jc_app.h               # three-arena model
    grep -n 'jc_spawn_backend\|posix_spawn\|fork()' src/util/jc_spawn.c          # fork is the default
    grep -rhoE '#include <curl/curl.h>' src/ --include=*.c | sort -u             # one dependency
