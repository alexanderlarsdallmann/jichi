# Security policy

## Reporting a vulnerability

**Do not open a public issue.** Report it privately to the maintainer, whose
address is the one on the commits in this repository, with `jichi security` in
the subject line.

Please include what you ran, on what platform, and what you observed. A proof of
concept is welcome and not required — a clear description of the mechanism is
usually enough, and this project would rather hear a suspicion than not hear it.

There is one maintainer and no service-level agreement to offer. What you will get
is an acknowledgement, an honest assessment of whether it reproduces, and — if it
does — a fix and a public write-up naming the finding and crediting you unless you
ask otherwise. Findings are recorded, including the ones that turn out not to be
defects, because the reasoning is worth as much as the patch.

## What jichi is, from a security point of view

This matters more than usual here, so it is stated plainly rather than assumed.

**jichi runs commands a language model chose.** That is the product, not a flaw in
it. The model reads your files, edits them, and executes shell commands — under
approval gates, but it is the mechanism. Everything below is about bounding that,
never about eliminating it.

The defences that **do not depend on the model's cooperation** are the real ones:

- **The path fence** — file tools resolve through one chokepoint and refuse paths
  outside the workspace; read-only `referenceRoots` widen reads without widening
  writes.
- **The network egress guard** — the one tool that fetches a URL at the model's
  choosing refuses private, loopback and link-local addresses both up front (the
  host literal) and at connect time (whatever the name resolves to, including a
  redirect hop), and it cannot be routed around by a proxy set in the environment:
  such a fetch goes direct, and a reused pooled connection is never trusted for it.
- **Redaction at the source** — a registered secret is scrubbed before it is
  written to an on-disk sink, before it is returned to the model as a tool or
  error result, and before a child agent's report is handed to the provider; and
  it is stripped from the environment of any program jichi starts on your behalf.
- **Approval gates** — per-tool, per-mode, composed with config allow/deny lists;
  `plan` mode is read-only. A shell command launched under `sudo`/`doas`/`pkexec`
  is detected and gated *below* the ordinary verdict, so a blanket "always" grant
  cannot satisfy it, and every attempt is written to an always-on audit log.
- **The autonomy envelope** — token, wall-clock and tool-call budgets, an
  edit-scope glob fence, a verification gate, and a JSONL journal of what happened.
- **The project-config trust step** — a repository's own `local/config.json` or
  `.jichi/config.json` comes with a clone, so until you trust it (`jichi trust`, or
  once in the TUI) it applies only in the safe direction: no new endpoint or
  credential, no program started, no pre-approval, no read outside the project.
  The trust is bound to the file's content (M851).
- **`setup` never applies an untrusted project config to a request** — the
  `setup --advisor` and `setup --onboard` paths load the defaulted project
  config through the same trust step: until it is trusted, its models are not
  applied, so a request carrying the user's own credential goes to the user's
  own endpoint, not to one the repository named. An explicit `--config` is a
  user decision and is used as-is. The drafts these paths write are
  link-refusing and no-overwrite, and are refused when the destination
  resolves outside the workspace. (M993.)
- **Project commands are trusted-project capabilities** — a command in the
  project's `.jichi/commands/` directory that runs a shell command (`!`cmd``) or
  reads a file outside the workspace (`@path`) is refused until the project is
  trusted, and a project command that shadows a user command of the same name
  prints a warning. The gate is keyed on the project's trust status, not a
  blanket ban on commands. A user's global `~/.config/jichi/commands` keeps
  its full capabilities even in an untrusted project (D-32).
- **Repository settings that point at jichi's own files** — a clone ships its own
  `.jichi/` tree, and a repository could place that tree (or one of the state files
  inside it) where it is read from and written to; jichi checks those paths before it
  opens them, so a repository cannot redirect jichi's own records outside the workspace.
  (M990; a follow-up closed the same class for the progress and board records, M997.)
- **The repository walkers refuse a link that leaves the workspace** — the codebase
  index, the repo map, and a recursive `list_files` all walk the workspace, and a
  repository can place a directory there as a symlink to somewhere outside it; the
  walkers check where a link resolves before they follow it, and a link that points
  outside is neither descended into nor shown, so outside paths and content do not
  reach the embeddings service, the system prompt, or a listing. A loop is visited
  once and bounded, so it terminates. (M991.)
- **Snapshots** — a shadow git repository whose work tree is your workspace, so
  `/undo` and rollback never touch your own `.git`.
- **Secret handling** — keys come from environment variables named by the config
  (or, for a model pointed at a vendor's own endpoint, that vendor's conventional
  variable — before 0.11.0 that variable was read for any host, CHANGELOG), are scrubbed from child environments, and are redacted from logs. jichi's own
  private state (sessions, telemetry, checkpoints) lives outside any workspace,
  and the state root that holds it is created owner-only (0700) -- and an
  existing permissive state root is tightened back by a jichi run and named by
  doctor -- so the checkpoints and worktrees it holds are unreachable on a
  traversable home.
  On Linux jichi also makes itself non-dumpable, so a command it runs cannot read
  the keys back out of jichi's own `/proc/<pid>/environ`: a same-user process
  could, until M770. What no part of jichi can do is hide a key from a command
  running as you somewhere else: in the file you keep it in, or in another of your
  processes. That boundary is another user or a container
  ([`docs/CONTAINER_TUTORIAL.md`](docs/CONTAINER_TUTORIAL.md)).
- **The workspace lease degrades to advisory where the filesystem's file locking
  is unavailable, and `--lease fail` then does not guarantee a single writer** —
  on a filesystem without lock support (some network mounts) a run proceeds with
  nothing to warn about at the default log level, and where the lock is held past
  its wait window the run proceeds with a warning naming the holder
  ([`docs/AUTONOMY.md`](docs/AUTONOMY.md) §3b). It is a convenience that catches
  two jichis on one tree, not a security boundary, and it does not serialise
  against another user (the per-`$HOME` limit AUTONOMY.md already states).

**Prompt injection is mitigated, not solved.** Content jichi fetches at the
*model's* choosing — a web page, an RSS feed, an MCP resource — is wrapped in an
explicit "data, not instructions" fence, and the convention is stated once in the
system prompt. That is a mitigation whose success depends on the model behaving,
which is exactly why it is listed *after* the defences that do not.
[`docs/HARDENING.md`](docs/HARDENING.md) §6b says so in the same words.

Known-and-accepted gaps are not hidden: [`docs/DEFERRED.md`](docs/DEFERRED.md)
records them with the reasoning, including a check-then-open window in the path
fence and the absence of OS-level sandboxing for `run_terminal_command`. If one of
those matters to your deployment, read
[`docs/HARDENING.md`](docs/HARDENING.md) and
[`docs/AUTONOMY.md`](docs/AUTONOMY.md) before running jichi unattended.

## Supported versions

jichi is distributed as **source only** and there is no back-porting: fixes land on
the current tree. See [`docs/PLATFORMS.md`](docs/PLATFORMS.md) for what has actually
been compiled and gate-run, and [`../CHANGELOG.md`](CHANGELOG.md) for what changed.
