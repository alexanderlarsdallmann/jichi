<!-- canonical source -->
# Getting started with jichi

jichi is a command-line AI coding agent for Linux. This page takes you from
nothing to a working session. The deeper guides (linked below) are in English.

## 1. Install

You need **libcurl** and a C compiler. Build the two binaries:

```sh
make
```

This produces `jichi` (the agent) and `jichi-convert` (a config importer).
See [INSTALL.md](../../INSTALL.md) for system requirements.

## 2. Configure

Run the guided setup — it detects your project's language and writes a config:

```sh
jichi setup
```

For a minimal, safe starting point (good for learning), add `--profile beginner`.
Your API key is read from an **environment variable** (never stored in the config).

Check that everything is wired up:

```sh
jichi doctor      # health check
jichi benchmark   # best-practice coverage score
```

jichi can answer in your own language: add `"language": "Japanese"` (or any
language) to the config, or pass `--language` — see
[LANGUAGE.md](../../LANGUAGE.md).

## 3. Use it

Interactive session (a REPL):

```sh
jichi
```

Type your request in plain language. Point at code with `@file` or `@sym:name`.
Useful commands: `/help`, `/model`, `/mode`, `/constraints`, `/undo`. Press
**Ctrl-C** to stop the current task without quitting.

Headless (for scripts and automation):

```sh
jichi -p "explain what src/main.c does"
```

## 4. Stay in control

- **Modes:** chat (asks before acting), plan (read-only), auto (autonomous).
- **Constraints:** say "do not run the build" and it is *enforced*, not just
  remembered — see [CONSTRAINTS.md](../../CONSTRAINTS.md).
- **Undo:** every change is checkpointed; `/undo` reverts it — see
  [SNAPSHOTS.md](../../SNAPSHOTS.md).

## Where to go next

- The whole road, from first step to mastery: [JOURNEY.md](../../JOURNEY.md)
- Pick your journey: [WORKFLOWS.md](../../WORKFLOWS.md)
- Beginner walkthrough: [TUTORIAL_BEGINNER.md](../../TUTORIAL_BEGINNER.md)
- Power-user subsystems: [TUTORIAL_ADVANCED.md](../../TUTORIAL_ADVANCED.md)
