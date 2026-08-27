# Project rules (AGENTS.md)

jichi automatically loads instruction files and prepends them to the
system prompt, so the agent picks up your project conventions without being
told each session. This mirrors opencode's / Claude Code's `AGENTS.md` rules.

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
   directory.
3. **Config `instructions`** — explicit file paths from the config, in order.

Duplicates (by path) are skipped, and the combined block is capped at 32 KB
(with a `... [rules truncated]` marker) so it can't blow up the context.

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
