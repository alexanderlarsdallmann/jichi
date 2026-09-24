# SQLite with jichi — three ways to give an agent a database, and what "read-only" really takes

*For a self-learner with a database file and a question about it, and for a junior
developer who has just been told "let the agent use the database". Everything on this
page was run on 2026-09-24 on the development machine: sqlite3 3.46.1, Python's `sqlite3`
module (SQLite 3.46.1), jichi `73403328`, the model `jlu/qwen3-coder-next` on the
institution's gateway. What was not run is listed in [§7](#7-what-this-page-did-not-test).*

---

## 0. The short answer

There are three routes. Choose by **how much you trust the agent with the data**, not by
how much typing each one needs:

| Route | What the agent can do | Set-up | Use it when |
|---|---|---|---|
| **1. An MCP server** — [`examples/sqlite/sqlite_mcp.py`](../examples/sqlite/sqlite_mcp.py) | read, and only read — enforced five separate ways | one file, six lines of config | the data matters, or you want to learn how MCP works |
| **2. A user-defined tool** around `sqlite3 -safe -readonly` | read, *if* you pass `-safe` | fifteen lines of config | a quick question tool for your own file |
| **3. The shell** — `run_terminal_command` with `sqlite3` | anything: read, write, delete | nothing | you *want* the agent to change the database, and you have a backup |

None of them adds anything to jichi. jichi links libcurl and nothing else
([`../CLAUDE.md`](../CLAUDE.md)), so SQLite reaches the agent the way every outside capability
does: through a program jichi starts. That is also why you can apply this page to
PostgreSQL or MySQL with small changes (§6).

---

## 1. The practice database

```sh
# anywhere: a throwaway directory is best
mkdir -p ~/sqlite-practice && cd ~/sqlite-practice
sqlite3 learning.db < /path/to/jichi/examples/sqlite/learning.sql
sqlite3 learning.db "SELECT * FROM lessons;"
```

Two tables — `lessons(id, title, minutes)` and `progress(lesson_id, done_on, score)` — and
five lessons, three of them started. Every answer below can be checked by hand: the best
score is **91**, in *Pointers and arrays*; *Sorting* and *Big-O by measurement* are not
started.

---

## 2. The trap: a read-only database is not a read-only tool

Before any route, the one lesson on this page that is not about jichi. `sqlite3 -readonly`
opens the **file** read-only. It does not make the **program** harmless — the `sqlite3`
shell has commands and SQL functions that reach outside the database, and they work on a
read-only one. Measured:

| What the SQL argument says | `sqlite3 -readonly` | `sqlite3 -safe -readonly` |
|---|---|---|
| `.shell echo hi > file` | **ran it** — the file appeared | refused: *cannot run .shell in safe mode* |
| `SELECT writefile('file', 'hi')` | **wrote the file** | refused: *cannot use the writefile() function in safe mode* |
| `SELECT readfile('/etc/hostname')` | **read it** | refused |
| `ATTACH DATABASE 'other.db' AS o` | failed, but only because the new file could not be created | refused: *cannot run ATTACH in safe mode* |

This matters for jichi specifically. A user tool marked `"readonly": true` runs **without an
approval prompt** and is allowed in plan mode ([`USER_TOOLS.md`](USER_TOOLS.md)). If its
command is `sqlite3 -readonly …`, you have given the model an unprompted shell. `-safe`
(SQLite 3.37 or newer — check with `sqlite3 --version`) closes it.

> **The habit to take away:** when something is called read-only, ask *who* is read-only —
> the file, the connection, or the program. They are three different promises, and only
> the last one is the one a tool needs.

---

## 3. Route 1 — an MCP server

**MCP** (Model Context Protocol) is a small protocol for letting another program offer
tools to an agent ([`MCP.md`](MCP.md)). jichi starts the program, asks it what tools it has,
and offers them to the model as `<server>__<tool>`. The server here is one file of Python
using only the standard library — around 190 lines, written to be read.

### 3.1 Configure it

Add this to your config (keep your own `models` block;
[`examples/sqlite/config.mcp.json`](../examples/sqlite/config.mcp.json) is the same entry):

```json
{
  "mcpServers": [
    {
      "name": "db",
      "command": "python3",
      "args": ["/path/to/jichi/examples/sqlite/sqlite_mcp.py", "learning.db"],
      "autoApprove": ["schema", "query"]
    }
  ]
}
```

`mcpServers` is an **array** — the one thing most people get wrong ([`MCP.md`](MCP.md)).
`autoApprove` is safe here because both tools only read; §3.4 is why you can believe that.

### 3.2 Check it without a model

```sh
# in ~/sqlite-practice
jichi mcp
jichi mcp call db__query '{"sql":"SELECT title FROM lessons WHERE id NOT IN (SELECT lesson_id FROM progress) ORDER BY id"}'
```

