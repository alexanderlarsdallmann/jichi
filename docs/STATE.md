# Where your state lives

Everything jichi keeps, where it keeps it, whether it matters if you lose it, and
how to get rid of it. One page, because the answer was previously spread across
five and the file holding a **secret** was on none of them.

> **The one-line version.** Your config is `~/.jichi`; your API key should be in
> `~/.jichi.env`; everything jichi *generates* lives under `~/.jichi.d/`; anything
> project-specific lives in that project's `.jichi/`. Only the first two are
> irreplaceable.

## The four places

| Path | What it is | Yours or jichi's? |
| --- | --- | --- |
| `~/.jichi` | The JSON config: models, mode, permissions, everything. **You write this** (or `jichi setup` writes it for you). | yours |
| `~/.jichi.env` | Shell-format environment file, **mode 0600**, holding `JICHI_API_KEY` and friends — written by `jichi setup` when you let it store the key. This is the file with the secret in it. Note that jichi does **not** read it by itself: your shell (or the start script `setup` generates) sources it, so the key arrives as an environment variable. | yours |
| `~/.jichi.d/` | Everything jichi generates — fifteen subtrees and one file, listed below. | jichi's |
| `<project>/.jichi/` | Project-scoped assets: `memory.md`, `skills/`, `commands/`, `agents/`, `glossary.md`. Plain files; you or the agent author them. Also the learner's record — `progress.jsonl` (graded attempts) and `hints.jsonl` (rungs pulled), deliberately two files so a hint can never be read as an attempt. | shared |

Plus one more you will meet in any project you configure per-directory:

| Path | What it is |
| --- | --- |
| `<project>/local/config.json` | A project-local config that **wins over** `~/.jichi` for scalars, and **unions** with it for lists. The precedence is spelled out in [CONFIG_TUTORIAL.md](CONFIG_TUTORIAL.md) §0 — read it once, because "first match wins" (what two older pages said) is not what happens. |

If `HOME` is unset, all of the above resolve under `/tmp` instead — which is a
different machine's worth of state, and why `doctor` says so rather than guessing.

## Everything under `~/.jichi.d/`

Fifteen subtrees, plus one file. The column that matters is the last one.

