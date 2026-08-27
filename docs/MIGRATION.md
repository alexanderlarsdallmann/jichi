# Migrating from `jlu_continue` to `jichi`

> **Who this is for, decided at M487: almost nobody, and it is kept anyway.**
> The rename happened before jichi was ever published, so **no reader of the public
> repository has ever had `jlu_continue` installed** and none of the `mv` steps below
> applies to them. Two reasons it still ships rather than being deleted: the runtime
> state migration it documents is **live code** (`src/main.c` detects a pre-rename
> `~/.jlu_continue.d` and says so, guarded by `tests/smoke/migration_paths.sh`), so a
> page describing it should exist; and the workspace-keyed cache problem in the next
> section — that `mv a b` nests `a` *inside* `b` when `b` already exists, silently — is
> a hazard worth reading once whether or not you ever owned the old name.
>
> If you arrived here from a search engine expecting to migrate an install: you do not
> have one. Start at [`../README.md`](../README.md).

The project was renamed for its first public release. The working name
`jlu_continue` carried two things it should not: `continue`, which implied a fork
of Continue when this is a from-scratch reimplementation, and `jlu`, which tied a
general-purpose tool to one university's initials.

Everything about the code is unchanged. The internal `jc_` / `JC_` symbol prefix
is **untouched** — it now reads as an abbreviation of *jichi* as well as of *just
code*. If you only build and run from a checkout, the two `mv` commands below are
the whole migration.

## What to run

`jichi` tells you this itself, with your real paths, the first time it starts and
finds pre-rename state. Since M326v it also speaks when **both** locations exist —
the case that used to be silent, and the one that loses data.

> ### ⚠️ Check the destination does not exist first
>
> Every command below is `mv OLD NEW`, and `mv` does **two different things**
> depending on `NEW`:
>
> - `NEW` does not exist → renames `OLD` to `NEW`. **What you want.**
> - `NEW` exists as a directory → moves `OLD` *inside* it, giving
>   `NEW/OLD`. **Silently, with exit status 0.**
>
> The second is easy to hit, because **running `jichi` even once creates the new
> directory**. Then you notice your sessions or checkpoints are missing, reach for
> the `mv` below, and bury the old tree one level deeper. Seen in the field as a
> `~/.jichi.d/.jichi.d/` holding 5.5 GB that nothing reads.
>
> So check first, and merge rather than move when both exist:
>
> ```sh
> # safe: refuses instead of nesting
> [ -e ~/.jichi.d ] && echo "exists -- merge by hand, do NOT mv" || mv ~/.jlu_continue.d ~/.jichi.d
>
> # merging: copy the contents up, then remove the old tree
> cp -an ~/.jlu_continue.d/. ~/.jichi.d/    &&  rm -rf ~/.jlu_continue.d
> ```
>
> (`cp -an` = archive, never overwrite — the new location's files win, which is
> right: they are the ones jichi has been writing.) On GNU coreutils `mv -T OLD
> NEW` also refuses to nest, but `-T` is not POSIX and is absent on the BSDs.
>
> **Expect a warning from recent GNU coreutils** (measured 2026-08-20 while
> following this page on a real migration): *"cp: warning: behavior of -n is
> non-portable and may change in future; use ...=none instead"*. The copy is still
> correct — `-n` does not overwrite today — and the spelling coreutils suggests is
> **not** the fix to reach for here: it is GNU-only, and this page is read on the
> BSD rows too. (Its literal name is left out on purpose — `docs_flags` reads any
> `--token` in `docs/` as a claim about a *jichi* flag and correctly failed the
> build when this paragraph first quoted it, which is the M295 rule: reword the
> prose collision rather than keep an exception list.) If the warning bothers you, or if a future
> coreutils changes the behaviour rather than only warning about it, the portable
> form is an explicit per-file loop:
>
> ```sh
> (cd ~/.jlu_continue.d && find . -type f -print) | while read -r f; do
>     [ -e "$HOME/.jichi.d/$f" ] || { mkdir -p "$HOME/.jichi.d/$(dirname "$f")"; \
>         cp -p "$HOME/.jlu_continue.d/$f" "$HOME/.jichi.d/$f"; }
> done
> ```
>
> Same rule either way: **the new location wins, and nothing is overwritten.**