What it printed here:

```
Connected 1/1 server(s), 2 tool(s) total, 2 advertised to the model.
  db__schema   [auto] List the tables of the database and their columns. ...
  db__query    [auto] Run ONE read-only SQL statement (SQLite dialect) ...

{"columns": ["title"], "rows": [{"title": "Sorting"}, {"title": "Big-O by measurement"}]}
```

Checking a tool by calling it yourself, before any model is involved, separates "the tool
is broken" from "the model used it badly" — the most useful split on this page.

### 3.3 Ask a question

```sh
# in ~/sqlite-practice
jichi -q --no-session --auto -p "Use the database tools to answer: in which lesson did I get my best score, and which lessons have I not started yet?" < /dev/null
```

The model called `db__schema` once and `db__query` twice, and answered — correctly:

```
- Your best score: "Pointers and arrays" with a score of 91
- Lessons you haven't started yet: "Sorting" and "Big-O by measurement"
```

(`--journal FILE` records every tool call the run made, if you want to see them.)

### 3.4 Why read-only is five things here, not one

Each layer stops something the others would let through. All five were tested:

| Layer | In the code | What it stops | Measured |
|---|---|---|---|
| open the file read-only | `mode=ro` in `open_db` | any write reaching the file | — |
| `PRAGMA query_only` | `open_db` | the connection executing a write at all | — |
| an **authorizer** | `authorizer` | writes, `ATTACH` and pragma changes, statement by statement | `DELETE`, `ATTACH` and `PRAGMA query_only = OFF` each came back *not authorized* |
| a **progress handler** | `run_query` | a query that never ends | an unbounded recursive query was *interrupted* after 5 s |
| a **row cap** | `MAX_ROWS` | a result too big to read | 1,000 rows came back as 200 plus *800 more row(s) not shown* |

And one thing is absent by construction: Python's `sqlite3` module has none of the shell's
dot-commands or file functions, so §2's trap does not exist here.

Every refusal comes back as a **tool result with `isError` set**, not as a crash — the
model reads *not authorized* and can try something else. That is the same rule jichi keeps
for its own tools ([`ARCHITECTURE.md`](ARCHITECTURE.md)): an error is a value.

### 3.5 Reading the server — a route through it

Read it in this order: `main` (the message loop: one JSON object per line in, one out),
`open_db` (three of the five layers), `authorizer`, `run_query`, then `TOOLS` (the
descriptions the model actually reads — a vague description is a tool the model never
chooses). Things to try, each small:

1. Add an `explain` tool that returns `EXPLAIN QUERY PLAN` for a statement. Which
   authorizer action does it need?
2. Make `MAX_ROWS` a command-line argument. What should happen when it is 0?
3. Remove `PRAGMA query_only` and run §3.4's `DELETE` again. Which layer still stops it?
   Then remove the authorizer as well. **Predict first**, then run — on a copy of the file.

---

## 4. Route 2 — a user-defined tool around `sqlite3`

The lightest route: no server, no protocol — jichi runs a command you wrote in the config
and hands it the model's arguments ([`USER_TOOLS.md`](USER_TOOLS.md)).
[`examples/sqlite/config.tool.json`](../examples/sqlite/config.tool.json):

```json
{
  "tools": [
    {
      "name": "sql_query",
      "description": "Run ONE read-only SQLite query against learning.db and return the rows as JSON. Tables: lessons(id, title, minutes), progress(lesson_id, done_on, score). Writes are refused.",
      "schema": {
        "type": "object",
        "properties": { "sql": { "type": "string", "description": "a single SELECT statement" } },
        "required": ["sql"]
      },
      "shell": "python3 -c 'import json, sys; sys.stdout.write(json.load(sys.stdin)[\"sql\"])' | sqlite3 -safe -readonly -json -bail learning.db",
      "readonly": true,
      "timeout": 20
    }
  ]
}
```

Each `sqlite3` flag has a job: `-safe` (§2), `-readonly` (the file), `-json` (rows the model
can read without guessing at columns), `-bail` (stop at the first error, so a failure is
reported rather than half-run). And the SQL travels on **stdin**, which is the second lesson
of this route.

### 4.1 Why the query is read from stdin, and not from `$JICHI_ARG_SQL`

jichi gives a user tool its arguments twice: the whole object as JSON on stdin, and each
scalar as a `JICHI_ARG_<NAME>` environment variable. The variable is the convenient one —
and it is **cut at 1023 bytes without a word** (a fixed buffer in `src/tools/jc_tool_user.c`;
recorded in [`DEFERRED.md`](DEFERRED.md)). For most tools that is a curiosity. For SQL it is
a trap, because a cut query is often still a *valid* query. Measured with a 1,236-byte query:

