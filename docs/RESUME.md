# Resuming work: what a new jichi session inherits, and how to brief it

*This page is for the operator (the person who runs jichi) and for the agents
that drive jichi. Written 2026-10-11 after a real handoff: a week of milestone work
crossed several sessions, and the question "what prompt does the next
session need?" had to be answered from files, because the answer is NOT in
any one file. This page makes it so. It is a reference, not a post-mortem;
the dated measurement it rests on is
[analysis/2026-10-11-context-management-analyzed.md](analysis/2026-10-11-context-management-analyzed.md).*

## 0. The answer first

A fresh session (after `/clear`, or a new process with `--continue`) inherits
**four** things, and nothing else from the conversation:

| Channel | Lives in | Survives `/clear` | Carries |
|---|---|---|---|
| The model's working memory | the context window | **no** | the conversation, the todo list, the in-flight reasoning |
| Project memory | `.jichi/memory.md` (loaded into every prompt) | yes | durable facts: commit hashes, process rules, gotchas |
| The plan | `.jichi/PLAN.md` | yes (it is a file, read on demand) | what remains, in order, and the falsifier |
| The repository itself | git history, working tree, `docs/` | yes | the ground truth: what is committed, what a doc cites |

Everything a resumed agent needs that is NOT in those four channels has to be
**in the prompt**. This page is the pattern for writing that prompt, and the
reasons each piece of it is there -- including the two that are easy to omit
and both of which have cost real work (section 5).

## 1. What `/clear` does, and what it does not

`/clear` resets the **conversation**: the history the model sees, and the
session's todo list. It does not touch the files. Concretely, after `/clear`
in the same project directory:

- **Gone:** the conversation (including any `[Earlier conversation
  summarized to save context]` block, which was itself the residue of a
  compaction -- see the analysis page), the `todowrite` list, any in-flight
  plan-mode state.
- **Still loaded:** the rules file (`AGENTS.md`, or `CLAUDE.md` as the
  fallback -- and NEVER both, an `AGENTS.md` shadows the rest, so do not
  create one here), `.jichi/memory.md` (up to the 8 KB injection budget;
  when it overflows, the OLDEST notes stop being loaded, and the truncation
  is today warned to stderr only -- whether the loaded block should state it
  is open decision D5 in the zigodot decisions page), the project map, the
  skill catalog.
- **Still on disk:** `.jichi/PLAN.md`, `.jichi/board/`, the session files
  (a `--continue` run can resume the OLD conversation from disk; a fresh
  process can pick it up by id), git, and everything `docs/` says.

The distinction that matters: **`/clear` is a model-side reset;
`--continue` is a model-side restore; the files are the only thing both
leave alone.** A prompt that briefs the agent well makes the difference
irrelevant -- the files carry the truth either way.

## 2. The state channels, one by one

### 2.1 `.jichi/memory.md` -- the durable notes

Every note is loaded into every session, so a note is a FACT, not a task:
"M929 is committed at 1d8b74f5", "the gate runs once per series of
milestone commits, not per commit", "the LSP sub-second-clock design was
drafted and reverted -- here is what the mock needs". A note that says "TODO: fix X" is
a lie the next session inherits.

The 8 KB budget means memory is also a **queue with an eviction policy**: the
oldest notes stop being loaded. The working pattern is to prune the note
whose work is now recorded somewhere more authoritative (git, the ROADMAP,
docs/) down to a one-line pointer, rather than deleting the lesson.

### 2.2 `.jichi/PLAN.md` -- the remaining work

Written by the `write_plan` tool (and it is a file, so an operator or the
agent can edit it). The shape that survives a handoff is the five-part plan:
**claim** (what remains, in order), **rejected** (the alternatives already
argued against -- so the next session does not relitigate them), **falsifier**
(what would show the plan wrong), **not goals**, **touches**.

