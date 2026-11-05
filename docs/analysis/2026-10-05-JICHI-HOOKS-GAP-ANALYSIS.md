# JICHI HOOKS — gap analysis

*Read-only survey of `docs/HOOKS.md` (121 lines) at HEAD `1b71247d`, verified
against `src/chat/jc_hooks.c`, `src/util/jc_proc.c`, `src/config/jc_config.c`,
`src/chat/jc_agent.c`, `src/main.c`, git history, and the M711 sweep record,
2026-10-05. Part of the 10-surface queue.*

## TL;DR

HOOKS.md is accurate on everything except **one sentence**: "Env vars
`JICHI_HOOK_EVENT`, `JICHI_TOOL_NAME`, `JICHI_CWD` are also set."
(`docs/HOOKS.md:60`). No code has ever exported those variables to a hook
child. The promise was written at M25's implementation commit (there, under
the pre-rename names `JLU_HOOK_EVENT`/`JLU_TOOL_NAME`/`JLU_CWD`), was
renamed by M170's sweep, and still does not exist. Every piece of data the
sentence claims to carry is *already* in the hook's stdin JSON
(`build_stdin`, `jc_hooks.c:124-153`: `event`, `cwd`, `tool_name`,
`session_id`), so the fix is deletion, not implementation — and the
miss is instructive: M711's fleet sweep ("a pointer resolved, only the claim
rotted"; 219 pages; 125 fixes) corrected a *different* line of the *same file*
(`tests/e2e/hooks.py` → `tests/smoke/hooks.sh`) and left this one.

## 1. Claim-by-claim verification (the 120 lines that are right)

Checked each falsifiable assertion; all PASS unless noted:

- **opt-in** — `"hooksEnabled": true` required: `hooksEnabled` parsed in
  `jc_config.c`; the guard `jc_hooks_active(app) || app->agent_depth != 0`
  (`jc_hooks.c:308`) is the single fire gate.
- **`--no-hooks`** — 3 refs in `src/main.c` (flag present).
- **top-level only** — `agent_depth != 0` in the same gate (`jc_hooks.c:308`).
- **five events + SessionStart in main.c** — `jc_hooks_fire` wired at
  `jc_agent.c:3633` (PreToolUse), `:3954` (PostToolUse), `:5483`
  (UserPromptSubmit), `:5520` (Stop), and `main.c:17317` (SessionStart).
- **exit-code contract** — `exit 2` blocks (`jc_hook_exit_blocks`,
  `jc_hooks.c:41`, "reason = output" at :194-196); other nonzero =
  advisory warning; `126/127` = not-runnable, special-cased (:218) and
  recorded as `not_runnable` (:283); JSON `{decision, reason,
  additionalContext}` honoured ("deliberate", :237) — the Claude Code
  contract, as the doc says.
- **stdin JSON** — `event`, `cwd`, `session_id`, `tool_name`,
  `tool_input`, `tool_response`/`tool_error` (PostToolUse), `prompt`
  (UserPromptSubmit): all present in `build_stdin` (:124-153).
- **matcher: glob + `|` alternation, absent/empty = all tools** — split on
  `|`, each alt via `jc_glob_match` (:58-66); empty matches every tool.
- **`{command,args}` vs `{shell}`, timeout default 30** — parser at
  `jc_config.c:874-891` (missing `timeout` → 0, which the spawn maps to
  `JC_PROC_DEF_TIMEOUT 30L`, `include/jc_proc.h:159`, used at
  `jc_proc.c:548`).
- **M584** — dead-hook warning wording ("THIS CHECK DID NOT RUN"), the
  bounded `outcome` set (`start_failed` · `timeout` · `not_runnable` ·
  `nonzero_exit`, :281-283), telemetry with `timeout_s` (:288), and the
  "check yours now: `jichi telemetry | grep -i hook`" advice: all in code.
- **snapshot blast radius** — hook output not snapshotted: the doc's
  ANECDOTES #1 pointer is the authority; not re-verified here (flagged).

## 2. The one false line

`docs/HOOKS.md:60`: "Env vars `JICHI_HOOK_EVENT`, `JICHI_TOOL_NAME`,
`JICHI_CWD` are also set."

