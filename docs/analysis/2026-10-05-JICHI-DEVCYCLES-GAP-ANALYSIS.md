# jichi gap analysis — complex development cycles: where work lands (data flow)

Read-only. All paths under `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`),
evidence cited, inference flagged, reproducible appendix.

## TL;DR

jichi's support for multi-agent development is *excellent on the machinery*:
`AGENT_COLLABORATION.md` maps a shipped 3×3 (learning / working / supervising ×
human / agent), `spawn_parallel` gives isolated git worktrees with per-child
verify, `workflow` adds a deterministic pipeline with a measured refute stage
(M637: named 12 of 12 planted false claims), and the register even documents
the boundaries ("no agent-to-agent negotiation protocol, no shared mutable
agent memory, no swarm consensus"). But for **complex development cycles —
long-lived branches, parallel work, coordinating merges** — the one thing
definitely lacking is **a mechanism that tells the model where its work is
landing, and previews the operations that decide it**. The model is **never
proactively told its git branch** (it is not in the system prompt, the repo
map, or the envelope — verified: zero matches), so branch identity reaches it
only if it *happens* to call `git_status`. The git tool is an 8-op surface
(`status/diff/log/blame/add/commit/branch/stash`) and the operations that
decide *where work lands* — `git_branch` (checkout/switch), `git_commit` — go
through the **generic** approval ("name + dim argument summary",
`cb_confirm` tui.c:1693) rather than the **previewed** path, which
`render_edit_preview` (tui.c:1218-1223) restricts to exactly three file tools.
The register's own record carries **three incidents of work landing in the
wrong place**: ANECDOTES:6982 (a pool-merged change silently destroyed by
`git checkout` restoring from the index — "the pool's report listed no
conflict"), SUBAGENTS.md:65-66 ("write subagents committed straight onto the
team branch" — learned "only from its own records"), and DRIVING_A_TEAM.md:53-54
("Twice in one night a lead threw away merged work it had not committed"). All
three are answered by *prose documentation*, not by mechanism.

## 1. What exists — and why it is a genuine strength

- **The collaboration map is real and shipped** (AGENT_COLLABORATION.md):
  human↔agent (curriculum + tutor stance, `ask_user`, approvals, the shared
  `board`), agent↔agent (`attempt`/`improve` with mechanical two-sided
  verify, the mentor loop with human gate, authored specs), supervising
  (journals, `jichi control` socket, `doctor --unattended`, the jsonl
  driving contract, daemon fleets, QUEUE_PROTOCOL). Every cell names a
  runnable entry point.
- **Isolation is first-class.** `spawn_parallel` write children get their own
  git worktrees from the snapshot shadow, a per-child verify gate (M144: "a
  red child is quarantined — reported, not merged"), budget slices (M62 #4),
  first-wins merge with conflicts *reported* (M803 preserves losers).
- **The register is honest about the agent↔agent boundary** — "What
  deliberately does NOT exist: no agent-to-agent negotiation protocol, no
  shared mutable agent memory, no swarm consensus. The interfaces between
  agents are the same ones humans get — files, the board, budgets, journals,
  and verifiers."
- **The git tools are safe in the ways that matter.** argv-style with no shell
  (paths never shell-interpreted), registered only inside a work tree, the
  read-only set is genuinely read-only, `git_add` uses `--` so a path can't
  be an option, and the register *documents the refusal* to include
  push/pull/merge/rebase/tag: "Deliberately out of scope (network / conflict
  risk)" (GIT.md:58-59).
- **The team playbook is real** (DRIVING_A_TEAM.md): lead/worker/subagent/
  reviewer role table, "say where the worker works: its own worktree, by
  relative paths", "commit a merge before you touch it, and run a red check
  in a separate worktree" — written *from* the recorded incidents.

## 2. The one thing definitely lacking

**A standing answer to "where is my work going?" and a preview for the
operations that decide it.** Concretely three related seams, one root:

1. **The model is never told its branch.** Branch identity is absent from the
   system prompt, the repo map, the envelope, and the startup context
   (verified: `grep branch` across jc_sysmsg.c/jc_repomap.c/jc_envelope.c
   finds no branch injection). It reaches the model only via a voluntary
   `git_status`. Every branch-dependent operation — commit (lands on the
   *current* branch), `git_branch` switch, `git_stash`, the pool merge-back —
   is performed against a fact the model may not hold.
2. **The landing operations are confirmed, not previewed.** `git_branch`
   ("switch to branch X" — a working-tree change whose blast radius the user
   cannot see), `git_commit` (whose *target* is the current branch), and
   `git_add`/`git_stash` all go through the generic `cb_confirm`
   (tui.c:1693; M553's comment names the block: "glyph, tool name, dim
   argument summary"). The diff-preview path exists only for
   `write_file`/`edit_file`/`apply_patch` (tui.c:1222-1223, called at 1294).
   An operation whose *effect* is "move the working tree / record commits on
   branch N" gets a one-line argument echo, not a view of what will change.
3. **The merge/back-landing is file-level and incident-prone.** The
   first-wins merge (spawn_parallel) and the shadow-repo checkpoint/undo
   operate on files, not on branches; and the *user's* merge decisions
   (git merge/rebase) are deliberately outside the tools (GIT.md:58-59), so
   they happen in `run_terminal_command` — where the shell is "NOT fenced"
   (see analysis 26) and none of the git ergonomics (the `--` guards, the
   refusal-of-blank-message, the 32 KB cap) apply.

## 3. Why this is "definitely lacking" (alternatives and their status)

- *"`git_status` exists, so the model can check its branch."* It *can* — the
  same way it can check disk space before a heavy build. "Available on
  demand" is not "present in the model's working context every turn", and the
  three incidents below all occurred with the branch a fact the model *had
  not* re-checked. The repo map was promoted from on-demand to standing
  context for exactly this reason (analysis 24) — the branch never got the
  same treatment.
- *"The generic confirm is the right contract for git ops — the args ARE the
  command."* For `git_log` yes. For `git_branch` the argument is a name; the
  *effect* (which files will appear/disappear, whether the switch is clean,
  whether it detaches) is exactly what a preview would show and what
  `cb_confirm` cannot. For `git_commit` the argument is a message; the
  *target* (the current branch, which may not be the intended one) is not in
  the argument at all. A confirm that shows the input but not the effect is
  the M530 shape (analysis: "a preview must read every argument exactly as
  the executor will") applied in reverse — the executor's *effect* is unseen.
- *"The register declined push/pull/merge/rebase on purpose — that IS the
  branch safety."* True, and documented (GIT.md:58-59, "network / conflict
  risk"). But the *declined* set is the *loud* set (network, real merges);
  the *kept* set includes `git_branch` — a working-tree switch — and
  `git_commit` — a landing onto whatever branch is current. The risk the
  register is managing (conflict) is present in the kept set too, in the form
  of "landed on the wrong branch", which is not a merge conflict, so the
  stated rationale doesn't cover it.
- *"Subagents/parallel already handle isolation (worktrees)."* They handle
  *isolation of the working tree*, not *attribution of the landing*: the
  pool merges file-level (analysis 21), the write subagent commits *straight
  onto the live branch* (SUBAGENTS.md:62-67), and the ANECDOTES:6982 drop
  happened *after* a clean pool merge. Isolation ≠ "I know which branch this
  landed on".

## 4. Evidence

- **The model is never proactively told its branch.**
  `grep -rn "branch" src/chat/jc_sysmsg.c src/index/jc_repomap.c
  src/chat/jc_envelope.c` (excluding prose like "both branches now come")
  finds **no** branch injection into the standing context. The repo map
  (analysis 24), the envelope, and the system-prompt builders all omit
  branch identity. (Verified by search.)
- **The git tool's surface is 8 ops; the landing ops are kept, the merge ops
  declined.** `src/tools/jc_tool_git.c`: read-only `status/diff/log/blame`,
  mutating (M39) `add/commit/branch/stash`. GIT.md:58-59: "Deliberately out
  of scope (network / conflict risk): push, pull, merge, rebase, tag. Use
  run_terminal_command for those if needed." (Verified.)
- **The preview path is restricted to exactly three file tools.**
  `src/tui/jc_tui.c:1222-1223`: `render_edit_preview` returns unless the name
  is `write_file`, `edit_file`, or `apply_patch`; called at `:1294` (top-level,
  non-quiet only). `git_branch`/`git_commit` therefore get the generic
  `cb_confirm` (`:1693`): "name, dim argument summary" (M553's comment at
  `:1716` names the approval block as such) — an echo of the *input*, not a
  view of the *effect*. (Verified.)
- **The register's own record holds three "work landed in the wrong place"
  incidents, each answered by prose:**
  - ANECDOTES.md:6978-6990 — "A merge drop… The pool's report listed no
    conflict." A lead, to show a test failing, wrote the committed version
    over the merged file (`git show HEAD:... > file`), then "restored" it with
    `git checkout <file>` — "A checkout restores from the **index**, and with
    nothing staged that is the last commit… The merged change existed nowhere
    else." (Verified.)
  - SUBAGENTS.md:62-67 — "A team on the weekend drive learned this only from
    its own records (its write subagents committed straight onto the team
    branch), so it is stated here and in the tool's own description."
    (Verified.)
  - DRIVING_A_TEAM.md:53-54 — "Twice in one night a lead threw away merged
    work it had not committed." → the rule "commit a merge before you touch
    it, and run a red check in a separate worktree." (Verified.)
- **The collaboration machinery it sits beside is real and strong** (the
  counterweight that makes this the *narrow* lacking thing, not the whole
  area): AGENT_COLLABORATION.md's 3×3 with per-cell runnable entry points;
  `spawn_parallel` per-child verify + first-wins merge + M803 loser
  preservation (analysis 21); the measured refute stage (M637, 12/12 planted
  false claims named); the explicit "what does NOT exist" boundary. (Verified.)

## 5. Implications

- **The three incidents share a root the documentation addresses but the
  mechanism does not: the model acted on a branch it had not confirmed.**
  The write subagent "committed straight onto the team branch" because
  nothing told it a branch was even a fact to check; the lead destroyed a
  merge because `git checkout <file>` restored from the index *on the current
  branch* and the pool report (file-level, analysis 21) said "no conflict" —
  a clean landing on the wrong tree looks like no landing at all.
- **The git tools are the *trusted-voice* version of the file tools'
  contract.** M530's rule for the file-edit preview: "A preview must read
  every argument exactly as the executor will." (tui.c:1177-1178.) The git
  *landing* ops (branch/commit) have no preview
  of their *effect*, so the operator approves an argument they can read
  (`{"name":"main"}`) but not the consequence (a working-tree swap, a commit
  on a branch that may not be `main`).
- **The deliberately-declined set leaves the *kept* set's landing risk
  unmanaged.** push/merge/rebase are out (right, for conflict risk); but
  `git_branch` + `git_commit` on an *unchecked* current branch is a different
  flavor of the same "where does this land" risk, and it is the one the three
  incidents actually hit.
- **It composes with the other bounded-run seams.** In `--auto` (the
  unattended team drive, DRIVING_A_TEAM's setting), `git_commit` is
  auto-approved (no `cb_confirm`), the shell is not fenced (analysis 26),
  and the envelope is per-invocation (analysis 27) — so an unattended agent
  committing on an unchecked branch is the *least*-watched landing of all.

## 6. Recommendation

1. **Put the branch in the model's standing context** (a one-line "on branch
   `X`" in the system prompt / env block, refreshed when `git_branch` or the
   shell changes HEAD — the repo-map treatment, analysis 24, applied to
   branch identity). Cheap; answers the shared root of all three incidents.
2. **Preview the *effect* of the landing ops.** For `git_branch`: the files
   that will change on the switch (`git diff --name-only <cur>..<target>`)
   plus a dirty-tree warning. For `git_commit`: echo the *target branch* and
   the staged set before the confirm. This extends M530's "a preview must
   read every argument exactly as the executor will" contract from the three
   file tools to the operations that decide *where work lands*.
3. **Red-first driver: commit-on-wrong-branch.** In a scripted repo with two
   branches, have the model (mock) call `git_commit` while on `feature`
   intending `main`, and assert the approval shows the target branch. Before
   the fix: red (the confirm shows only the message). After: green. This is
   the SUBAGENTS.md:65 incident ("committed straight onto the team branch")
   made a test.

Rejected, in the register's spirit:

* *Add `git_merge`/`git_rebase` as tools.* The register declined those on
  purpose (conflict risk) and the decline is right; the incidents are about
  *landing on the wrong branch*, not about *merging*. Adding the loud ops
  would not have prevented any of the three.
* *Require the model to call `git_status` before every commit.* That turns a
  context gap into a ritual the model will skip exactly when tired — the
  three incidents *are* the model skipping the check. Standing context, not
  a required step.

## 7. Limitations and honest counterarguments

1. **This is a *seam* finding across three shipped, documented features, not
   a bug in one.** The isolation, the worktrees, the first-wins merge, the
   refusal of push/merge are all *correct* for their stated purpose. The
   lacking thing is narrow: *attribution of the landing* (branch identity in
   context + effect preview), not the mechanics.
2. **The three incidents are *recorded and documented*.** DRIVING_A_TEAM and
   SUBAGENTS *state* the lessons; a careful operator who reads them is
   protected. The gap is that the protection is prose in a playbook, not a
   mechanism in the approval path — the register's own standing distinction
   (a lint beats an audit that "found what it knew to look for"; analysis 23).
3. **`git_status` DOES show the branch** (`git status --short --branch`,
   GIT.md:13), so the information is *available*. I am claiming it is not in
   the model's *standing* context and not previewed at the landing op — the
   same claim shape as analysis 24 (repo map: on-demand → standing). I do not
   claim the branch is unknowable.
4. **I did not run the git tools against a live repo** (read-only). The
   "no branch injection" and "git_branch is generic-confirm" claims are
   verified by reading the sysmsg/repomap/envelope (no branch) and the TUI
   dispatch (render_edit_preview gates to 3 file tools; cb_confirm is
   name+summary). Step 3's driver executes the commit-on-wrong-branch case.

## 8. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

    # the model is never proactively told its branch
    grep -rn "branch" src/chat/jc_sysmsg.c src/index/jc_repomap.c src/chat/jc_envelope.c \
      | grep -viE "both branches|a branch|branches of"      # (expect none)

    # the git tool: 8 ops; the merge ops declined
    grep -nE '"git_[a-z]+"' src/tools/jc_tool_git.c | grep -oE "git_[a-z]+" | sort -u
    sed -n '58,59p' docs/GIT.md        # "push, pull, merge, rebase, tag … out of scope"

    # the preview path is gated to three file tools; git gets the generic confirm
    sed -n '1222,1223p' src/tui/jc_tui.c   # write_file/edit_file/apply_patch only
    sed -n '1294p'   src/tui/jc_tui.c       # where render_edit_preview is called
    sed -n '1693,1712p' src/tui/jc_tui.c    # cb_confirm: name + dim argument summary

    # the three "work landed in the wrong place" incidents
    sed -n '6978,6990p' docs/ANECDOTES.md   # the merge drop (git checkout from index)
    sed -n '62,67p'   docs/SUBAGENTS.md     # write subagents committed onto the team branch
    sed -n '53,54p'   docs/DRIVING_A_TEAM.md# a lead threw away uncommitted merged work (twice)

    # the strong machinery it sits beside (the counterweight)
    sed -n '1,16p' docs/AGENT_COLLABORATION.md   # the shipped 3x3
    grep -n "M637" docs/AGENT_COLLABORATION.md    # the measured refute stage (12/12)

**Verdict line for the register this could seed:** jichi's multi-agent
development machinery is genuinely strong and honestly bounded — the shipped
3×3 collaboration map (AGENT_COLLABORATION.md), isolated worktrees with
per-child verify and first-wins merge (spawn_parallel, analysis 21), a
measured refute stage (M637, 12/12 planted false claims), and a *documented*
refusal of push/pull/merge/rebase on conflict-risk grounds (GIT.md:58-59).
For complex development cycles — long-lived branches, parallel work,
coordinated merges — the one thing lacking is *attribution of the landing*:
the model is **never proactively told its git branch** (absent from the
system prompt, repo map, and envelope; available only via a voluntary
`git_status`), and the operations that decide *where work lands* —
`git_branch` (a working-tree switch) and `git_commit` (onto the current
branch) — are confirmed by a generic name+argument echo (`cb_confirm`,
tui.c:1693) rather than previewed for their *effect* (the M530 preview
contract, tui.c:1222-1223, is gated to the three file tools). The register
carries **three recorded incidents of work landing in the wrong place** —
ANECDOTES:6978 (a pool-merged change destroyed by `git checkout` restoring
from the index, "the pool's report listed no conflict"), SUBAGENTS.md:65
("write subagents committed straight onto the team branch", learned "only
from its own records"), DRIVING_A_TEAM.md:53 ("twice in one night a lead
threw away merged work it had not committed") — each answered by *prose in a
playbook*, not by a mechanism in the approval path. Definitely lacking:
(1) the branch in the model's standing context, refreshed on HEAD change
(the repo-map treatment, analysis 24, applied to branch identity); (2) an
*effect* preview for `git_branch`/`git_commit` (the target branch + the
files that will change), extending M530's "read every argument exactly as
the executor will" to the landing ops; (3) a red-first driver — commit on `feature` intending `main` —
that asserts the approval names the target branch. Revisit when: the model
always knows its branch, the landing ops preview their effect, and a test
pins the wrong-branch commit — at which point the three documented incidents
are structurally prevented, not merely recorded.