**The failure mode, measured:** a plan file is a snapshot. After a series of
commits, its "remaining work" list is stale unless someone rewrites it. The
rule is in the claim itself: pin the plan to a commit ("state: master =
3d20a37a") and **enumerate the remaining work from git log on the exact
head**, never from the plan text alone -- the plan is the index, git is the
truth, and the two have been caught disagreeing.

## 3. The prompt: the six pieces, in order

The resume prompt that was actually used this week (reproduced so the
pattern is concrete):

    Resume the fix-order work. State: master = 3d20a37a (M929 done + zigodot
    decision doc pushed). Read .jichi/PLAN.md (the Claim section has the
    remaining items in order) and .jichi/memory.md for context.

    Today's priorities, in order:
    1. The zigodot decision doc
       (docs/internal/2026-10-11-zigodot-drive-decisions.md) is awaiting my
       review -- I will answer D1-D6 when we talk; do not implement any of
       them yet.
    2. Branch tidy remainder: 13 branches left -- 4 are checked out in
       worktrees (remove those first), the 2 measure/ branches carry new
       files (import into master, then delete), the rest are stale ancestors
       (delete, 2-dot-diff verified).
    3. Then the two LSP review fixes: R09-F1 (the false-clean -- needs a
       deterministic mock LSP for red-first) and R09-F2 (UTF-16 columns).
    One make ci on the final head, ff-merge, push.

Why each line is there:

1. **"State: master = 3d20a37a."** A fresh session verifies against live
   git, but naming the pin up front catches drift in the first tool call
   instead of after an hour of work on the wrong base. The hash, not a date:
   "the state from Tuesday" is not a state.
2. **"Read .jichi/PLAN.md and .jichi/memory.md."** memory.md is auto-loaded,
   so this line really names PLAN.md -- but it says so explicitly, because
   a resumed agent that never opens the plan file works from the index the
   conversation used to be, and the index is stale by design (2.2).
3. **The ordered list.** Order is a decision the previous session made and
   the next session should inherit, not rediscover. "Do the branch tidy
   first" exists because it is mechanical and low-risk, and it stops new
   milestone branches piling on the clutter.
4. **"Do not implement any of them yet."** The single most important line,
   and the one a prompt-writer is most likely to omit. See 5.2.
5. **The process rule restated.** "One make ci on the final head, ff-merge,
   push" is in memory.md -- but a process directive that changes how the
   work is *gated* is cheap to repeat and expensive to get wrong, and the
   memory note is subject to eviction.

**The anti-pattern:** a prompt that says "continue the work". The agent
will, faithfully -- from its first tool call onward it has no work. It has
a repository. The prompt is the bridge between the two.

## 4. Writing it: the session that ends is the one that writes the next

The resume prompt cannot be written well in a fresh session, because the
fresh session does not know what the old one knew. The rule: **the ending
session writes the state.** Concretely, when a session knows it is nearing
the end (the pressure note says so, the operator says "wrap up", or the
work is done):

1. Update `.jichi/PLAN.md` to the remaining work, pinned to the exact head
   (`git rev-parse --short HEAD`), with the next session's ordering.
2. Prune `.jichi/memory.md`: promote finished work to one-line pointers
   (the ROADMAP and git carry the detail), keep the process rules and the
   in-flight designs.
3. Write the resume prompt (section 3) into the handoff -- the message to
   the operator, or a file in `docs/internal/` if the handoff crosses
   machines.
4. Verify the working tree is clean or the dirt is named: a resumed agent
   that finds unexplained changes has to spend its first minutes
   archaeologising them.

The context-management analysis page (2026-10-11) measures why this matters:
the model-summarizing autocompact only runs *between* turns, so a long
headless turn is carried by elision alone, and the `[context] ... write
durable results now` note is a nudge, not a fence. The session that ends is
the only session that still has the full picture.

## 5. The two measured failure modes (what this page exists for)

### 5.1 The plan that was never updated

This week, `.jichi/PLAN.md` still listed R06-#1 as to-do *after* R06-#1 was
committed, gated and merged -- it had been written at the start of the task
series and no session rewrote it. A resumed agent reading only the plan
would have re-implemented M929. The fix was mechanical (rewrote the plan,
pinned it to 3d20a37a), and the rule is 2.2: **the plan is the index, git
is the truth; the prompt says so, and the ending session keeps them
honest.**

### 5.2 The decision doc that got implemented

The zigodot decision page is titled "decisions, laid out for review" and
every section says "recommendation to discuss" -- but to an agent that
opens it, a recommendation with a rejected-alternatives analysis *looks
decided*. The prompt therefore carries the override: "awaiting my review --
do not implement any of them yet". The general rule: **anything that is a
proposal, not a decision, needs its status in the prompt, because a file
that reads like an instruction will be followed like one.** The same applies to a
reverted design ("the LSP fix was drafted and REVERTED, untested"), and to
"open" findings that look "closed" because the code now exists.

### 5.3 (For the junior developer) what to check in the first five minutes

A resumed agent -- human or jichi -- should verify, in this order, before
touching anything: `git status` clean? `git log -1` matches the prompt's
state pin? Does `.jichi/PLAN.md`'s "remaining work" still match `git log`
on that head (enumerate a second way: the plan says three items, count the
findings in the review docs that have no closing milestone)? If any of the
three disagree, STOP -- the prompt or the state is stale, and the cheapest
fix is a message to the operator, not an assumption.


## Read further

- [analysis/2026-10-11-context-management-analyzed.md](analysis/2026-10-11-context-management-analyzed.md) — the dated measurement this page's prompt pattern is built on (why the ending session is the only one with the full picture).
- [SESSION_RUNBOOK.md](SESSION_RUNBOOK.md) — the operator's session steps, which this page's "brief the next session" rule feeds.
