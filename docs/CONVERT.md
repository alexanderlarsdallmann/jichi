# Converting Continue and opencode configs

`jichi-convert` turns an existing **Continue** or **opencode** configuration into a
jichi config — and, optionally, a `.jichi/` asset tree (agents, commands,
`AGENTS.md`). The same conversion is available inside the agent as
`jichi setup --import <path>`, which writes the config and assets into the
current project in one step.

## Supported inputs

| Source | Files | Detected by |
|---|---|---|
| Continue (modern) | `config.yaml` / `config.yml` | YAML with a `models:` sequence |
| Continue (legacy) | `config.json` | top-level `models[]` array |
| opencode | `opencode.json` / `opencode.jsonc` | `$schema` mentioning opencode, or an object-valued `provider`/`agent`/`mcp`/`command`/`permission`, or a string `model` |
| Claude Code | a **directory** (project or `~/.claude/`) | contains `.claude/` or `CLAUDE.md`, or the path ends in `.claude` |

The format is auto-detected from the filename and content, so you rarely need to
say which it is. opencode's JSONC (`//` and `/* */` comments, trailing commas) is
handled; Continue YAML block scalars (`prompt: |`, `rule: >`) are handled.

### Claude Code (a config tree, not one file)

Point `jichi-convert` at a directory (e.g. `jichi-convert . --assets .` in a project,
or `jichi-convert ~/.claude`). It reads `.claude/settings.json` (→ an Anthropic
model on `ANTHROPIC_API_KEY` using the configured `model`, plus `mcpServers`),
`CLAUDE.md` (→ `AGENTS.md`), and `.claude/agents/*.md` + `.claude/commands/*.md`
(carried over verbatim — the frontmatter is compatible). Claude `permissions`
(the `Tool(spec)` pattern language), `hooks`, and `env` differ structurally from
jichi's and are **not** auto-mapped; they're reported as notes so you can port them
by hand (`docs/HOOKS.md`, `permissions`/`editScope`).

## What is converted

- **Models** → jichi `models[]`. The provider maps to `anthropic` or `openai`
  (anything else becomes OpenAI-compatible — set `apiBase`). API keys are **never**
  written literally unless the source had a real literal key; templates
  (`${{ secrets.X }}`, `{env:VAR}`, `$VAR`) and env references become `apiKeyEnv`.
  opencode's `provider/model` selector is split, and `provider.<id>.options`
  supplies `apiBase`/`apiKeyEnv`. Roles carry through (`chat`/`edit`/`embed`/…);
  opencode's `model`→`[chat,edit,apply]`, `small_model`→`[summarize,autocomplete]`.
- **MCP servers** → jichi `mcpServers[]` (stdio: `command`+`args[]`+`env{}`; remote:
  `url`+`headers[]`). opencode's `command:[cmd,args...]` array is split; a remote
  `headers{}` object is flattened to `"Name: value"` strings.
- **LSP** (opencode `lsp{}`) → jichi `lspServers[]`.
- **Docs** (Continue `docs[]`) → jichi `docs[]` (`startUrl`→`url`).
- **Instructions** (opencode `instructions[]`) → jichi `instructions[]`.
- **Permissions** (opencode `permission{}`) → jichi `permissions{allow,deny}`; a
  config that denies both `bash` and `edit` sets `mode: "plan"`.
- **Rules** (Continue `rules[]`, and legacy `systemMessage`) → `AGENTS.md`.
- **Prompts / commands** (Continue `prompts[]`/`customCommands[]`/`slashCommands[]`,
  opencode `command{}`) → `.jichi/commands/<name>.md`.
- **Agents** (opencode `agent{}`) → `.jichi/agents/<name>.md`, mapping the tool
  allow-list / permissions to `readonly` + a `tools:` list.

Hub references (`uses:`) can't be resolved offline and are skipped with a warning.
opencode `formatter{}` and per-model pricing have no jichi equivalent and are dropped.
Every unconvertible or lossy item is reported as a `note:` on stderr.

## Usage

```sh
# Print the jichi config to stdout (Continue or opencode auto-detected):
jichi-convert ~/.continue/config.yaml
jichi-convert ./opencode.jsonc

# From stdin:
cat opencode.json | jichi-convert -

# Write a whole project (config.json + AGENTS.md + .jichi/ tree) under a dir:
jichi-convert ./opencode.jsonc --assets ./myproject
jichi-convert ./opencode.jsonc --emit-assets          # == --assets .
jichi-convert ./opencode.jsonc --assets . --dry-run    # preview, write nothing

# In-agent bridge (writes into the current project, then run doctor):
jichi setup --import ~/.continue/config.yaml
jichi setup --import ./opencode.jsonc --config-target global --force
```

`--assets` writes non-destructively (existing files are skipped) unless `--force`;
`--dry-run` shows `+` (new) / `~` (overwrite) / `=` (skipped). Diagnostics go to
stderr so stdout stays clean for piping.

After importing, review the config, set the `apiKeyEnv` variable it references, and
run `jichi doctor` (or `jichi doctor --output json` for a
machine-readable health report).

## After setup: inspect and get recommendations

```sh
# Which config file is in effect, print it, or confirm it loaded:
jichi config path
jichi config show
jichi config validate

# Ask the configured model for a project-tailored setup (propose-only):
jichi setup --advisor
```

`setup --advisor` loads the active config, and — if the model has a key and its
server is reachable — sends the project's top-level files, the machine profile
(cores/RAM), the configured models, and the models the server actually offers,
then writes recommendations to `.jichi/setup.advice.md` for you to review and apply.
It never changes the config itself, and skips gracefully (with a hint) when no key
is set or the server is unreachable, so it is safe in headless/agent flows. On a
low-RAM host jichi also auto-enables the lean profile (override with `--no-lite`).