| Subtree | Holds | If you delete it |
| --- | --- | --- |
| `sessions/` | Saved conversations: history, mode, workspace, and the task list (`todowrite`, M606) | **you lose your conversations** |
| `runs/` | The run journal, one JSONL per autonomous run — the record a supervisor reads | you lose the audit trail of past runs |
| `audit/` | `privileged.jsonl` — every `sudo`-shaped attempt, allowed or refused | you lose the security record |
| `telemetry/` | Per-turn cost and tool-use logs that `jichi telemetry` and `learn analyze` read — one appended file per workspace, `metrics` tier on by default since M599 (numbers and names, never content) | you lose the measurements, and the learning loop's evidence |
| `checkpoints/` | The shadow git repo behind snapshots and `/undo` (your own `.git` is untouched) | rebuilt; pending undos are gone |
| `index/` | Semantic search index: `manifest.json` + `vectors.f32`, one dir per workspace (trim with `prune`, M612; the current workspace's is protected) | rebuilt on demand (costs embedding calls) |
| `worktrees/` | Sandbox worktrees (parallel pool; `attempt`/`improve`/workflow) | rebuilt; stale `att-`/`imp-`/`wf-` trees trimmed by `prune` (M616) |
| `leases/` | One JSON per workspace — the lock that stops two runs sharing a tree | rebuilt; delete only when no run is live |
| `control/` | Per-run unix-domain socket, mode 0600, for the mid-run control channel | rebuilt per run |
| `daemon/` | The daemon's socket and state | rebuilt |
| `improve/` | The self-improvement band's drafts | you lose unapplied drafts |
| `dreams/` | `jichi dream` output (deduped against the last; trim with `prune`, M611) | you lose those notes |
| `voice/` | Synthesised speech clips | rebuilt |
| `docs/` | The fetched-docs cache behind `search_docs` | re-fetched |
| `tool/` | Spill files for over-cap tool output (the M339 claim tickets) | in-flight receipts become unredeemable |
| `calibration.json` | The measured token-per-character ratio for **this** gateway | re-measured |

**The irreplaceable set is small:** `~/.jichi`, `~/.jichi.env`, `~/.jichi.d/sessions`,
and — if you care about the record — `runs/`, `audit/` and `telemetry/`. Everything
else is a cache or a rebuildable index. That is a deliberate property, not luck:
jichi is designed so that deleting `~/.jichi.d` costs you time, never work.

## Permissions, and one platform where they do not hold

Files holding secrets are created 0600 and directories 0700 — and you should know
that **jichi currently trusts that this worked.** `chmod` is called and its result
is not read back, which is fine on every filesystem that implements POSIX modes
and wrong on at least one that does not: on **Windows/MSYS2 with the default
`noacl` mount**, `chmod` reports success and changes nothing, leaving the key file,
the daemon socket and the audit log world-readable while jichi reports no problem
at all. One `/etc/fstab` line with `acl` fixes all four. A network mount or an
exotic container filesystem can fail the same silent way.

Reading the mode back after setting it is an open item — cheap to do, with a real
design question attached (is a failed privacy guarantee a `doctor` warning or a
hard failure?) — recorded in [DEFERRED.md](DEFERRED.md) under "platforms never
compiled". Until it lands, on an unusual filesystem, check for yourself:

```sh
ls -l ~/.jichi.env ~/.jichi
```

The measurement behind all of this, and what still fails after the `acl` fix, is
in [PLATFORMS.md](PLATFORMS.md).

## Reclaiming space

```sh
jichi ls --all
jichi checkpoints gc
jichi prune --keep 20 --older-than 30d --dry-run
```

`ls --all` lists saved sessions across every project (plain `ls` shows this one).
`checkpoints gc` **reports** the shadow store and what is orphaned in it.
`prune` deletes old sessions — both criteria must agree, and `--dry-run` shows
you the list first. Deleting `index/` by hand is always safe. [SNAPSHOTS.md](SNAPSHOTS.md)
and [OBSERVABILITY.md](OBSERVABILITY.md) cover the retention policies.

## The prerequisite nobody wrote down: `make install` does not ship the docs

This is the gap that strands self-learners, so it gets its own section.

`sudo make install` installs exactly this:

| Installed | Where |
| --- | --- |
| `jichi`, `jichi-convert`, `jichi-nano` | `$PREFIX/bin` |
| `jichi.1` | `$PREFIX/share/man/man1` |
| bash + zsh completions | the completion dirs |
| `jichi.el`, `jichi.vim` | the editor dirs |

It does **not** install `docs/`, `tests/`, `docs/assignments/`, `docs/reading/`, or
`examples/`. Nothing is broken by that — the binary is self-contained — but it has
one consequence that matters:

> **Every tutorial's reading track, every graded assignment, and every
> `examples/` scaffold needs the source checkout.** If you installed jichi from a
> package and want to *learn from* it, keep the repository too:
>
> ```sh
> cd ~/src/jichi        # wherever you cloned it
> jichi assignments
> ```
>
> Run learner commands from inside the checkout. `jichi` itself works from
> anywhere; the *course* lives in the tree.

The scaffold packs (`jichi init <pack>`) are the exception: they are compiled into
the binary, so `init` works without the checkout. The twelve domain benches under
`examples/` are deliberately copy-to-use rather than compiled in — they would have
cost about 20% of the binary — so those, too, need the tree.

## See also

- [INSTALL.md](INSTALL.md) — installing, and the system requirements
- [CONFIG_TUTORIAL.md](CONFIG_TUTORIAL.md) — writing the config this page locates
- [MIGRATION.md](MIGRATION.md) — moving state from older layouts
- [TOOL_DECISIONS.md](TOOL_DECISIONS.md) — what the agent may do to any of it
