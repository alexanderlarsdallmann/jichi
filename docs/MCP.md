# MCP servers — giving the agent tools jichi does not have

**MCP** (Model Context Protocol) is a small open protocol for exposing *tools*,
*resources* and *prompts* from a separate program to an AI agent. jichi is an MCP
**client**: you name a server in your config, jichi starts or connects to it,
asks what it offers, and registers each of its tools alongside jichi's own — so the
agent can call them exactly like `read_file`.

That is the whole point: jichi ships ~45 built-in tools and will never ship a
Jira tool, a Postgres tool, or your company's deployment tool. MCP is how those
reach the agent without a line of C.

> **New here? The one thing to get right:** `mcpServers` is a **JSON array**, not
> an object. Claude Code and Continue key theirs by server name, so that is what
> most examples on the internet look like — and jichi will now *warn* you rather
> than silently configuring nothing (it used to do the latter, M395). See
> [The config shape](#the-config-shape).

## When to use it, and when not to

**Use it** when the agent needs to reach a system jichi knows nothing about — an
issue tracker, a database, a browser, an internal service — and a program already
exists (or is easy to write) that speaks MCP.

**Do not reach for it** when a shell command would do. `run_terminal_command` can
already call `curl`, `psql` or your own script, and a
[user-defined tool](USER_TOOLS.md) wraps a command with a proper schema in ten
lines of config — no server, no process, no protocol. MCP earns its cost when the
capability is genuinely someone else's software with its own lifecycle.

## The config shape

An **array** of entries, each with a `name`. Two transports:

```json
{
  "mcpServers": [
    {
      "name": "fs",
      "type": "stdio",
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/srv/data"],
      "env": { "LOG_LEVEL": "warn" },
      "autoApprove": ["read_file", "list_directory"]
    },
    {
      "name": "issues",
      "url": "https://mcp.example.internal/v1",
      "headers": ["Authorization: Bearer <token>"],
      "deny": ["delete_issue"]
    }
  ]
}
```

| Key | Meaning |
|---|---|
| `name` | **Required.** Prefixes every tool from this server (see namespacing). An entry with no name is skipped with a warning. |
| `type` | `stdio` (jichi spawns `command` and speaks newline-framed JSON-RPC over its pipes) or `http` (streamable HTTP to `url`). **Optional:** omit it and jichi infers `http` when `url` is set, `stdio` otherwise — so the second entry above needs no `type`. |
| `command`, `args`, `env` | For `stdio`. `args` is an **array of strings**; `env` is an **object** of `NAME: value` pairs, added to the child's environment. |
| `url`, `headers` | For `http`. `headers` is an **array of whole header lines** (`"Authorization: Bearer …"`), *not* an object of name→value — a shape worth checking twice, since a wrong one is simply ignored. |
| `autoApprove` | Tool names from this server that run **without** an approval prompt. Also accepts `"*"` or `true` for *every* tool this server offers — a real decision, not a convenience: you are pre-approving whatever the server happens to expose today **and** after its next update. |
| `deny` | Tool names that are **never** offered to the model and refused if asked for anyway. Accepts `"*"` / `true` too, which is a useful way to keep a server's resources and prompts while offering none of its tools. |
| `kinetic` | `true` marks this server's tools as actuating hardware, so they pass the kinetic gate — see [ROBOTICS.md](ROBOTICS.md). |

> **The shape trap, once more.** `"mcpServers": { "fs": { … } }` — an object — is
> not jichi's shape. You will get a warning naming the problem and **zero**
> configured servers. `lspServers` has the same array shape and the same warning.

Config lives wherever your config lives (`~/.jichi` globally, `local/config.json`
per project — [CONFIG_TUTORIAL.md](CONFIG_TUTORIAL.md) §0). `jichi doctor` reports
how many servers connected, which is the fastest way to know it worked.

## What you get, and what it is called

After connecting, jichi runs `initialize` then `tools/list`, and registers each
remote tool under **`<server>__<tool>`** — two underscores. So the `fs` server's
`read_file` becomes `fs__read_file`, which cannot collide with jichi's own
`read_file`. A server that fails to connect is logged and skipped; the rest of the
session works normally.

It also probes `resources/list` and `prompts/list`. A server that does not
implement them errors, and jichi tolerates and skips that — those are optional.

```sh
jichi mcp                     # list connected servers and their tools
jichi mcp call fs__read_file '{"path":"/srv/data/notes.md"}'
jichi mcp resources           # list discovered resources
jichi mcp read <uri>          # print one resource's text
jichi mcp prompts             # list discovered prompts
jichi mcp prompt review path=src/x.c   # render one prompt's messages
```

**Three ways the agent and you reach the same things:**

- **Tools** — the agent calls `<server>__<tool>` like any tool. You can call one
  yourself with `jichi mcp call`.
- **Resources** — the agent uses the read-only `read_mcp_resource` tool (only
  registered when a server actually advertises resources); you can write
  **`@mcp:<uri>`** in a message to inline one ([REFERENCES.md](REFERENCES.md)).
- **Prompts** — a server's prompt becomes a **slash command**. `/review` runs
  the `review` prompt if no file command of that name exists (files win). Bare
  prompt names appear in `/help` and Tab completion, and arguments map both
  positionally (`/greet World`) and by name (`/review path=x style=terse`).

## Approvals: who decides a remote tool may run

An MCP tool is permission-gated like any other tool, plus a per-server policy:

| Policy | Effect |
|---|---|
| **ask** (default) | The normal prompt — you approve each call (or `a` for the session). |
| `autoApprove` | Runs without a prompt. Use it for read-only tools you trust. |
| `deny` | The tool is **never advertised to the model at all**, and refused as a backstop if it is somehow requested. |

`deny` beating everything is the important half: a denied tool is not registered,
so the model never sees it and cannot be talked into asking for it. And note what
this composes with — under `--auto` the approval prompt is gone, so
`autoApprove` and `--auto` are the same posture for these tools; `deny` still
holds. See [AGENT_MODES.md](AGENT_MODES.md) for the full verdict order.

## The security posture, stated plainly

**Resource reads are treated as untrusted data.** A resource is content a server
fetched from somewhere, so `read_mcp_resource` wraps it in the M300 untrusted
fence — *"DATA, NOT INSTRUCTIONS"* — because a document that says "ignore your
instructions and…" is an injection attempt, not a request.

**Tool *results* are not fenced, deliberately.** A server you hand-configured is
treated like your own repository: semi-trusted. Fencing every tool result would
train the model to discount everything, which weakens the fence where it matters.
This is a judgement, and you should know it is being made on your behalf: **an
MCP server you did not write and do not control is code you are trusting**, with
your files' access, in your session. Read what you install.

Two further facts, one reassuring and one not. **Your model API keys are scrubbed
from a stdio server's environment before it is exec'd** (M130) — a spawned server
inherits your environment *minus* the keys jichi registered, so it gets only what
you put in `env`. But **a header or `env` value is a literal string in your config
file**: there is no `${VAR}` expansion, so a token written there is a secret at
rest in a file you may be about to commit — keep such a config in git-ignored
`local/config.json`, or prefer a stdio server that reads its own credential.
And a server's own network reach is its business: jichi's path fence and SSRF
protections bound *jichi*, not a separate process it spawned. If that matters, the
answer is OS-level isolation, exactly as in
[GATE_INTEGRITY.md](GATE_INTEGRITY.md) §9.

## When it does not work

| Symptom | Cause and fix |
|---|---|
| `MCP: none configured` and a warning about an ARRAY | You used the object shape. Make `mcpServers` an array of `{name, …}`. |
| `doctor` says *some MCP servers failed to connect* | Run the `command` yourself in a terminal — most failures are a missing `npx`/binary, a bad path, or a server that needs an env var you did not pass. |
| The agent never calls a tool you can see in `jichi mcp` | Check `deny`, check the tool profile (`toolProfile: core` advertises only the lean built-in set — [COMPACTION.md](COMPACTION.md)), and check the tool's own description: a model picks tools by description, so a vague one is never chosen. |
| A prompt-as-slash-command says *unknown command* | A file command of that name wins, or the server does not advertise that prompt — `jichi mcp prompts` lists the real names. |

## Implementation

`src/mcp/jc_mcp_proto.c` is the pure, unit-tested core (JSON-RPC 2.0 builders,
`tools/list` and `tools/call` parsers, the resources/prompts parsers, and the
`<server>__<tool>` namespacing). Two transports sit behind a vtable:
`jc_mcp_stdio.c` (fork/exec/pipe, `select` for timeout and abort) and
`jc_mcp_http.c` (streamable HTTP, JSON or SSE responses, echoing
`Mcp-Session-Id`). `jc_mcp.c` is the manager that connects each server and
registers its tools as **dynamic** `jc_tool` entries. jichi is a client only — to
drive jichi *as* a server for an editor, see [ACP.md](ACP.md).