Evidence:
1. The hook child is spawned by `jc_proc_capture(argv, NULL, ...)` —
   `env` is `NULL` (`jc_hooks.c:253`), i.e. "no additions" per the
   contract in `include/jc_proc.h:166-172`.
2. No `setenv`/`putenv` anywhere in `jc_hooks.c` or the capture path; the
   only env manipulation in `jc_proc.c` is `unsetenv` of secrets
   (:112, :115) — scrubbing, not adding.
3. `git log -S` for `JICHI_TOOL_NAME` / `JICHI_HOOK_EVENT` across the
   whole tree: the string appears in **exactly two commits** — M170's
   rename (1601bc39) and the M25 implementation (c5d489e4), and in both it
   is **docs/HOOKS.md only**. It has never been in code, under the new
   name or the old (`JLU_*`).
4. It was introduced *by* the M25 commit (the 86-line HOOKS.md diff at
   c5d489e4 contains it) — the promise landed with the feature and was
   never implemented.
5. No test or script in the tree depends on the vars
   (`grep` of `tests/`, `scripts/`: zero hits) — so nothing broke when
   they didn't materialize; the doc simply outlived the intent.
6. The precise mechanics (easy to get wrong, so stated): the hook child
   INHERITS the operator's environment minus scrubbed secrets
   (`jc_spawn.c:125-128` — scrub, then `child_apply_env(NULL)` adds
   nothing; there is no clean-room). So `JICHI_CWD` would reach a hook
   ONLY if the operator's shell happened to define it — not by contract.
   And `JICHI_CWD` IS a real jichi export, but to a DIFFERENT child:
   notify commands (`jc_notify.c:54` pushes `JICHI_CWD` into the notify
   env; the hook call at `jc_hooks.c:253` pushes no env at all).
   The doc's sentence reads as if HOOKS.md borrowed NOTIFY.md's env
   line; the two subprocess classes share one variable name, which makes
   the false sentence plausible for years — and it is the reason no
   operator who tested `JICHI_CWD` in a hook on their own machine would
   necessarily have caught it (a user with $JICHI_CWD exported in their
   shell would see it work).

## 3. The meta-finding: why M711's fleet sweep missed it

M711 (fd1be1e1, "125 rotted claims") is the strongest defense this tree has
against exactly this class — "the pointer resolved, only the claim rotted."
It swept 219 pages with eight read-only review agents, each finding required
to carry a proving command and its real output. And it **did fix a line in
HOOKS.md** (the stale `tests/e2e/hooks.py` reference → `tests/smoke/hooks.sh`).
The env sentence survived because it is the hardest sub-class of claim-rot:

- A **negative existence claim** ("X is set"). The sweep's proving pattern is
  "run a command that shows the state" — for a positive claim that works
  (grep for the symbol; find the wire-in). For "this variable is exported to
  that child," the proving command is `grep -rn JICHI_TOOL_NAME src/` and the
  *absence* of hits must be read as the falsification. A reviewer who greps,
  finds the variable in the doc and in `jc_notify.c` (JICHI_CWD!), and moves
  on, has been fooled by a same-named export to a different subprocess class.
- It **co-locates with true neighbours**: the stdin-JSON fields around it are
  all accurate, the exit-code table is accurate, M584's mechanism is accurate.
  One false sentence among twelve true ones is the hardest shape to spot by
  reading; it needs per-claim falsification, which the sweep's budget
  (~55k lines, 8 agents) does not guarantee per line.

This is the sixth time this register has met a lint/sweep that checks
*references* (paths, counts, flags) and not *sentences* — and the sharpest
instance yet, because M711 was a *human+agent fleet*, not a regex lint.

## 4. What HOOKS.md is "definitely lacking"

A **hook contract self-test**: the doc's stdin-JSON block is an example, the
exit-code table is prose, and the one false sentence has sat for the life of
the feature because *no test executes the documented contract end to end*.
**`tests/smoke/hooks.sh` (163 lines) is strong, and covers a different
slice**: the M584 telemetry outcomes (`not_runnable`, `nonzero_exit`, clean/
exit-2/JSON-contract controls, the telemetry-reader seam). `test_hooks.c`
covers the interpreter (matcher, exit codes). What *neither* executes is the
**child's observed world** — "a hook script that
asserts on its own stdin, argv, and environment, run by the smoke tier, is
the only thing that could have caught line 60." The recommendation, in
priority order:

