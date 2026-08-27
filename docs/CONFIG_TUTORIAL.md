# Tutorial: configuring & using the model/agent options

> **Prerequisite:** you've built jichi and run a basic `jichi setup`
> ([TUTORIAL_BEGINNER.md](TUTORIAL_BEGINNER.md)). This is the *next* step —
> hand-editing the config for several servers, routing, and bounded autonomy. If
> you just want a first chat, stay in the beginner tutorial and come back later.

A hands-on walk through configuring jichi: where the config lives, **how to hold
your API key in an environment variable instead of a file**, how to make that
permanent in your shell, how to write a per-project launcher script, and then the
multi-server, routing, and bounded-autonomy options. Each step is runnable. Deep
dives live in [MODELS.md](MODELS.md), [ROUTING.md](ROUTING.md),
[AUTONOMY.md](AUTONOMY.md), and [PARALLEL.md](PARALLEL.md); this ties them
together.

By the end you'll have a key that lives in your environment and never in a file
you could commit, a `./run-jichi.sh` that starts this project the same way every
time, and one config that uses a local server (e.g. LM Studio) when it's up,
falls back to a remote (e.g. JLU Gießen) when it isn't, embeds locally, routes
cheap turns to the small model, and can run unsupervised inside hard limits.

**Read in order the first time.** §0–§2 are the ones everybody needs; §3 onward
are options you add when you want them.

