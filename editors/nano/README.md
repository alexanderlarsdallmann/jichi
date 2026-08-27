# nano + jichi

nano has no plugin/scripting API, so integration is a **shell wrapper** you call
alongside nano rather than from inside it. `editors/nano/jichi-nano` bridges a file
and the agent over the headless contract (`jichi -p -`).

## Install

```sh
cp editors/nano/jichi-nano ~/.local/bin/    # somewhere on PATH
chmod +x ~/.local/bin/jichi-nano
```

Needs `jichi` on `PATH` (override with `JICHI=/path/to/jichi`).

## Use

```sh
jichi-nano ask     notes.md  "summarize the open questions"   # prints an answer
jichi-nano explain parser.c                                   # prints an explanation
jichi-nano edit    parser.c  "add a doc comment to each function"  # rewrites in place
```

- `ask` / `explain` run **read-only** and print to stdout — pipe to a pager or
  `nano -v` (view mode) if you like: `jichi-nano explain parser.c | nano -v -`.
- `edit` runs `--auto` and **rewrites the file in place**, keeping a `<file>.bak`
  backup. Review with `diff file.bak file` before discarding the backup.

## Recipes

**Two-pane workflow (recommended):** run nano and jichi-nano in adjacent terminals
(or [tmux](../../docs/TMUX.md) panes) — edit in one, ask/transform in the other,
reload in nano with the usual read-file command.

**From nano's "execute command" prompt** (modern nano, `^T` then the command, or
`M-B` to run a command): nano feeds the buffer to the command's stdin and reads
stdout back, so you can pipe the buffer through jichi:

```
jichi -q --readonly -p 'Fix grammar; return only the corrected text.'
```

Add a binding to `~/.nanorc` so it's one keystroke (nano ≥ 5):

```nanorc
# ^J: pipe the buffer through jichi for a grammar/style pass
bind ^J "{execute}jichi -q --readonly -p 'Proofread; return only the corrected text.'{enter}" main
```

**Shell alias** for a quick question about the file you're editing:

```sh
alias jask='jichi-nano ask'
jask README.md "is the install section accurate?"
```

## Notes

- Run jichi-nano from the **project root** so the path fence and repo map are
  project-scoped (the `edit` mode writes only within the cwd).
- For a richer, streaming, in-editor experience use Emacs (`jichi.el`), Vim
  (`jichi.vim`), or an ACP editor — see [EDITORS.md](../../docs/EDITORS.md).
