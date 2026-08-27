# Vim / Neovim integration

`editors/vim/jichi.vim` drives the jichi agent from Vim 8+ and Neovim over
the headless contract ([SCRIPTING.md](SCRIPTING.md)): it pipes buffer/region text
plus an instruction to `jichi -p -` on stdin, reads the answer from stdout,
and keeps stderr separate. No jichi C code; the same binary the CLI uses.

## Install

Copy the plugin where your editor loads plugins:

```sh
# Vim
mkdir -p ~/.vim/plugin && cp editors/vim/jichi.vim ~/.vim/plugin/
# Neovim
mkdir -p ~/.config/nvim/plugin && cp editors/vim/jichi.vim ~/.config/nvim/plugin/
```

`make install` also copies it to the user plugin dir. The `jichi` binary
must be on `PATH` (or set `let g:jichi_program = '/path/to/jichi'`).

## Commands

| Command | Mode | Effect |
|---|---|---|
| `:JichiAsk {question}` | normal | Ask about the current buffer; the answer opens in a `__jlu__` scratch split. Read-only. |
| `:{range}JichiRegion {instr}` | visual/range | Transform the selected lines per `{instr}`; the answer **replaces** the range. Read-only (jichi computes the new text; the editor writes it). |
| `:{range}JichiExplain` | visual/range | Explain the selection in the scratch split. |
| `:JichiExplainBuffer` | normal | Explain the whole file. |
| `:JichiTask {task}` | normal | **Agentic** `--auto` run in the project (confirms first, may edit files, reloads changed buffers). |

### Default mappings

Disable with `let g:jichi_no_mappings = 1`. `<Leader>` is `\` unless you remap it.

| Mapping | Mode | Command |
|---|---|---|
| `<Leader>ja` | normal | `:JichiAsk ` (type your question) |
| `<Leader>jr` | visual | `:JichiRegion ` (type the instruction) |
| `<Leader>je` | visual | `:JichiExplain` |
| `<Leader>jt` | normal | `:JichiTask ` (type the task) |

## How it works

```mermaid
flowchart LR
    B["buffer / visual range + instruction"] -->|system(), stdin| J["jichi -q --readonly -p -"]
    J -->|stdout| R["answer"]
    R --> S["scratch split (JichiAsk)"]
    R --> P["replace the range (JichiRegion)"]
    T[":JichiTask (confirm)"] -->|--auto| J2["jichi --auto"]
    J2 --> C["checktime -> reload changed buffers"]
```

- The plugin `:lcd`s into the buffer's **project root** (git root, else the
  file's dir) for the call, so the path fence and repo map are project-scoped,
  then restores your working directory.
- Calls are **synchronous** (`system()`) for portability across Vim and Neovim —
  a whole-buffer transform blocks until jichi answers. For streaming, token-level
  output, use the Emacs client or an ACP editor (see [EDITORS.md](EDITORS.md)).
- `:JichiRegion` strips code fences/commentary by instructing jichi to return only
  the transformed text; review the diff (`u` undoes the replace) before saving.

## Configuration

```vim
let g:jichi_program = 'jichi'      " binary name or absolute path
let g:jichi_default_args = ['-q']         " always-passed args (-q silences stderr)
let g:jichi_no_mappings = 0               " 1 to skip the default <Leader> maps
```

Point jichi at a project/global config the usual way (its own precedence:
`--config` / `$JC_CONFIG` / `./local/config.json` / `~/.jichi`); add it to
`g:jichi_default_args` if you want a specific one, e.g.
`let g:jichi_default_args = ['-q', '--config', 'local/config.json']`.

## Troubleshooting

- **"jichi: exited N"** — run `:!jichi doctor` in the project to check the
  config, key, and model reachability.
- **Empty answer** — the model returned nothing (often a missing/again-unset API
  key, or an unreachable local server). `doctor` reports both.
- **`:JichiTask` changed files but the buffer is stale** — the plugin runs
  `checktime`; if `autoread` is off, run `:checktime` or `:edit`.

See also [EDITORS.md](EDITORS.md), [EMACS.md](EMACS.md), and — for driving jichi on
a remote box — [REMOTE_SSH.md](REMOTE_SSH.md) and [TMUX.md](TMUX.md).