The commands, assuming you have checked:

```sh
# the session store, which the rename missed (M204 -- see below)
mv ~/.continue/sessions  ~/.jichi.d/sessions

# per-user state: telemetry, run journals, checkpoints, calibration,
# the search index, the docs cache, audit logs
mv ~/.jlu_continue.d  ~/.jichi.d

# the global config file
mv ~/.jlu_continue  ~/.jichi

# the global assets directory, if you have one
mv ~/.config/jlu_continue  ~/.config/jichi

# per project, in each workspace: agents, skills, commands, memory, glossary,
# output styles, constraints, the board
mv .jlu  .jichi
```

**Design decision — detect and instruct, do not migrate.** `jichi` will not move
these for you. Two alternatives were rejected: reading both the old and new
locations everywhere would have meant a fallback at roughly 650 call sites and a
permanently doubled surface for path bugs; and moving the directories
automatically would silently relocate tens of megabytes of your telemetry,
sessions and checkpoints as a side effect of an unrelated command. Ten seconds of
copy-paste is the better trade, and it leaves exactly one path scheme in the tree.

## The full mapping

| Was | Is now |
|---|---|
| `jlu_continue` (binary) | `jichi` |
| `jlu-convert` | `jichi-convert` |
| `~/.jlu_continue` | `~/.jichi` |
| `~/.jlu_continue.d/` | `~/.jichi.d/` |
| `~/.continue/sessions/` (M204) | `~/.jichi.d/sessions/` |
| `~/.config/jlu_continue/` | `~/.config/jichi/` |
| `.jlu/` (per project) | `.jichi/` |
| `JLU_*` env vars | `JICHI_*` (e.g. `JLU_BIN` → `JICHI_BIN`) |
| `man jlu_continue` | `man jichi` |
| `jlu.el`, `jlu.vim`, `jlu-nano` | `jichi.el`, `jichi.vim`, `jichi-nano` |
| Vim `:JluAsk`, `:JluTask`, … | `:JichiAsk`, `:JichiTask`, … |
| Emacs `jlu-` / `jlu--` prefix | `jichi-` / `jichi--` |
| `jc_` / `JC_` symbols | **unchanged** |
| `JC_CONFIG`, `JC_PROVIDER` | **unchanged** |

## What was deliberately *not* renamed

These contain "JLU" or "jlu" and mean something other than this project. Renaming
any of them would break a working setup:

| Kept | Why |
|---|---|
| `JLU_API_KEY` | the Justus-Liebig-Universität HRZ gateway key, not ours |
| `jlu/qwen3-coder-next`, `jlu/gemma-4-31b-it`, … | **HRZ model ids** — wire values sent to the proxy. Renaming one silently breaks every model lookup in a config. |
| `api.hrz.uni-giessen.de`, `gitlab.hrz.uni-giessen.de` | university hosts |
| "the JLU HRZ local models", "Justus-Liebig-Universität" | prose about where this was built |

## Back-compat aliases

`make` and `make install` also create `jlu_continue` and `jlu-convert` symlinks to
the new binaries, so a wrapper script that resolves the old path by name keeps
working — including the `./jlu`-style wrappers in sibling projects. They are
**deprecated** and slated for removal one release after the rename; move your
wrappers to `jichi` when convenient.

Note the aliases only cover the *executable*. A wrapper that also relies on
`.jlu/` assets or `~/.jlu_continue.d/` state still needs the `mv` commands above.

## Moving the checkout directory (optional)

