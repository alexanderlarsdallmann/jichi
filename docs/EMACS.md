# Emacs integration (`jichi.el`)

> One of jichi's editor integrations — see [EDITORS.md](EDITORS.md) for the full
> set (Vim/Neovim, nano, ACP editors) and how they all share the headless
> contract. If you also keep your project's plans and decisions in Emacs,
> [ORG_MODE.md](ORG_MODE.md) is the org-mode tutorial that goes with this
> package (the practice itself is editor-free:
> [PROJECT_RECORDS.md](PROJECT_RECORDS.md)).

`editors/emacs/jichi.el` lets you send a marked region — or the whole buffer — to
the `jichi` agent and get the result back into Emacs: in a side buffer,
at point, appended, or replacing the region. It complements running jichi in a
shell buffer; here the *text in your buffer* is the input.

It drives jichi's **headless** mode (the same contract as `docs/SCRIPTING.md`): it
composes a prompt, pipes it to `jichi -p -` on stdin, streams the answer
back from stdout, and keeps diagnostics (stderr) separate. There is no network
or protocol code in the package — jichi does the talking. (For a richer,
session-based editor client, jichi also speaks ACP — see `docs/ACP.md` — but this
package intentionally takes the simple headless path.)

## Install

The file is plain Emacs Lisp (Emacs 25.1+; no mandatory dependencies —
`project.el` and `markdown-mode` are used when present).

- **From the repo, manually:**

  ```elisp
  (add-to-list 'load-path "/path/to/jichi/editors/emacs")
  (require 'jichi)
  (global-jichi-mode 1)        ; bind the commands under C-c j everywhere
  ```

- **Via `make install`:** the file is installed to
  `$(PREFIX)/share/emacs/site-lisp/jichi.el` (alongside the binary, manpage, and
  shell completions), which is on the default Emacs `load-path`. Then just
  `(require 'jichi)`.

Ensure `jichi` is on `PATH`, or set `M-x customize-variable
jichi-program` to an absolute path.

## Commands

Enable `jichi-mode` (or `global-jichi-mode`) for the `C-c j` keymap; every command
also works via `M-x`.

| Key | Command | What it does |
| --- | --- | --- |
| `C-c j j` | `jichi-dwim` | Ask about / transform the region (or whole buffer) → answer streams into `*jichi*`. |
| `C-c j a` | `jichi-ask-about-region` | Ask a question about the region → `*jichi*`. |
| `C-c j b` | `jichi-send-buffer` | Send the whole buffer; result goes to `jichi-default-disposition`. |
| `C-c j r` | `jichi-replace-region` | Transform the region and **replace it** with the answer. |
| `C-c j i` | `jichi-insert-at-point` | Generate text (region as context) and insert it at point. |
| `C-c j t` | `jichi-task` | **Agentic:** jichi may run tools and **edit files on disk** (asks first). |
| `C-c j C-g` | `jichi-cancel` | Interrupt the running request (SIGINT; jichi aborts cleanly). |

Each command prompts for an instruction in the minibuffer.

### Read-only by default

The five text commands (`jichi-dwim`, `-ask-about-region`, `-send-buffer`,
`-replace-region`, `-insert-at-point`) always pass `--readonly`: jichi can read
the project and answer, but **cannot change files on disk**. The result only
ever flows back through Emacs, so an edit is a normal, undoable buffer change.

`jichi-replace-region` pushes the old text to the kill ring and wraps the change
in one `atomic-change-group`, so a single `undo` (or `C-y`) restores it.

### The agentic command

`jichi-task` is the exception: it runs with `--auto`, so jichi uses its tools and
**edits files in the project on disk**. It:

1. asks `yes-or-no-p`, naming the workspace root, before doing anything;
2. streams the transcript into `*jichi*` and appends jichi's tool log at the end;
3. after a successful run, detects which open buffers' files it changed and
   **offers** to revert them (you can decline and review with your own tools).

Use it for "refactor this across the module", "add a test for this function",
etc. — work that should land on disk, not just produce text.

## How the prompt is built

The instruction you type is sent with the selected text fenced beneath it, e.g.:

```
make this concise

Context: file notes.md, major mode markdown-mode.

```markdown
<your selected text>
```
```

For `replace`/`insert` an extra line asks jichi to *output only* the resulting
text (no preamble, no code fence) so it drops cleanly into the buffer; the
package also de-fences a single surrounding ```` ``` ```` block defensively.
The `Context:` line is controlled by `jichi-include-file-context` (default on).

The subprocess runs with its working directory set to the buffer's **project
root** (via `project.el`, then VC, then the file's directory), so jichi's
repository map, `@`-references, and tools see the right workspace.

## Customization

`M-x customize-group RET jichi RET`, or set:

| Variable | Default | Meaning |
| --- | --- | --- |
| `jichi-program` | `"jichi"` | binary name or absolute path |
| `jichi-default-args` | `("-q")` | args always passed (quiet keeps stderr to errors) |
| `jichi-model` | `nil` | `--model` selector; `nil` uses jichi's config default |
| `jichi-no-session` | `t` | pass `--no-session` (don't persist editor one-shots) |
| `jichi-include-file-context` | `t` | tell jichi the file name + major mode |
| `jichi-output-buffer-name` | `"*jichi*"` | the display buffer |
| `jichi-default-disposition` | `display` | where `jichi-send-buffer` puts the answer |
| `jichi-keymap-prefix` | `"C-c j"` | prefix for `jichi-mode` (reload to change) |

A per-call model/mode override is just `jichi-program` + flags; for finer control
run jichi in a shell or via ACP.

## Notes & limits

- **Async, never blocking.** Requests run via `make-process`; Emacs stays
  responsive while jichi works. The answer streams live for the display / insert /
  append dispositions; `replace` waits for the whole answer (it has to, to
  de-fence) and then replaces in one undo step.
- **One workspace per call.** Each command is a fresh headless run (stateless).
  There's no shared conversation between commands — that's what the TUI and ACP
  are for.
- **Large buffers are fine.** The prompt is piped on stdin, never passed as a
  command-line argument, so there's no `ARG_MAX` limit on buffer size.

## Internals

- Package: `editors/emacs/jichi.el`. Core runner `jichi--run` (a `make-process` with
  a `:filter` for streaming, a separate `:stderr` buffer, and a `:sentinel` that
  reports the exit code); `jichi--build-args` (always ends in `-p -`);
  `jichi--compose` (prompt template); `jichi--project-root`; `jichi--defence`; the
  `jichi--start` dispatcher; the commands and `jichi-mode`.
- Tests: `tests/elisp/jichi-tests.el` (ERT) drive `tests/elisp/jichi-stub.sh` (a
  stand-in binary) — fully offline. `make elisp-compile` byte-compiles with
  warnings-as-errors; `make elisp-test` runs the suite. Both are a no-op when
  Emacs isn't installed, so `make ci` is unaffected on Emacs-less hosts.
