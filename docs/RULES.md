# Project rules (AGENTS.md)

jichi automatically loads instruction files and prepends them to the
system prompt, so the agent picks up your project conventions without being
told each session. The order is opencode's: `AGENTS.md` first, `CLAUDE.md` only as a fallback.
(Claude Code reads the other way round: its own `CLAUDE.md`, and `AGENTS.md` only in a project
with no `CLAUDE.md`.)

> This page is the **loading mechanics** (what is discovered, in what order).
> For **how to write an effective `AGENTS.md`** — what to include, what to leave
> out, and how it composes with memory/glossary/skills — see
> [`AGENTS_GUIDE.md`](AGENTS_GUIDE.md).

## What gets loaded, and in what order

On startup the agent concatenates (each prefixed with a `# Rules from <path>`
header), in this order:

1. **Global** — `~/.config/jichi/AGENTS.md`.
2. **Directory walk** — every `AGENTS.md` from the git root (the nearest
   ancestor containing `.git`) down to the working directory, root first. If a
   directory has no `AGENTS.md`, `CLAUDE.md` is used as a fallback for that
   directory. The `.git` may be a directory, as in a checkout, or a file, as in
   a linked worktree or a submodule: each is its own root, the directory
   `git rev-parse --show-toplevel` names (M884). A submodule that wants its
   superproject's rules lists them under `instructions`, whose paths are
   relative to the working directory; in a project's own `.jichi/config.json`
   that applies once `jichi trust` has been run there (M851).
3. **Config `instructions`** — explicit file paths from the config, in order.

Each file that is not a regular one (a FIFO, device or socket) is skipped with a
warning rather than opened: opening a named pipe would otherwise block start-up
(M1108, D-33). The same rule holds for a search: `search_code` whose `path` is a
present-but-unsearchable file (a FIFO, device or socket) is refused with a tool
error rather than opened, since `grep` would otherwise block in `open()` and
hang the whole call (M1110, D-36).

Duplicates (by path) are skipped, and the combined block is capped at 32 KB
(with a `... [rules truncated]` marker) so it can't blow up the context.

A prompt-asset file (the rules above, the project/global glossary, and the body
of an `@`-referenced command) is read **at most to its cap**, not whole: a
planted 60 MB file no longer sits resident on the session arena for the process
lifetime, only the head (or the combined tail, for the glossary) that the cap
keeps (M1109, D-35).

**A `CLAUDE.md` beside an `AGENTS.md` is not read, and nothing says so.** `jichi init`
writes an `AGENTS.md`, so in a project whose rules live in `CLAUDE.md` they stop applying
after `init` (measured; [DEFERRED def-288](DEFERRED.md#def-288)). Until that is fixed, keep
them active with `"instructions": ["CLAUDE.md"]` in `.jichi/config.json` and then
`jichi trust`, which was measured to bring them back. The trust step is needed since M851
(2026-10-01): an untrusted project config's `instructions` are skipped, and jichi says so
(measured 2026-10-03). jichi's own rules file, `JICHI.md`, read in addition to the
shared one, is decided and not built yet ([DEFERRED def-289](DEFERRED.md#def-289)).

Subagents do not receive the rules block (they get a focused task prompt).

## Config

```json
{
  "instructions": ["docs/conventions.md", "/etc/team/standards.md"]
}
```

`instructions` entries are file paths (relative to the working directory unless
absolute). Globs are not expanded in this version.

**This is the per-task lever, and it is how this repository uses it.** The
directory walk gives you rules by *place*; `instructions` gives you rules by
*job*. jichi's own `docs/rules/` holds small task files — e.g.
`docs/rules/reviewing.md`, loaded by the self-hosting review configs — on top of
the root `CLAUDE.md`, which carries only what every session must not violate.

Keep both halves small, and know why: **the assembled block is capped** (below),
truncation is **silent**, and it takes the *tail* — so an oversized rules file
does not fail, it just stops applying from the bottom up, and which rules survive
becomes a function of their line number. Measured on this repository before the
split: a 139 KB rules file delivered 14.1% of itself to a 16k-context model and
23.5% to a 196,608-token one, because the cap is absolute
(`docs/analysis/2026-08-21-self-hosting-first-review.md` §5).
`tests/smoke/rules_budget_lint.sh` now fails the build if the assembled rules
block is truncated.

## Example

`AGENTS.md` at your repo root:

```markdown
# Project conventions
- This is a C89 codebase; declarations go at the top of a block.
- Run `make test` before claiming a change works.
- Prefer the existing jc_* utilities over new helpers.
```

Every turn now starts with those rules in the system prompt.