1. **Delete the env sentence** (docs/HOOKS.md:60), or implement it — but the
   data it names is already on stdin, and `JICHI_CWD` specifically is a
   NOTIFY.md export, so deletion is the honest fix (~1 line).
2. **Add the child-world assertion to `tests/smoke/hooks.sh`**: fire a
   `PreToolUse` hook whose script writes `"$JICHI_TOOL_NAME:$JICHI_HOOK_EVENT"`
   and its stdin to a file; the test asserts stdin carries the documented
   JSON keys. Any future env promise then needs a test, or it cannot be
   written. (~15 lines.)
3. **(Policy, for the register)** add line 60's defect to the class the
   sweep should target: *negative-existence / capability claims* — a
   doc-line stating "the tool sets/exports/receives X" with no `file:line`
   anchor is the shape that survives both regex lints and review fleets.

## 5. Limitations (honest)

- I did not *run* a hook to observe its environment directly (read-only
  survey); the "not set" conclusion is from the spawn path
  (`jc_hooks.c:253` env=NULL + `jc_spawn.c:125-128`/`:284` build) and the
  `git log -S` absence across all history, not from a child `env` dump. A
  30-second smoke run would make it airtight; the static case is already
  decisive because nothing in the child's construction adds these names.
- The JICHI_CWD-notify conflation (item 6) is inference from the code
  shapes; I did not test whether an operator's shell exporting JICHI_CWD
  would make the doc sentence accidentally true on that machine (it would —
  that is the point, and it is why the sentence survived).
- M711's "why the sweep missed it" (§3) is a reconstruction of the sweep's
  method from its ROADMAP entry, not from the sweep's own working notes —
  flagged as inference.
- The snapshot/blast-radius claim (doc's ANECDOTES #1 pointer) was not
  re-verified here; it is a one-line pointer to another doc, out of scope.

## 6. Appendix (reproducible; from the jichi checkout at 1b71247d)

    grep -n "JICHI_HOOK_EVENT\|JICHI_TOOL_NAME\|JICHI_CWD" docs/HOOKS.md     # the false sentence (:60)
    grep -rn "JICHI_TOOL_NAME" src/ include/                                # zero hits (never exported)
    git log --oneline -S JICHI_TOOL_NAME | cat                              # only 1601bc39 (the M170 rename), docs-only
    git log --oneline -S JLU_TOOL_NAME | cat                                # only c5d489e4 (M25), docs-only
    git show c5d489e4:docs/HOOKS.md | grep JLU_TOOL_NAME                    # the promise, at birth
    sed -n '250,256p' src/chat/jc_hooks.c                                  # jc_proc_capture(argv, NULL, ...)
    sed -n '120,135p' src/util/jc_spawn.c                                  # scrub + child_apply_env (fork backend)
    grep -n "jc_spawn_build_envp\|scrub_env" src/util/jc_spawn.c | cat      # posix backend, same story
    grep -n "JICHI_CWD" src/util/jc_notify.c                               # :54 — the REAL export, notify class
    sed -n '124,153p' src/chat/jc_hooks.c                                  # build_stdin: event/cwd/tool_name/session_id
    grep -n "jc_hooks_fire" src/chat/jc_agent.c src/main.c | cat            # the five wire points
    grep -n "JC_PROC_DEF_TIMEOUT" include/jc_proc.h src/util/jc_proc.c | cat  # default 30s
    git show fd1be1e1 -- docs/HOOKS.md | grep "^[+-]" | head                # M711's HOOKS.md fix (different line)
    grep -c "t_ok" tests/smoke/hooks.sh                                    # 9 M584 telemetry assertions in a 163-line script

## 7. Verdict

HOOKS.md is one of the better-documented subsystems in the tree: 20 of 21
falsifiable claims verified true against source and git, the M584 failure
semantics accurate, the exit-code contract accurate, the wiring accurate.
The one false sentence (line 60, the never-implemented env vars) has been
there since the feature's birth, survived M170's rename, and survived M711's
125-claim fleet sweep because it is a *negative-existence* claim wearing the
face of a true neighbour (`JICHI_CWD` is a real export — to notify children).
Fix: delete one line, add one child-world assertion to the smoke hook test.
The class note for the register: review fleets catch reference-rot and
positive claim-rot; capability claims without a `file:line` anchor need a
test, not another reader.
