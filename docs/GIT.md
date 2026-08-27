# Git tools & self-review

Two complementary capabilities give the agent version-control awareness and a
second look at its own work.

## Read-only git tools

When the workspace is a git repository, four **read-only** tools are advertised
to the agent (and usable in plan mode / read-only subagents):

| Tool | Args | Runs |
| --- | --- | --- |
| `git_status` | — | `git status --short --branch` |
| `git_diff` | `path?`, `staged?` | `git diff [--staged] [-- <path>]` |
| `git_log` | `path?`, `max?` (≤100, def 20) | `git log --oneline -n <max> [-- <path>]` |
| `git_blame` | `path`, `start?`, `end?` | `git blame [-L start,end] -- <path>` |

These read the **user's own repository** (`git -C <cwd> …`), separate from the
snapshot shadow repo used by `/undo`. They're invoked **argv-style with no
shell**, so model-supplied paths are never shell-interpreted; output (with
stderr merged) is capped at 32 KB. The tools are registered only when
`git rev-parse --is-inside-work-tree` succeeds, so they're never advertised
where they can't work.

Use them to see what changed (`git_diff`), gauge a change's blast radius and
recent history (`git_log`), or find why a line exists (`git_blame`).

## Mutating git tools (M39)

So an agent can **record its own work** (the read-only tools could only observe),
four mutating tools are advertised in the same git repo:

| Tool | Args | Runs |
| --- | --- | --- |
| `git_add` | `paths?` (array), `all?` | `git add -- <paths>` or `git add -A` |
| `git_commit` | `message` (req), `all?` | `git commit [-a] -m <message>` |
| `git_branch` | `name` (req), `create?` | `git checkout [-b] <name>` |
| `git_stash` | `pop?`, `message?` | `git stash push [-m <msg>]` or `git stash pop` |

These are **mutating** (`readonly = 0`), so they flow through the normal
permission gate (`jc_perm`) exactly like `edit_file` / `run_terminal_command`:
they **ask** in chat mode, are **auto-approved** under `--auto` / AUTO mode, and
are **hidden** in plan mode's read-only fence. They run argv-style with no shell
(same `git_capture` as the readers), against the user's own repo.

Guards and ergonomics:

- `git_commit` refuses a blank/whitespace `message` before running git, and
  returns git's own summary (the `[branch sha] message` line + stat) so the
  model sees the new commit; a failure (e.g. *nothing to commit*) comes back as
  git's stderr with `is_error` set.
- `git_add` requires either `paths` (staged after a `--` separator, so a path
  can't be read as an option) or `all: true`.
- `git_branch` switches by default; `create: true` makes the branch from HEAD and
  switches to it.
- `git_stash` saves by default; `pop: true` restores the latest stash.

Deliberately **out of scope** (network / conflict risk): `push`, `pull`,
`merge`, `rebase`, `tag`. Use `run_terminal_command` for those if needed.

These touch the **user's** repository, independent of the snapshot shadow repo
behind `/undo` — so `/undo` still reverts file changes, while these record them
into real history.

## Self-review

Before finishing a **top-level turn that changed files**, the agent can review
its *own* diff once — checking for bugs, regressions, leftover debug code, and
whether the change actually addresses the request — and fix any problems before
returning. It mirrors the autonomy envelope's fix-forward: the turn's diff is
fed back as a user message and the agent gets one more round; a one-shot guard
ensures exactly one review pass per turn.

The diff shown is the turn's changes since the pre-edit checkpoint, produced from
the **shadow** repo (`git add -A` into the shadow index, then
`git diff --cached <checkpoint>`) — so it includes new files and never touches
the user's own `.git`/index. Self-review therefore requires snapshots to be
enabled (the default). When it runs, jichi logs `[self-review]` and (under
`--auto`) records a `self_review` event in the audit journal.

### When it runs

Default: **AUTO (`--auto`) runs only** — the unsupervised case where a self-check
adds the most safety. Interactive/headless edits add no extra round by default.

| Config `selfReview` | Effect |
| --- | --- |
| unset (default) | on in AUTO mode only |
| `true` | on in every mode |
| `false` | off everywhere |

CLI overrides: `--review` forces it on, `--no-self-review` forces it off. In the
TUI, `/review` toggles it for the session.

```json
{ "selfReview": true }
```

### Cost

Self-review adds one model round to each editing turn it runs on (bounded: the
diff fed back is capped at ~8 KB). That's why the default is AUTO-only; enable it
everywhere with `selfReview: true` / `--review` when you want the extra check.