Renaming the *repository directory* is separate from the rename and not required —
both shipped wrappers try `../jichi/jichi` and `../jlu_continue/jichi`. If you do
move it, two per-user caches are keyed by a `djb2` hash of the **workspace path**
and must come along, or you silently lose this repo's undo history:

```sh
BASE=~/development/miscellaneous            # adjust
mv $BASE/jlu_continue $BASE/jichi
ln -s jichi $BASE/jlu_continue              # optional: keep stale absolute paths working

# carry the caches (compute the keys with the djb2 of each absolute path).
# The same nesting trap as above, and MORE likely here: <newkey> already exists
# the moment you run jichi once from the new path -- which is exactly when you
# notice the undo history is gone and come looking for this section.
[ -e ~/.jichi.d/checkpoints/<newkey> ] && echo "newkey exists -- see the warning above"
mv ~/.jichi.d/checkpoints/<oldkey> ~/.jichi.d/checkpoints/<newkey>
git -C ~/.jichi.d/checkpoints/<newkey> config core.worktree $BASE/jichi
mv ~/.jichi.d/index/<oldkey> ~/.jichi.d/index/<newkey>
```

If `<newkey>` already exists it is a *fresh, empty* cache jichi just made — the
history you want is in `<oldkey>`. Delete the new one and then rename, rather than
`mv`-ing one into the other.

The **checkpoint** repo moves cleanly once `core.worktree` is corrected — the
commits, and therefore `undo` / `rewind`, survive. The **index** additionally
stores per-file absolute paths, so the first run after a move re-embeds the whole
workspace once and reuses normally thereafter; there is no way around that short of
rewriting the manifest's file list, and one re-embed is cheaper than the risk.

Two things are deliberately **not** rewritten: the workspace paths recorded in past
telemetry events (`"ws"`) and run journals. Those are history, and editing them
would falsify the record. The practical consequence is that
`telemetry --workspace <new path>` will not match events logged under the old one —
filter on the old path when reading old logs.

## If you drive `jichi` from another project

Check for these, which the binary symlink does not cover:

```sh
grep -rn 'jlu_continue\|\.jlu/\|JLU_BIN\|JLU_CONFIG\|JLU_LANG' . \
  --exclude-dir=.git | grep -v JLU_API_KEY
```

Typical hits: a `./jlu` wrapper's binary path, `local/config.json` paths, a
`logging.path` pointing into `~/.jlu_continue.d/telemetry/`, systemd units, and
cron entries.

See also: `README.md` ("The name"), `docs/ROADMAP.md` (the release checklist).

## M204 — the session store, which the rename missed

Sessions lived in **`~/.continue/sessions/`** until M204. That was deliberate once:
jichi began as a from-scratch reimplementation of Continue CLI, and putting the
per-session files beside Continue's meant the two tools could sit in one directory.

The M170 rename then moved *everything else* — binary, config, state directory,
assets directory, per-project directory, env vars, man page, editor plugins, Vim
commands, Emacs prefixes — and missed this one, because nothing breaks when it is
wrong. The mapping table above did not even list it.

The costs were real, and one of them is why this is now fixed:

- Cleaning up or uninstalling Continue **destroyed a jichi user's history**.
- The documentation had to carry a "do not delete `~/.continue`" warning, which is
  a sign that a path is in the wrong place rather than a thing worth documenting.
- It was the single piece of per-user state living in a foreign product's
  directory, while telemetry, journals, checkpoints, calibration, the index, the
  docs cache and the audit logs all sat in `~/.jichi.d/`.

`jichi` detects the old store and prints the one `mv` above, following the same
**detect and instruct, do not migrate** rule as the rest of this document: it will
not silently relocate megabytes of your conversation history as a side effect of an
unrelated command. The notice stops on its own once the new path exists.

The one argument for the old location — sharing a session store with a
still-installed Continue CLI — is what the change gives up. If you rely on that,
symlink it: `ln -s ~/.continue/sessions ~/.jichi.d/sessions`.
