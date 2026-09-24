# SQLite with jichi -- the files for docs/SQLITE.md

| File | What it is |
|---|---|
| `learning.sql` | the practice database: `sqlite3 learning.db < learning.sql` |
| `sqlite_mcp.py` | a read-only SQLite MCP server, one file, Python standard library only |
| `config.mcp.json` | route 1: the `mcpServers` entry for that server |
| `config.tool.json` | route 2: a user-defined tool around `sqlite3 -safe -readonly` |

The walk-through, what each route can and cannot do, and why `-safe` is not optional, is
[`../../docs/SQLITE.md`](../../docs/SQLITE.md). Everything here was run by hand on
2026-09-24 with a real model; there is no smoke driver for it yet (the smoke tier is
Python-free by design, and the server is Python), which `docs/DEFERRED.md` records.
