# Session export (`export` / `/export`)

jichi persists every conversation as JSON under `~/.jichi.d/sessions/`. The
*export* feature (M34b) renders any of those sessions to a clean, human-readable
transcript — **Markdown** (the default) or a **self-contained HTML** document —
for PR write-ups, decision/handoff records, teaching artifacts, or just reading
a long run outside the terminal.

It is read-only and pure: a session's stored history is walked and formatted; no
provider, no network, no mutation.

## The `export` subcommand

```sh
jichi export                      # most recent session for this project
jichi export <id|prefix>          # a specific session
jichi export <id> --html          # HTML instead of Markdown
jichi export <id> -o out.md        # write to a file (else stdout)
jichi export --all                # consider sessions from any project
```

- With **no id**, the most recent session for the current project is exported
  (or any project's, with `--all`) — the same scoping `ls` and `--continue` use.
- An **id or unambiguous prefix** selects a specific session (the `ls` table
  shows ids); an ambiguous prefix is an error.
- **`--html`** emits a styled, self-contained HTML page (inline CSS, no external
  assets); the default is GitHub-flavored Markdown.
- **`-o <file>`** (alias `--out`) writes to a file; without it the transcript
  goes to stdout, so it pipes and redirects cleanly:

  ```sh
  jichi export abc123 > pr-notes.md
  ```

## The TUI `/export` command

Inside an interactive session:

```
/export                  # Markdown -> jichi-<id8>.md in the cwd
/export --html           # HTML     -> jichi-<id8>.html
/export notes.md         # a chosen filename
/export --html notes.html
```

`/export` always writes a file (the current session is auto-titled first); with
no filename it derives `jichi-<first 8 of the id>.{md,html}` in the working
directory.

## What the transcript contains

- A title (the session's title, falling back to "Session") and a metadata line:
  session id, workspace, mode, and message count.
- Each **user** and **assistant** turn, role-labeled, with its text.
- Each **tool call** the assistant made, as a labeled fenced block showing the
  tool name and its JSON arguments.
- Each **tool result**, fenced, flagged `(error)` when the tool reported one.

The **system prompt is omitted** — it is rebuilt each turn and isn't part of the
conversation you want to share. HTML output escapes `&`, `<`, and `>` so message
content can't break the document.

The JSON projection (`--output json`) additionally carries **`jichi`** — the build
that produced the export (M290). Additive to the M165 contract, so an existing
consumer is unaffected; a supervisor archiving transcripts gets provenance without
a second lookup. The session *file* records the build that last saved it, which is
not necessarily the one that exported it.

> Per-session **token/cost totals are not included**: the session store records
> the transcript, not usage counters (those are surfaced live in the TUI and in
> telemetry). Export is a faithful transcript, not a billing report.

## Internals

- **`jc_session_render(session, format, out)`** (`src/session/jc_session.c`) —
  the pure renderer; `format` is `JC_EXPORT_MD` or `JC_EXPORT_HTML`, output goes
  to a caller-owned `jc_sb`. A single history walk emits either format.
- **`run_export`** (`src/main.c`) — the subcommand shell: resolve the session
  (prefix/recent-scoped), render, write to the file or stdout.
- **TUI `/export`** (`src/tui/jc_tui.c`) — writes the live session.
- Tests: a render unit test (Markdown headings/tool blocks + HTML escaping) in
  `tests/test_session.c`; an offline round-trip e2e in `tests/e2e/export.py`.

## Not (yet) included

Sharing via a hosted **URL** needs a server and is out of scope for the
single-binary, dependency-light design — local export delivers the
communication value without the infrastructure. (For external integrations,
**MCP** is the extension point.)