| | section | you get |
| --- | --- | --- |
| §0 | [Where config lives](#0-where-config-lives) | which file jichi actually reads |
| §1 | [The API key](#1-the-api-key-environment-variables-step-by-step) | a key in the environment, permanently, checked |
| §2 | [A launcher script](#2-a-launcher-script-for-one-project) | `./run-jichi.sh` — one config, one key, every time |
| §3–§8 | models, roles, fallback, routing, agents | several servers, cheap-by-default |
| §9–§11 | autonomy, parallel, a full config | bounded unsupervised runs |
| §12–§13 | [quick reference](#12-quick-reference) + [troubleshooting](#13-troubleshooting-key-and-config-problems) | the tables |

---

## 0. Where config lives

There are **two** paths, and confusing them is a common (and until M284b, a
documented) mistake:

**An explicit config replaces everything.** The first of these that resolves is
used *alone* — no merge:

1. `--config <path>`
2. `$JC_CONFIG`

**Otherwise the global and the project config are MERGED.** With neither of the
above, jichi reads `~/.jichi` (global) and overlays a project config on it —
`./local/config.json`, else `.jichi/config.json`. Either file alone works;
neither means built-in defaults. The overlay rule (`jc_config_merge_json`):

- **scalar keys** — the project value wins (`testCommand`, `contextLimit`, …)
- **list keys** — the two **union**, with the project's entries **first**
  (`models`, `docs`, aliases …), so a project model takes precedence and the
  first model overall is the active one

`jichi doctor` prints which happened, e.g.
`~/.jichi + local/config.json (merged)`.

> **The merge has one sharp edge:** because `models` unions rather than
> replaces, a project entry that repeats a model already in `~/.jichi` yields
> **two** entries with the same name. Selectors are substring matches, so
> `"strong": "jlu/qwen3-coder-next"` then matches both and the winner is decided
> by position. `doctor` reports this as an ambiguous selector (M284) — the fix is
> to give project models **distinct, intent-based names** (`fast`, `strong`)
> rather than repeating the global's ids.

So for development, drop your config at `local/config.json` and it's overlaid
automatically whenever you run jichi inside the project — without exposing
keys to git, and without restating your models. Start from the secret-free
template:

```sh
mkdir -p local
cp examples/config.multi-server.json local/config.json
nano local/config.json         # or your editor of choice: set model ids / apiBase to match yours
```

The config is JSON. Verify it loads:

```sh
jichi models            # lists models; also a no-network sanity check of the file
```

---

## 1. The API key: environment variables, step by step

If your model server needs a key (a hosted gateway does; a local LM Studio or
Ollama does not), you have to get that key to jichi. **Put it in an environment
variable, not in the config file.** This section is the whole procedure, from one
throwaway command to a permanent, safe setup.

### 1.1 Why not just write the key in the config?

You *can* — `"apiKey": "sk-…"` works. Every step after that is where it goes
wrong: the file gets committed, copied into a bug report, synced to a backup, or
pasted into a chat. jichi warns about it:

```sh
jichi doctor
# ! literal apiKey in config (prefer apiKeyEnv)
#     my-model
```

The alternative is one line of indirection. In the config you write the **name**
of an environment variable; the key itself lives in your shell:

```json
{
  "models": [
    { "name": "strong", "provider": "openai", "model": "jlu/qwen3-coder-next",
      "apiBase": "https://api.hrz.uni-giessen.de/v1",
      "apiKeyEnv": "JICHI_API_KEY" }
  ]
}
```

> `apiKeyEnv` holds a **variable name, not a key**. `"apiKeyEnv": "sk-abc123"`
> means "read the variable called `sk-abc123`" — a name no shell can even
> `export`, so it can never resolve. If you find yourself pasting something that
> starts with `sk-` into `apiKeyEnv`, you want `apiKey` (and you don't want
> `apiKey`).
>
> This was easy to get wrong until M326e, because `setup` asked for it under the
> label "API key env var". `jichi doctor` now **FAILs** on a key-shaped
> `apiKeyEnv` and `setup` refuses one — neither of them echoing the value back,
> since if the guess is right that value is your key.

Now the config file is safe to commit, share, and copy between machines.

### 1.2 Set the key for a single command

The smallest possible step, and the right way to *test* a key before committing
to anything. A `VAR=value` prefix applies to that one command only:

```sh
# One command, one variable. Nothing is stored; the next command won't see it.
JICHI_API_KEY='sk-paste-your-key-here' jichi doctor
```

Note the **single quotes**: keys can contain `$`, `!` and backticks, which the
shell would otherwise interpret. Single quotes pass the value through literally.

### 1.3 Set the key for the current terminal

`export` puts the variable into the shell's environment, where every command you
start from that terminal inherits it:

```sh
export JICHI_API_KEY='sk-paste-your-key-here'

jichi doctor       # sees it
jichi models       # sees it too
```

Check what you set, without printing the secret to a screen someone may be
sharing:

```sh
echo "${JICHI_API_KEY:+set}"        # prints "set", or nothing at all
echo "${#JICHI_API_KEY}"            # prints the length, e.g. 51
```

This lasts until you close the terminal. Open a second tab and it's gone — which
is exactly the problem §1.4 solves.

> **Don't type a key with a leading space.** Some shells are configured to keep
> every command in history (`~/.bash_history`), and `export JICHI_API_KEY='sk-…'`
> is then a key sitting in a plain file forever. On the default Ubuntu bash,
> `HISTCONTROL=ignoreboth` means a command starting with a **space** is not
> recorded — so ` export JICHI_API_KEY='sk-…'` (leading space) is a decent
> stopgap. The real fix is §1.4: type it into a file once, never into a prompt.

### 1.4 Make it permanent: `~/.bashrc`

To have the key in every new terminal, it must be set by a file your shell reads
at startup. For bash that is `~/.bashrc`.

**The recommended pattern is two files, not one:** the key lives alone in a
private file, and `.bashrc` merely loads it. That way you can show someone your
`.bashrc`, keep it in a dotfiles repo, and sync it between machines without ever
moving the secret.

**Step 1 — create the private key file.** `umask 077` makes the file readable
only by you *at the moment it is created*, so there is no window where it is
world-readable:

```sh
# Create ~/.jichi.env with owner-only permissions from the start.
# NOTE: append (>>), never overwrite (>). `jichi setup` may already have written
# this file (it offers to store your key here, default yes), and you may want a
# second key in it later — `cat >` would silently destroy both.
( umask 077; touch ~/.jichi.env )
cat >> ~/.jichi.env <<'EOF'
# jichi API keys. Loaded by ~/.bashrc. Never commit this file.
export JICHI_API_KEY='sk-paste-your-key-here'
EOF

chmod 600 ~/.jichi.env    # belt and braces: -rw------- , owner only
ls -l ~/.jichi.env        # confirm: -rw------- 1 you you ... /home/you/.jichi.env
```

The `<<'EOF'` (quoted delimiter) matters: it stops the shell expanding `$` or
backticks inside the key.

**Step 2 — load it from `~/.bashrc`.** Append this block:

```sh
cat >> ~/.bashrc <<'EOF'

# --- jichi ---------------------------------------------------------------
# Load API keys from a private file (chmod 600) instead of storing them here,
# so ~/.bashrc stays safe to share, commit, and sync between machines.
[ -f "$HOME/.jichi.env" ] && . "$HOME/.jichi.env"
# -------------------------------------------------------------------------
EOF
```

The `[ -f … ] &&` guard means a machine without the key file still gets a working
shell instead of an error on every login.

**Step 3 — load it into the terminal you already have open.** `.bashrc` runs at
shell *startup*, so an existing terminal has not read your change yet:

```sh
. ~/.bashrc          # or: source ~/.bashrc — or just open a new terminal

jichi doctor         # ✓ API key present for the active model
```

**Which file, if not `.bashrc`?**

| your shell | interactive terminals | login shells (ssh, console) |
| --- | --- | --- |
| bash | `~/.bashrc` | `~/.profile` (or `~/.bash_profile` if you have one) |
| zsh | `~/.zshrc` | `~/.zprofile` |
| fish | `~/.config/fish/config.fish` | same |

For fish the syntax differs — `set -gx JICHI_API_KEY 'sk-…'`, and to load a
POSIX file use `bass` or simply keep the export in `~/.config/fish/config.fish`.

> **The trap that catches everyone: `~/.bashrc` does nothing for cron,
> `systemd`, or `ssh host 'command'`.** Ubuntu's stock `~/.bashrc` begins with
>
> ```sh
> case $- in
>     *i*) ;;
>       *) return;;    # <- not interactive? stop reading this file.
> esac
> ```
>
> so anything non-interactive exits before reaching your export. A scheduled or
> supervised jichi run therefore sees **no key** and fails with 401 — while the
> same command works when you type it. Give those runs the key explicitly:
> a `systemd` unit wants `EnvironmentFile=/home/you/.jichi.env` (drop the
> `export` keywords from the file — systemd wants bare `KEY=value`), a cron job
> or a script wants an explicit `. "$HOME/.jichi.env"`, which is what §2 does.

### 1.5 What jichi does with the key (the resolution order)

For each model, jichi takes the **first** of these that yields a non-empty value:

1. a literal `"apiKey"` in the config,
2. `getenv()` of the variable named by `"apiKeyEnv"`,
3. a provider convention — `OPENAI_API_KEY` when `"provider": "openai"`,
   `ANTHROPIC_API_KEY` otherwise,
4. nothing — jichi runs keyless (correct for a local server) and `doctor` warns.

Two consequences worth knowing:

- **Step 3 fires even with no `apiKeyEnv` at all.** If you already export
  `OPENAI_API_KEY` for other tools, an `openai`-provider model picks it up
  automatically. Convenient — and confusing when you *meant* to use a different
  key and wonder why the wrong account is billed. Name the variable explicitly
  with `apiKeyEnv` whenever more than one key is in play.
- **Per-model, not global.** Each model entry resolves its own key, so a config
  can mix a keyless local server with a keyed remote one, or two gateways with
  two different variables — which is the whole point of §4.

```json
{
  "models": [
    { "name": "local",  "provider": "openai", "model": "google/gemma-4-e4b",
      "apiBase": "http://127.0.0.1:1234/v1" },
    { "name": "work",   "provider": "openai", "model": "jlu/qwen3-coder-next",
      "apiBase": "https://api.hrz.uni-giessen.de/v1",
      "apiKeyEnv": "JICHI_API_KEY" },
    { "name": "personal", "provider": "anthropic", "model": "claude-sonnet-4-5",
      "apiKeyEnv": "MY_PERSONAL_ANTHROPIC_KEY" }
  ]
}
```

### 1.6 Check that it worked

```sh
jichi doctor
```

Read three lines of the output:

```
✓ config source
    /home/you/.jichi + local/config.json (merged)
✓ API key present for the active model
✓ model server reachable
    jlu/qwen3-coder-next: https://api.hrz.uni-giessen.de/v1
```

| what you see | what it means | fix |
| --- | --- | --- |
| `✓ API key present` | a key resolved (by any of the four steps) | — |
| `! no API key for the active model` | all four steps came up empty | is the variable exported *in this shell*? `echo "${JICHI_API_KEY:+set}"` |
| `! literal apiKey in config` | the key is in the file | move it to `apiKeyEnv` (§1.1) |
| `✗ apiKeyEnv is not a usable environment-variable name` | the **key** is in `apiKeyEnv`, where a variable name belongs | put the key in a variable and name *that* (§1.1); this is a FAIL because it can never resolve |
| `! model(s) with no API key` | a **non-active** model is keyless | fine for local servers; otherwise it 401s only once routing switches to it |

`doctor` never prints the key itself — only whether one was found. That makes it
safe to paste into a bug report.

`doctor` checks that a key *exists*, not that it is *correct*; only a real
request can do that. The cheapest one:

```sh
jichi -p "reply with the single word: ok"
```

### 1.7 Your key does not reach the commands the agent runs

Worth knowing in both directions. When jichi runs a shell command on the model's
behalf — `run_terminal_command`, `run_tests`, a verifier, a background job, an
MCP stdio server — it **removes the API keys from that child's environment**
first. Removed are the variable named by your `apiKeyEnv` *and* a built-in list
of well-known provider variables (`OPENAI_API_KEY`, `ANTHROPIC_API_KEY`,
`GEMINI_API_KEY`, `TAVILY_API_KEY`, and others), so a stray export in your shell
cannot leak either. Everything else in your environment passes through untouched.

- **The good half:** a model that decides to run `env` or `curl` cannot exfiltrate
  your key, and neither can a command it was tricked into running by a malicious
  file it read.
- **The half that surprises people:** if your *build* needs that key — a test
  suite hitting the same gateway, a deploy script — it will not find it. Pass it
  in deliberately for that one command, e.g. have the script read it from
  `~/.jichi.env` itself, rather than expecting to inherit it.

MCP servers are the documented exception: they get their keys from the `env`
block of their own `mcpServers` entry, which is applied *after* the scrub.

### 1.8 Every environment variable jichi reads

The complete list — jichi reads no others:

| variable | what it does |
| --- | --- |
| `OPENAI_API_KEY` | key for an `openai`-provider model with no `apiKeyEnv` |
| `ANTHROPIC_API_KEY` | key for any other provider with no `apiKeyEnv` |
| *(anything you name)* | via a model's `"apiKeyEnv"` — the recommended route |
| `JC_CONFIG` | path to the config file to use (see §0) |
| `JC_PROVIDER` | default provider when a model entry doesn't name one |
| `JICHI_LANG` | UI language (`en`, `de`, `es`, `ja`, `zh`) |
| `JICHI_DAEMON_SOCK` | socket path for the daemon mode |
| `NO_COLOR`, `COLUMNS`, `LANG`, `LC_ALL`, `LC_CTYPE`, `HOME`, `PATH` | the usual POSIX/terminal conventions |

> `JICHI_API_KEY` is **not** in this table, and that is not an omission. It is
> jichi's *suggested* name — the one the setup wizard and the examples write into
> `apiKeyEnv` — but jichi has no built-in knowledge of it. Name your variable
> whatever you like; only `apiKeyEnv` has to agree with it. Likewise
> `JICHI_CONFIG`, which appears in the generated launcher scripts, is a variable
> *those scripts* define for their own use — jichi's own config variable is
> `JC_CONFIG`.

### 1.9 Recommendations

1. **`apiKeyEnv`, never `apiKey`.** One line of indirection buys you a config
   file you can commit.
2. **One private file (`~/.jichi.env`, mode 600), sourced from `~/.bashrc`.**
   Keeps `.bashrc` shareable and gives scheduled runs one file to point at.
3. **Name the variable after the account, not the tool**, once you have more than
   one: `HRZ_API_KEY`, `MY_PERSONAL_ANTHROPIC_KEY`. `apiKeyEnv` makes which model
   uses which key explicit and reviewable.
4. **Never put a key on a command line.** `/proc/<pid>/cmdline` is
   world-readable (mode `0444`) and `ps -ef` shows every user's arguments;
   `/proc/<pid>/environ` is owner-only (`0400`). An environment variable is
   private to your account, an argument is public to the machine.
5. **Run `jichi doctor` after any change.** It is offline, instant, and prints
   which config file it read — which is the other half of every "why isn't it
   picking up my key" question.
6. **Rotate rather than debug a suspected leak.** If a key ever reached a git
   history or a paste, issuing a new one is faster and more certain than
   scrubbing.

---

## 2. A launcher script for one project

A project should start the same way for you tomorrow, for a colleague today, and
for the machine that runs it at 3am. That is a small shell script committed with
the code: it pins the **config**, loads the **key**, and sets the **posture**
(chat / plan / auto), so nobody has to remember a flag.

### 2.1 The minimal version

```sh
#!/bin/sh
# run-jichi.sh -- start jichi for THIS project.
exec jichi --config ./local/config.json "$@"
```

```sh
chmod +x run-jichi.sh    # make it executable, once
./run-jichi.sh           # interactive TUI
./run-jichi.sh -p "explain the build"   # extra arguments pass straight through
```

`exec` replaces the shell with jichi rather than leaving a parent process
hanging around, so Ctrl-C and exit codes reach you unchanged. `"$@"` (with the
quotes) forwards your arguments including ones containing spaces.

### 2.2 The recommended version: config **and** key

This is the one to copy. It reads the key from a private file, so the script
itself contains no secret and is safe to commit:

```sh
#!/bin/sh
# run-jichi.sh -- start jichi for this project with a pinned config and key.
#
#   ./run-jichi.sh                     interactive TUI
#   ./run-jichi.sh -p "a question"     one headless answer
#   ./run-jichi.sh --plan              investigate, change nothing
#
# The API key is NOT in this file. It is read from a private key file so this
# script can be committed. Create the key file once:
#
#   ( umask 077; echo "export JICHI_API_KEY='sk-...'" > ~/.jichi.env )
#
set -eu           # -e: stop on the first error   -u: an unset variable is an error

# Run from the project root no matter where the script was invoked from, so
# ./local/config.json and .jichi/ resolve the same way every time.
cd "$(dirname "$0")"

# Which binary. Override for a local build:  JICHI=./jichi ./run-jichi.sh
JICHI="${JICHI:-jichi}"

# Which config. Override for an experiment:  CONFIG=./local/alt.json ./run-jichi.sh
CONFIG="${CONFIG:-./local/config.json}"

# --- the key --------------------------------------------------------------
# Load it only if the environment does not already carry one, so an explicit
#   JICHI_API_KEY=... ./run-jichi.sh
# and a systemd EnvironmentFile both still win.
KEY_FILE="${JICHI_KEY_FILE:-$HOME/.jichi.env}"
if [ -z "${JICHI_API_KEY:-}" ] && [ -f "$KEY_FILE" ]; then
    # shellcheck source=/dev/null
    . "$KEY_FILE"
fi
if [ -z "${JICHI_API_KEY:-}" ]; then
    echo "run-jichi.sh: no JICHI_API_KEY." >&2
    echo "  create $KEY_FILE:  ( umask 077; echo \"export JICHI_API_KEY='sk-...'\" > $KEY_FILE )" >&2
    echo "  or run:            JICHI_API_KEY='sk-...' $0" >&2
    exit 1
fi
export JICHI_API_KEY     # the config's "apiKeyEnv": "JICHI_API_KEY" reads this

exec "$JICHI" --config "$CONFIG" "$@"
```

Three details that are deliberate:

- **`export`, not just an assignment.** `JICHI_API_KEY=...` alone sets a *shell*
  variable, which a child process does not inherit. `export` is what makes it an
  *environment* variable. (Sourcing a file whose lines already say `export`
  covers this; the explicit `export` makes the script work with a bare
  `KEY=value` file too.)
- **The environment wins over the file.** The `[ -z … ]` guard is what lets CI, a
  systemd unit, or a one-off `JICHI_API_KEY=… ./run-jichi.sh` override the
  developer default without editing anything.
- **A named error, not a 401.** Failing with "no JICHI_API_KEY" and the command
  that fixes it beats failing thirty seconds later inside an HTTP error.

Commit the script, ignore the secrets:

```sh
chmod +x run-jichi.sh
git add run-jichi.sh
printf 'local/\n.jichi/memory.md\n' >> .gitignore   # config with local paths; agent notes
```

### 2.3 One script per posture

Most projects want two or three fixed ways to start. Give each its own tiny
script — a name is easier to remember than a flag combination, and it is
reviewable in a way a shell-history recall is not:

```sh
#!/bin/sh
# ask.sh -- read-only question answering. Cannot modify the project.
set -eu; cd "$(dirname "$0")"
. "$HOME/.jichi.env"; export JICHI_API_KEY
exec jichi --config ./local/config.json --readonly -p "$*"
```

```sh
#!/bin/sh
# review.sh -- plan mode: investigate and propose, change nothing.
set -eu; cd "$(dirname "$0")"
. "$HOME/.jichi.env"; export JICHI_API_KEY
exec jichi --config ./local/config.json --plan "$@"
```

```sh
#!/bin/sh
# fix-tests.sh -- bounded unsupervised run. See §9 for what each bound does.
set -eu; cd "$(dirname "$0")"
. "$HOME/.jichi.env"; export JICHI_API_KEY
exec jichi --config ./local/config.json --auto \
    --verify 'make && make test' \
    --budget-tokens 200k --deadline 20m --max-tool-calls 60 \
    --edit-scope 'src/**' --edit-scope 'tests/**' \
    --journal "$HOME/.jichi.d/runs/fixtests-$(date +%Y%m%d-%H%M%S).jsonl" \
    -p "${1:-run the tests and fix any failures}"
```

> Note `--journal` writes **outside** the project. A verifier failure rolls the
> workspace back, and a log kept inside it would be rolled back with everything
> else — the log of what happened, deleted by what happened.

### 2.4 Two keys, two projects, one machine

Where a launcher script earns its keep. Each project's script names its own
variable; the config in that project names the same one in `apiKeyEnv`. Nothing
global has to change when you switch between them:

```sh
# ~/.jichi.env  (mode 600) -- every key you have, in one private file
export HRZ_API_KEY='sk-...'         # the university gateway
export ACME_API_KEY='sk-...'        # the client project
```

```sh
#!/bin/sh
# acme/run-jichi.sh -- this project bills the client's account, never the
# university one. The config says  "apiKeyEnv": "ACME_API_KEY".
set -eu; cd "$(dirname "$0")"
. "$HOME/.jichi.env"
export ACME_API_KEY
unset HRZ_API_KEY OPENAI_API_KEY ANTHROPIC_API_KEY   # cannot be picked up by accident
exec jichi --config ./local/config.json "$@"
```

The `unset` line is the point: because jichi falls back to `OPENAI_API_KEY` /
`ANTHROPIC_API_KEY` when a model names no `apiKeyEnv` (§1.5), removing them makes
a misconfiguration fail loudly instead of quietly charging the wrong account.

### 2.5 Keeping the config out of `ps` too

For a fully self-contained run, jichi can take the whole config as inline JSON.
Beware the obvious form:

```sh
# DON'T: the entire config, key included, is visible to every user via `ps`.
jichi --config-json '{"models":[{"model":"x","apiKey":"sk-secret"}]}' -p "hi"
```

Pass it on **stdin** instead — and keep using `apiKeyEnv`, so the key is not in
the JSON at all:

```sh
#!/bin/sh
# Self-contained run: no config file on disk, nothing sensitive in `ps`.
set -eu
. "$HOME/.jichi.env"; export JICHI_API_KEY

cat <<'JSON' | jichi --config-stdin -p "summarise the README"
{
  "models": [
    { "name": "strong", "provider": "openai", "model": "jlu/qwen3-coder-next",
      "apiBase": "https://api.hrz.uni-giessen.de/v1",
      "apiKeyEnv": "JICHI_API_KEY" }
  ],
  "snapshots": false
}
JSON
```

`--config-stdin` (equivalently `--config-json -`) consumes stdin, so the prompt
must come from `-p` or `--prompt-b64` rather than from a pipe.

### 2.6 Recommendations

1. **Commit the launcher, ignore the config that has local paths in it.** The
   script is documentation that runs; `local/` stays out of git.
2. **`set -eu` and `cd "$(dirname "$0")"` in every script.** The first stops a
   failed step from being ignored, the second makes the script work from any
   directory.
3. **Let the environment override the file.** One `[ -z "${VAR:-}" ]` guard makes
   the same script usable by a developer, CI, and systemd.
4. **Fail with the fix.** An error message containing the command that repairs it
   is worth more than any amount of README.
5. **Use `--journal` and point it outside the project** for anything running
   `--auto` — see [AUTONOMY.md](AUTONOMY.md) and, for unattended loops,
   [AUTONOMOUS_LOOPS.md](AUTONOMOUS_LOOPS.md).
6. **`jichi setup` writes a starter script for you** (`jichi setup --list` shows
   the role presets), and since M326e it does the key too: the interactive wizard
   offers to store it in `~/.jichi.env` (mode 600, typed at a prompt that does
   not echo), and every generated script loads that file behind the same `[ -f
   … ] &&` guard used above. §2.2 remains the version to reach for when you want
   the failure message, the environment override, and a script you wrote.

---

## 3. One model (the baseline)

The minimum is a single model. Any OpenAI-compatible server works via
`provider: "openai"` + `apiBase`:

```json
{
  "model": {
    "provider": "openai",
    "model": "google/gemma-4-e4b",
    "apiBase": "http://127.0.0.1:1234/v1",
    "apiKey": "lm-studio"
  }
}
```

- **End `apiBase` in `/v1`.** Then both chat (`/v1/chat/completions`) and
  embeddings (`/v1/embeddings`) resolve. (A base without `/v1` also works — it's
  inserted — but `/v1` is the unambiguous form.)
- **Local servers need no key.** Omit `apiKey`/`apiKeyEnv` (a harmless "no API
  key" warning prints), or set a dummy value to silence it.
- **Keys without secrets in the file:** use `"apiKeyEnv": "MY_KEY"` to read the
  key from an environment variable.

---

## 4. Several models on several servers

Use a `models` array instead of `model`. Each entry names its own server, so you
can mix remote + local in one config:

```json
{
  "models": [
    { "name": "jichi-gemma", "provider": "openai", "model": "jlu/gemma-4-31b-it",
      "apiBase": "https://api.hrz.uni-giessen.de/v1", "apiKeyEnv": "JICHI_API_KEY" },
    { "name": "local-gemma", "provider": "openai", "model": "google/gemma-4-e4b",
      "apiBase": "http://127.0.0.1:1234/v1" }
  ]
}
```

> **The env-var name is a placeholder, not a requirement.** `apiKeyEnv` names
> whichever variable holds *your* key — `JICHI_API_KEY` here, jichi's generic
> default. The examples point `apiBase` at the JLU HRZ gateway (staff and
> students can request a key, or are handed one in class); HRZ users can reuse
> theirs with `export JICHI_API_KEY="$JLU_API_KEY"`. Anyone else names the
> variable what they like (`LLM_API_KEY`, `OPENAI_API_KEY`, …), points `apiBase`
> at their own provider, and brings their own key — that bring-your-own-key case
> is exactly what jichi is built for.

The **first** model is the active (default) one. Inspect and switch:

```sh
jichi models                 # list all; * marks the active one
```

In the interactive TUI: `/model` lists models, `/model <name|index>` switches.
On the CLI, `--model <selector>` overrides for one run.

A **selector** (used everywhere a model is named) is a **name** or **model-id**
substring (case-insensitive), a **1-based index**, or a **role** name. Pick
substrings that are unambiguous — e.g. `e4b` for the local gemma vs `31b` for the
remote one.

---

## 5. Roles: send specific jobs to specific models

Add a `roles` array to a model. Roles route non-chat work:

| role | used for |
| --- | --- |
| `embed` | embedding code chunks + queries (`index`, `codebase_search`, `embed`) |
| `rerank` | reordering search hits by relevance |
| `summarize` | auto-compaction of long histories |
| `chat`, `edit`, `apply`, `autocomplete` | parsed; selectable by name/role |

A great local win is **local embeddings** — free, offline, fast indexing:

```json
{ "name": "local-embed", "provider": "openai",
  "model": "text-embedding-nomic-embed-text-v1.5",
  "apiBase": "http://127.0.0.1:1234/v1", "roles": ["embed"] }
```

> **Re-index after changing the embed model.** The index records which embedding
> model built it; pointing `embed` at a new model triggers a full re-embed. Run
> `jichi index --reindex` once after switching.

---

## 6. Fallback: survive a server being down

A model may name a **`fallback`** (a selector). When that model is *used* and its
server is unreachable, jichi probes it (`GET {apiBase}/models`, short
timeout) and transparently switches to the first reachable model in the chain:

```json
{ "name": "local-gemma", "provider": "openai", "model": "google/gemma-4-e4b",
  "apiBase": "http://127.0.0.1:1234/v1", "roles": ["summarize"],
  "fallback": "jichi-gemma" },
{ "name": "local-embed", "provider": "openai",
  "model": "text-embedding-nomic-embed-text-v1.5",
  "apiBase": "http://127.0.0.1:1234/v1", "roles": ["embed"],
  "fallback": "jichi-embed" }
```

See it live:

```sh
jichi models
#  6) local-gemma   google/gemma-4-e4b   [reachable]      (LM Studio running)
#       http://127.0.0.1:1234/v1  roles=summarize  fallback=jichi-gemma
# ...stop LM Studio...
jichi models
#  6) local-gemma   google/gemma-4-e4b   [UNREACHABLE]
```

When a fallback fires during a run you'll see:

```
[fallback] local-gemma unreachable -> jichi-gemma
```

Notes: probing is **opt-in** — a model with no `fallback` is never probed (no
added latency). Fallback applies to the active model, the routing tiers, and role
lookups (embed/summarize/rerank). If a whole chain is down, the original is kept
and the request fails/retries as usual.

---

## 7. Routing: cheap by default, strong when it's hard

Tiered routing runs each turn on a **`fast`** model and escalates to a
**`strong`** model on a hard step (by default, when the autonomy verifier fails).
Combine it with `fallback` so "fast" can be a local model that degrades to the
remote:

```json
"routing": {
  "enabled": true,
  "fast": "gemma-4-e4b",       // local small model (escalates / falls back)
  "strong": "qwen3-coder",     // remote, for hard turns
  "escalateOnVerify": true,    // default: escalate after a verify failure
  "escalateOnError": false     // default: don't escalate on every tool error
}
```

Behaviour:

- Each turn starts on `fast`; once escalated it stays `strong` for that turn and
  resets next turn. You'll see `[route] -> local-gemma (turn-start)` and, on a
  hard step, `[route] -> qwen3-coder (verify_fail)`.
- Routing is **inert** unless `fast` and `strong` resolve to two distinct models
  (so it's safe to leave `enabled`).

Override per run / interactively:

```sh
jichi --route-fast e4b --route-strong qwen3-coder -p "..."   # set tiers
jichi --no-route --model qwen3-coder -p "..."                # pin one model
```

```
/route                 # show tiers, escalation flags, current model
/route on | off
/route fast <model> | strong <model>
```

---

## 8. Specialist agents on specific models

Sections 3–7 are all *config*. The other half of a tuned project lives in
`.jichi/agents/*.md` — named subagent profiles — and until M284 this tutorial
never mentioned them, so the two halves were documented apart and the
composition was documented nowhere. It is the most useful thing here.

A profile's frontmatter takes a `model:` **selector**, and a selector resolves
three ways, in this order:

1. a **1-based index** into your `models` array (`model: 2`),
2. a case-insensitive **substring** of a model's `name` or `model` id,
3. a **role** name (`model: summarize` follows whichever model holds that role).

```markdown
---
description: Reviews changes for correctness and risk (read-only).
model: strong
readonly: true
tools:
  - read_file
  - search_code
  - git_diff
---
You are a meticulous code reviewer. You do not modify files.
```

**Name models by intent, not by vendor id.** Call them `fast` and `strong` in
your config, and a profile that says `model: strong` keeps working when you
change which model plays that part. Pinning `model: gpt-5-codex` in eleven
profiles means eleven edits the day you switch. The scaffolded profiles
(`jichi init`) ship this line commented out, next to a `config.example.json`
that defines the two tiers.

Three surfaces take a selector, and they do **different** jobs:

| where | scope | use it for |
| --- | --- | --- |
| `routing` (§7) | top-level turns only | cheap-by-default with escalation on hard steps |
| an agent profile's `model:` | that subagent's whole run | a specialist that always wants a particular tier |
| a command's `model:` | one turn | a `/slash` command that should run somewhere specific |

Routing does **not** reach subagents (it is gated on `agent_depth == 0`), so a
profile's `model:` is the only way to place a specialist on a tier. They compose:
a routed top-level turn can spawn a subagent pinned to `strong` while the parent
is still on `fast`.

**Skills carry no model.** A skill (`.jichi/skills/<name>/SKILL.md`) is
instructions plus an optional tool fence — it has no `model:` key. To run a
skill's instructions on a chosen model, pair it with a profile:

```
spawn_subagent{agent: "reviewer", skill: "valgrind-triage"}
```

The profile supplies the model and posture; the skill supplies the instructions
and, when it declares `restrict-tools: true`, an enforced tool allow-list
(intersected with the profile's). A command with both `agent:` and `model:` in
its frontmatter is the deterministic version of the same pairing.

**Check your work:** `jichi doctor` resolves every selector in your profiles,
commands, and routing tiers, and reports the three ways one can be wrong —
unresolvable (a FAIL), ambiguous because it substring-matches several models
(a warning; the first wins *by position*), or naming a role no model declares.
Before M284 a typo'd profile selector was found only when the subagent was
spawned — a tool error mid-run — and a typo'd routing tier was never reported at
all, because routing simply stays inert.

```sh
jichi agents     # every profile with its resolved model/readonly/tools
jichi doctor     # ✓ model selectors resolve
```

---

## 9. The autonomy envelope: bounded unsupervised runs

For hands-off work, wrap a run in limits + a verifier. Flags imply `--auto` in
headless mode:

```sh
jichi --auto \
  --verify 'make && make test' \
  --budget-tokens 200k --deadline 20m --max-tool-calls 60 \
  --edit-scope 'src/**' --edit-scope 'tests/**' \
  -p "Fix the failing parser tests"
```

What each does:

- `--verify <cmd>` — runs when the agent finishes; it **must** exit 0. On failure
  the agent fix-forwards (`--verify-retries`, default 3; `0` rolls back on the
  first failure). When retries run out, the workspace **rolls back** to the last
  green checkpoint and the run exits 1. `--no-rollback` leaves it for inspection.
- `--verify-timeout <dur>` — kill a hung verifier (e.g. `5m`).
- `--verify-baseline` — run the verifier once at the start and record whether the
  tree was already broken.
- `--budget-tokens` / `--deadline` / `--max-tool-calls` — hard caps; hitting one
  stops (and rolls back) the run.
- `--edit-scope <glob>` (repeatable) — restrict `edit_file`/`write_file` to
  matching paths. Add `--strict-scope` to also forbid `run_terminal_command`
  (no shell escape).
- `--journal <path|->` — JSONL audit log (default
  `~/.jichi.d/runs/<id>.jsonl`; keep it **outside** the workspace, or a
  rollback will revert it).

Config-file defaults for the project-stable parts:

```json
"verify": "make && make test",
"editScope": ["src/**", "tests/**"]
```

In the TUI, `/verify` runs the configured verifier on demand. Full model:
[AUTONOMY.md](AUTONOMY.md).

---

## 10. Parallel agents (optional)

`spawn_parallel` runs independent subtasks concurrently in a fork pool sized to
the CPU. Cap it:

```json
"maxParallelAgents": 4      // 0 = auto (min(cores, 8))
```

Read-only tasks fan out for investigation; `write:true` tasks each edit an
isolated git worktree merged back file-level. See [PARALLEL.md](PARALLEL.md).

---

## 11. A complete dev config

Putting it together (this is `examples/config.multi-server.json`, secret-free):

```jsonc
{
  "models": [
    { "name": "jichi-gemma",  "provider": "openai", "model": "jlu/gemma-4-31b-it",
      "apiBase": "https://api.hrz.uni-giessen.de/v1", "apiKeyEnv": "JICHI_API_KEY",
      "roles": ["chat", "edit", "apply"] },
    { "name": "jichi-qwen",   "provider": "openai", "model": "jlu/qwen3-coder-next",
      "apiBase": "https://api.hrz.uni-giessen.de/v1", "apiKeyEnv": "JICHI_API_KEY",
      "roles": ["chat"] },
    { "name": "jichi-embed",  "provider": "openai", "model": "jlu/qwen3-embedding",
      "apiBase": "https://api.hrz.uni-giessen.de/v1", "apiKeyEnv": "JICHI_API_KEY" },
    { "name": "local-gemma", "provider": "openai", "model": "google/gemma-4-e4b",
      "apiBase": "http://127.0.0.1:1234/v1", "roles": ["summarize"], "fallback": "jichi-gemma" },
    { "name": "local-embed", "provider": "openai", "model": "text-embedding-nomic-embed-text-v1.5",
      "apiBase": "http://127.0.0.1:1234/v1", "roles": ["embed"], "fallback": "jichi-embed" }
  ],
  "routing": { "enabled": true, "fast": "gemma-4-e4b", "strong": "qwen3-coder",
               "escalateOnVerify": true },
  "verify": "make && make test",
  "editScope": ["src/**", "tests/**"],
  "maxParallelAgents": 4,
  "snapshots": true
}
```

Drive it:

```sh
cp examples/config.multi-server.json local/config.json   # then edit + export the key
export JICHI_API_KEY=...                                    # not stored in the file
jichi models                                      # confirm reachability
jichi index --reindex                             # build the index with local embeddings
jichi -p "explain the agent loop"                 # routine -> local; hard -> remote
```

---

## 12. Quick reference

| Option | Config key | CLI | TUI |
| --- | --- | --- | --- |
| **API key** (recommended) | `models[].apiKeyEnv` — the *name* of an env var | — | — |
| API key (discouraged) | `models[].apiKey` — the literal key; `doctor` warns | — | — |
| API key (implicit) | — | `$OPENAI_API_KEY` / `$ANTHROPIC_API_KEY` by provider | — |
| Models / servers | `models[]` (`apiBase`, `apiKey`/`apiKeyEnv`, `roles`) | `--model`, `--config` | `/model` |
| Fallback | `models[].fallback` | — | — |
| Reachability view | — | `models` | — |
| Routing | `routing{fast,strong,escalateOnVerify,escalateOnError}` | `--route-fast`, `--route-strong`, `--no-route` | `/route` |
| Verifier gate | `verify` | `--verify`, `--verify-retries`, `--verify-timeout`, `--verify-baseline`, `--no-rollback` | `/verify` |
| Budgets | — | `--budget-tokens`, `--deadline`, `--max-tool-calls` | — |
| Edit scope | `editScope[]` | `--edit-scope`, `--strict-scope` | — |
| Audit journal | — | `--journal` | — |
| Parallel cap | `maxParallelAgents` | — | — |
| Parallel child timeout | `parallelTaskTimeout` | — | — |
| Shell memory watchdog | `memBudgetMb` | `--mem-budget <MB>` | — |
| Shell command timeout | `runTimeout` | `--run-timeout <s>` | — |
| Config location | — | `--config`, `$JC_CONFIG`, `./local/config.json` | — |
| Config inline / on stdin | — | `--config-json`, `--config-json-b64`, `--config-stdin` | — |

Other long-standing knobs (`maxToolIters`, `maxRetries`, `maxSubagentDepth`,
`maxSubagentIters`, `autoCompact`, `contextLimit`, `snapshots`, `snapshotLimit`,
`mode`, `permissions`, `instructions`, `mcpServers`, `lspServers`) are covered in
the README and the per-feature docs.

### Four knobs that were hard to find (M325)

Each of these is real, has been for a while, and was mentioned nowhere an operator would
look it up — three of them only in the roadmap. That is a documentation bug, and the
[config-key lint](../tests/smoke/config_keys_lint.sh) had been treating a mention in the
roadmap as "documented", which is how they stayed hidden.

```json
{
  "parallelTaskTimeout": 900,   // seconds a spawn_parallel CHILD may run (default 300)
  "memBudgetMb": 2048,          // kill a shell command that exceeds this RSS (0 = off)
  "runTimeout": 120,            // seconds a shell command may run (0 = no limit)
  "wisdom": true                // the TUI's occasional craft note (default true; /wisdom)
}
```

- **`parallelTaskTimeout`** — the per-child watchdog. **The default of 300 s is the one to
  check first** if `spawn_parallel` children keep failing: a measured workload's *successful*
  parallel calls ran 300–462 s, so the default was killing about half of them. See
  [PARALLEL.md](PARALLEL.md#when-a-child-fails-measured-m325).
- **`memBudgetMb`** — a watchdog on `run_terminal_command`/`run_tests`: a command whose
  resident memory exceeds this is killed, so one runaway build cannot take the machine down.
  Off by default. Also `--mem-budget`.
- **`runTimeout`** — a default wall-clock limit for shell commands, so a hung command does not
  hold a turn forever. Off by default; a per-call `timeout` argument overrides it. Also
  `--run-timeout`.
- **`wisdom`** — whether the TUI occasionally prints a short craft note. Cosmetic; `/wisdom`
  toggles it for a session.

`"systemPrompt": "..."` appends free text to the system prompt — both the
top-level one **and** every subagent's. It is the bluntest of the four ways to shape
what jichi is:

| Want | Use | Scope |
|---|---|---|
| a standing instruction for everything | **`systemPrompt`** (this key) | the session, incl. subagents |
| tone/format/verbosity, switchable | an **output style** ([`OUTPUT_STYLES.md`](OUTPUT_STYLES.md)) | the session, `/output-style` |
| one specialist's tone | a profile's or skill's **`style:`** (M302) | while that specialist runs |
| replace the persona for one turn | a command's **`agent:`** frontmatter | that turn |

Prefer an output style when you might want to switch it, and `systemPrompt` for a
fact that is simply always true of this project. It is **not** a replacement for
`AGENTS.md`: project *rules* belong there, where they are versioned with the code
and visible to a reader ([`RULES.md`](RULES.md)).

> This key shipped long before M305 and was documented **nowhere** — found by the
> config-key coverage lint that milestone added. If you went looking for "how do I
> configure the system prompt" and concluded you could not, that was the docs' fault,
> not yours.

`"timeFormat": "%X"` (default) is the `strftime` pattern for the wall-clock stamp on
the TUI's reply header (`fast (jlu/…) · chat · 21:55:32`). `"%H:%M"` drops the
seconds; a locale-aware `%X` is the default. TUI only.

`"numberFormat": "auto"` (default) groups large numbers with the OS locale's
separator, falling back to `.` when the locale has none. `"off"`/`"none"` disables
grouping; any other value uses its first character (`"."`, `","`, `" "`). It affects
readability of token counts and costs only.

`"craft": false` (default **true**, but **false under `--lite`** — see the measurement
below) removes the *How to work* section from the
system prompt (M299). That section asks the agent to understand before changing,
ask when two readings would lead to different work, **write the design and the
decisions — including rejected alternatives — before implementing**, then
implement → test → correct → refactor, prove a test can fail before trusting it,
and say plainly what is unverified or skipped. Even a very short program gets a
short design note: the cost of a paragraph is nothing against the cost of not
knowing why.

It is appended even when a command's `agent:` profile overrides the persona,
because *how* to work is not the same thing as *who* is working — a blunt reviewer
still analyses before concluding. The reasoning behind it — and the constraint that
every line be checkable behaviour rather than vocabulary — is in
[`PHILOSOPHY.md`](PHILOSOPHY.md), under 職人気質（しょくにんかたぎ）.

**What it costs, and what that bought on a small model** (M318, pre-registered A/B,
[full numbers](analysis/2026-08-06-craft-ab.md)): the section is **329–386 tokens on every
model call** — the largest single part of a graded attempt's prompt. On a 31B local model,
18 runs across three graded tasks passed **18/18 in both conditions**, including the *design*
task; and on an under-specified probe (a prompt naming a goal and no deliverable) neither
condition wrote a design note or named a rejected alternative, while craft-on cost 15–69%
more per run.

So it is **off by default under `--lite`**, alongside `repoMap`/`references`/`markdown`, and
**on everywhere else** — because what 18 runs of one 31B model license is a statement about
small models, not about the section. Its claimed value is for larger models (window to spare)
and larger, vaguer tasks (where the design note is the deliverable nobody asked for), neither
of which was tested and neither of which a lean run is doing. An explicit `craft` key wins in
both directions.

`"assignments": true` (default false) opts into the SDLC assignment workflow —
the agent writes assignment + reference-solution files for lifecycle tasks. Pair
it with `jichi init assignments`. See
[`ASSIGNMENTS.md`](ASSIGNMENTS.md).

---

## 13. Troubleshooting: key and config problems

Start every diagnosis with `jichi doctor`. It is offline, instant, prints which
config file it actually read, and never prints your key.

### "no API key for the active model"

jichi found nothing at any of the four steps in §1.5. In order of likelihood:

```sh
# 1. Is the variable set IN THIS SHELL? (a new terminal after editing ~/.bashrc,
#    or a `. ~/.bashrc` in the old one)
echo "${JICHI_API_KEY:+set}"          # empty output = not set here

# 2. Is it EXPORTED, or merely a shell variable? Only exported vars are inherited.
export | grep -c JICHI_API_KEY        # 0 = assigned but not exported

# 3. Does the config name the same variable you set?
grep -n apiKeyEnv ~/.jichi ./local/config.json 2>/dev/null

# 4. Is jichi even reading the config you are editing?
jichi config path         # e.g. "~/.jichi + local/config.json (merged)"
```

Step 4 is the one people reach last and should reach first: a `--config` flag or
a leftover `$JC_CONFIG` makes jichi ignore both `~/.jichi` and your project file
entirely (§0).

### It works when I type it, but not from cron / systemd / `ssh host 'jichi …'`

The classic. Those are **non-interactive** shells, and the stock `~/.bashrc`
returns before reaching your export (§1.4). Give them the key explicitly:

```ini
# systemd unit — bare KEY=value lines, no `export`
[Service]
EnvironmentFile=/home/you/.jichi.env
```

```sh
# cron / any script — source it yourself, with an absolute path
0 3 * * *  . "$HOME/.jichi.env"; /usr/local/bin/jichi --config /srv/proj/local/config.json --auto -p "..."
```

### The wrong account is being billed

You have `OPENAI_API_KEY` (or `ANTHROPIC_API_KEY`) exported for another tool, and
a model entry with no `apiKeyEnv` silently picked it up (§1.5, step 3). Give every
keyed model an explicit `apiKeyEnv`, and `unset` the conventional variables in
your launcher script (§2.4).

### My build/test command can't find the key any more

Expected: jichi strips API keys from every command it runs on the model's behalf
(§1.7). Have the command load the key itself — `. "$HOME/.jichi.env"` inside your
test script — rather than expecting to inherit it.

### `config path` names a file I didn't mean to use

```sh
jichi config path                # what jichi actually loaded
echo "${JC_CONFIG:-unset}"       # a stale export is the usual culprit
unset JC_CONFIG
```

Otherwise check the precedence in §0: `--config` beats `$JC_CONFIG` beats the
`~/.jichi` + project merge.

### Two models with the same name, and the wrong one is chosen

The global/project merge **unions** the `models` list rather than replacing it, so
repeating a global model in your project config gives you two entries and a
selector that matches both (§0). `doctor` reports it as an ambiguous selector.
Give project models distinct, intent-based names (`fast`, `strong`).

### I think a key leaked

Rotate it. Issuing a new key takes a minute; proving that an old one never reached
a git history, a backup, or a log takes an afternoon and cannot be finished. Then
move to `apiKeyEnv` (§1.1) so the next one cannot follow it.

### Anything else

```sh
jichi doctor            # config, keys, reachability, MCP/LSP, project assets
jichi models            # every model, its server, its roles, live reachability
jichi config path       # which config file(s) are in effect — both, if merged
jichi config validate   # confirm it parsed; how many models it found
jichi config show       # the resolved active model and a few session settings
jichi status            # the resolved session: model, mode, routing, cwd
```

See [DOCTOR.md](DOCTOR.md) for every check and
[HARDENING.md](HARDENING.md) for the threat model behind the key handling.