```
... WHERE id = 1 OR id = 2 OR id = 3 OR ... OR id = 5
```

cut to 1,023 bytes ended in `... OR id = 3 OR id` — which SQLite accepts (`id` on its own is
a condition) and **ran**. It happened to return the same count; a different cut would not
have. So read the query from stdin, where it arrives whole. The `python3 -c` in the config
does only that: it takes the `sql` field out of the JSON and hands it to `sqlite3`.

### 4.2 Proving it with a hostile argument, the way to trust a fence

```sh
# in ~/sqlite-practice
printf '%s' '{"sql": ".shell touch pwned"}' | sh -c 'python3 -c "import json, sys; sys.stdout.write(json.load(sys.stdin)[\"sql\"])" | sqlite3 -safe -readonly -json -bail learning.db'
ls pwned
```

*cannot run .shell in safe mode*, exit 1, and no `pwned`. Now remove `-safe` and run it
again — on a copy — and watch the file appear.

**Asking the same question** as §3.3 ("Use the sql_query tool …"): two `sql_query` calls,
a correct answer, two seconds.

What this route does **not** give you: no row cap of its own (jichi keeps the first 32 KB of a
user tool's output, so tell the model to use `LIMIT`), no per-statement authorizer, and a
guarantee only as good as your `sqlite3`'s `-safe`.

---

## 5. Route 3 — letting the agent write, and taking it back

`run_terminal_command` can run `sqlite3` like any command. In chat mode every command asks
first; under `--auto` it just runs. This is the route for *changing* data — and the question
worth asking before you use it is **how you get the data back**.

jichi checkpoints the workspace before the agent's first file-changing action in a turn
([`SNAPSHOTS.md`](SNAPSHOTS.md)). A database is a file, so it is inside that net. Measured,
in a throwaway copy:

1. Asked to *"use the sqlite3 command in the terminal to delete every row from the progress
   table"*, the model did it: 4 rows became 0.
2. `jichi checkpoints` listed the checkpoint taken before that turn.
3. `jichi undo --dry-run` named exactly one change: `learning.db | Bin 12288 -> 12288 bytes`.
4. `jichi undo` restored it: **4 rows**, and `PRAGMA integrity_check` said `ok`. The
   discarded state was kept for `jichi recover`.

**When the net does not hold**, and each of these is common:

| Condition | Why undo will not save you | Check |
|---|---|---|
| snapshots are off | no checkpoint was taken | `jichi doctor` |
| the database is **git-ignored** (`*.db` is a common pattern) | a checkpoint is `git add -A`, which skips ignored files | `git check-ignore -v learning.db` |
| the database is outside the workspace | only the workspace is checkpointed | where is the file? |
| **WAL mode** (`learning.db-wal`, `-shm` beside it) | the file and its log can be restored out of step | `sqlite3 learning.db 'PRAGMA journal_mode'` |
| another program has it open | undo rewrites the file under that program | close it first |

So take the belt as well as the braces — before any session in which the agent may write:

```sh
# in ~/sqlite-practice
sqlite3 learning.db ".backup learning.backup.db"
```

`.backup` makes a consistent copy even of a database that is in use.

---

## 6. Other databases

The shape carries over; the specific protections do not:

- **PostgreSQL / MySQL** have no `-safe`. The equivalent of §3.4's layers is the database
  server's own permission system: connect as a role that has only `SELECT` on the tables the
  agent may see. That is stronger than anything a wrapper can do, because the server enforces
  it — prefer it over parsing SQL yourself.
- **Credentials** belong in the environment the tool is started with, never in a config
  file you might commit: an MCP entry's `env` and `headers` are literal strings in the file
  ([`MCP.md`](MCP.md)).

---

## 7. What this page did not test

- A local model. Only `jlu/qwen3-coder-next` was driven. A model that does not make native
  tool calls will *describe* a query instead of running one; `jichi doctor --live` says which
  kind yours is ([`LOCAL_MODELS.md`](LOCAL_MODELS.md)).
- `sqlite3` older than 3.37, which has no `-safe`.
- Undo on a database in WAL mode, or one another program holds open.
- Several agents on one database at once.
- Windows. The `shell` string assumes a POSIX shell.
- There is no automated test of `examples/sqlite/` yet — the smoke tier is deliberately
  Python-free and the server is Python ([`DEFERRED.md`](DEFERRED.md)).

## Where to go next

- [`MCP.md`](MCP.md) — the client: approvals, `deny`, what jichi trusts a server with.
- [`USER_TOOLS.md`](USER_TOOLS.md) — every field of a user-defined tool.
- [`SNAPSHOTS.md`](SNAPSHOTS.md) — checkpoints, `undo`, and when the net is not armed.
- [`AGENT_MODES.md`](AGENT_MODES.md) — chat, plan and auto, and which tools each allows.
