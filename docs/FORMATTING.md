# Formatting: `format_file` and `formatCommand` (M263)

jichi's `format_file` tool has **two backends**:

1. a **language server** — `textDocument/formatting`, used whenever a configured
   `lspServers` entry formats the file (see [`LSP.md`](LSP.md));
2. a **`formatCommand`** — a shell command that rewrites the file in place, used
   when no language server formats it.

The second exists because plenty of languages have no LSP formatter. Emacs Lisp
has no standalone server at all (the editor *is* Emacs); Racket, R, shell and
others format through their own tools. Before this, `format_file` was registered
only when `lspServers` existed, so those projects had no formatting path.

## Configuring it

```jsonc
{
  "formatCommand": "clang-format -i"
}
```

The file path is supplied by jichi. Two forms:

| Command contains | jichi runs |
|---|---|
| no `{}` | the command with the path appended: `clang-format -i 'src/a.c'` |
| `{}` | every `{}` replaced by the quoted path: `prettier --write {} --loglevel warn` |

Use `{}` when the path is not a trailing argument — followed by other flags,
or mid-command. Put `{}` **outside any quotes** (see the rule below).

### The quoting rule (read this if your command has quotes)

**`{}` is replaced by the path *already single-quoted*, so it must sit OUTSIDE
your own quotes.** jichi substitutes `'src/a.R'` (a complete shell word), which
is a single argument wherever it lands in unquoted command text:

```jsonc
"formatCommand": "prettier --write {} --loglevel warn"
```

**Do not put `{}` inside quotes you opened.** This used to be taught as correct
and it is not: the substituted path's own opening quote closes yours, and the
path's contents fall into the shell unquoted — a filename with a space splits
into two arguments, and a crafted name runs commands. jichi now **refuses** a
`formatCommand` whose `{}` is inside a single- or double-quoted span (M847, the
scaffolding review's S1), with an error naming the fix:

```jsonc
"formatCommand": "Rscript -e 'styler::style_file(\"{}\")'"   // ✗ refused: {} inside quotes
```

When the formatter takes the path *inside* an expression, do not use `{}` at
all: append it as a trailing argument and have the tool read it. The R and
Emacs Lisp packs do this (below). Both the safe and the refused shapes are
pinned in `tests/test_fmtcmd.c`.

## Which backend runs

| Situation | Backend |
|---|---|
| a language server formats this file | **LSP** (richer: it knows the language) |
| `lspServers` is set but no server matches, or the server returns nothing usable | `formatCommand`, if set |
| no `lspServers` at all | `formatCommand`, if set |
| neither | an error naming both options |

`format_file` is **advertised to the model when either backend is available** —
previously it appeared only with `lspServers`.

## Safety

The path comes from the model, and a shell command is a string the shell parses,
so the path is **always single-quoted**, with embedded single quotes escaped as
`'\''`, and `{}` is accepted only outside your own quotes (M847). A file
literally named `x; id > pwned; echo .txt` is formatted, not executed —
asserted by `tests/smoke/format_command.sh`, which also checks that a filename
with a space is formatted as one argument and that a `{}`-inside-quotes
`formatCommand` is refused rather than run with a path broken out of its quotes.

Beyond that, `format_file` is unchanged: **mutating**, so it is permission-gated
like any write, and the path goes through the workspace fence before anything
runs.

Two behaviours worth knowing:

- The external formatter **writes the file itself**, so unlike the LSP backend it
  does not pass through `jc_app_write_file`. Under an ACP editor delegate that
  means the change lands on disk rather than in the editor's buffer; the editor
  will show it as an external modification.
- After it runs, jichi refreshes its read-before-edit record for that file: what
  it last read is no longer what is on disk.

Failures are returned as tool errors carrying the command's own output and exit
code, so the model can read `clang-format`'s complaint rather than guessing.

## Which formatter for your language

The language scaffold packs ship a working `formatCommand` in their
`config.example.json` — `jichi init cpp`, `init clojure`, `init r`, and so on:

| Pack | `formatCommand` |
|---|---|
| cpp | `clang-format -i` |
| perl | `perltidy -b` |
| racket | `raco fmt -i` |
| clojure | `cljfmt fix` |
| haskell | `fourmolu -i` |
| elixir | `mix format` |
| erlang | `erlfmt -w` |
| r | `Rscript -e 'styler::style_file(commandArgs(trailingOnly=TRUE))'` |
| elisp | `emacs -Q -batch --eval '(progn (find-file (car (last command-line-args-left))) (indent-region (point-min) (point-max)) (save-buffer))'` |

`jichi doctor` reports whether the configured formatter is on your PATH, and
warns (rather than fails) when it is not — the tool is on-demand.

> **History.** These nine packs shipped `formatCommand` for months **before
> jichi parsed it** — the key did nothing, and the M262 audit found it. The
> honest choice at that moment was to delete a promise the binary did not keep;
> implementing it, and putting the key back truthfully, was the better one. Two
> of the restored values were also wrong *as documentation*: the R entry could
> never have worked without `{}`, and the elisp entry hardcoded `my-pkg.el`.
