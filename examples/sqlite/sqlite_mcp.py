#!/usr/bin/env python3
"""sqlite_mcp.py -- a read-only SQLite MCP server in one file, standard library only.

A teaching example for docs/SQLITE.md: it speaks just enough of the Model Context
Protocol (JSON-RPC 2.0, one JSON object per line on stdin/stdout) for jichi to list
its tools and call them. Run it by naming it in jichi's config:

    "mcpServers": [
      { "name": "db", "command": "python3",
        "args": ["examples/mcp-sqlite/sqlite_mcp.py", "learning.db"],
        "autoApprove": ["schema", "query"] }
    ]

The model then sees two tools, db__schema and db__query.

WHY READ-ONLY IS FIVE THINGS HERE, NOT ONE. Each layer stops something the others
would let through, and the guide shows what a single layer misses:
  1. the file is opened read-only (mode=ro): no write reaches the file;
  2. PRAGMA query_only: the connection refuses to execute any write at all;
  3. an authorizer: ATTACH (opening a second file) and anything that is not a read
     are denied, statement by statement;
  4. a progress handler: a query that runs too long is interrupted;
  5. a row cap: a huge result is cut, and the cut is said out loud.
Python's sqlite3 module has none of the sqlite3 *shell's* dot-commands or file
functions (.shell, readfile, writefile), which is the other half of why this is a
module and not a wrapper around the shell.
"""
import json
import sqlite3
import sys
import time

MAX_ROWS = 200          # rows returned per query; more are counted and reported
MAX_SECONDS = 5.0       # wall-clock budget per query
PROTOCOL = "2025-06-18"  # what jichi sends; echoed back if the client asks for another

# The authorizer's action codes for reading. SQLITE_RECURSIVE (a WITH RECURSIVE query)
# is 33; older Pythons do not name it.
ALLOWED = {
    sqlite3.SQLITE_SELECT, sqlite3.SQLITE_READ, sqlite3.SQLITE_FUNCTION,
    getattr(sqlite3, "SQLITE_RECURSIVE", 33),
}
# Pragmas that only describe the schema. Their argument is a table or index NAME,
# not a value to set, so they are allowed with one.
SCHEMA_PRAGMAS = {"table_info", "table_xinfo", "table_list", "index_list",
                  "index_info", "foreign_key_list"}


def authorizer(action, arg1, arg2, dbname, source):
    """Allow reads, deny everything else -- including ATTACH, which opens a file."""
    if action == sqlite3.SQLITE_PRAGMA:
        if arg1 in SCHEMA_PRAGMAS:
            return sqlite3.SQLITE_OK
        # Reading query_only's value is fine; setting it (arg2 = "OFF") is not.
        return sqlite3.SQLITE_OK if arg1 == "query_only" and arg2 is None \
            else sqlite3.SQLITE_DENY
    return sqlite3.SQLITE_OK if action in ALLOWED else sqlite3.SQLITE_DENY


def open_db(path):
    con = sqlite3.connect("file:%s?mode=ro" % path, uri=True)
    con.execute("PRAGMA query_only = ON")
    con.set_authorizer(authorizer)
    return con


def run_query(con, sql):
    start = time.monotonic()
    con.set_progress_handler(
        lambda: 1 if time.monotonic() - start > MAX_SECONDS else 0, 10000)
    try:
        cur = con.execute(sql)
        cols = [d[0] for d in cur.description or []]
        rows, extra = [], 0
        for row in cur:
            if len(rows) < MAX_ROWS:
                rows.append(dict(zip(cols, row)))
            else:
                extra += 1
        out = {"columns": cols, "rows": rows}
        if extra:
            out["truncated"] = "%d more row(s) not shown -- add a LIMIT or aggregate" % extra
        return json.dumps(out, default=str), False
    except sqlite3.Error as e:
        # An error is a VALUE the model reads and can act on, not a crash.
        return "error: %s" % e, True
    finally:
        con.set_progress_handler(None, 0)


def schema(con):
    lines = []
    for (name,) in con.execute(
            "SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name"):
        ident = '"' + name.replace('"', '""') + '"'   # SQL identifier quoting
        cols = ", ".join("%s %s" % (c[1], c[2]) for c in
                         con.execute("PRAGMA table_info(%s)" % ident))
        lines.append("%s(%s)" % (name, cols))
    return "\n".join(lines) or "(no tables)", False


TOOLS = [
    {"name": "schema",
     "description": "List the tables of the database and their columns. Call this "
                    "first, before writing a query.",
     "inputSchema": {"type": "object", "properties": {}}},
    {"name": "query",
     "description": "Run ONE read-only SQL statement (SQLite dialect) and return the "
                    "rows as JSON. Writes, ATTACH and schema changes are refused. At "
                    "most %d rows come back; use LIMIT or aggregates." % MAX_ROWS,
     "inputSchema": {"type": "object",
                     "properties": {"sql": {"type": "string",
                                            "description": "a single SELECT statement"}},
                     "required": ["sql"]}},
]


def reply(msg_id, result=None, error=None):
    msg = {"jsonrpc": "2.0", "id": msg_id}
    if error is not None:
        msg["error"] = error
    else:
        msg["result"] = result
    sys.stdout.write(json.dumps(msg) + "\n")
    sys.stdout.flush()


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: sqlite_mcp.py DATABASE\n")
        return 2
    con = open_db(sys.argv[1])
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        method, msg_id = msg.get("method"), msg.get("id")
        params = msg.get("params") or {}
        if msg_id is None:          # a notification (e.g. notifications/initialized)
            continue
        if method == "initialize":
            reply(msg_id, {"protocolVersion": params.get("protocolVersion", PROTOCOL),
                           "capabilities": {"tools": {}},
                           "serverInfo": {"name": "sqlite-readonly", "version": "1"}})
        elif method == "tools/list":
            reply(msg_id, {"tools": TOOLS})
        elif method == "tools/call":
            name = params.get("name")
            args = params.get("arguments") or {}
            if name == "schema":
                text, is_error = schema(con)
            elif name == "query":
                text, is_error = run_query(con, str(args.get("sql", "")))
            else:
                text, is_error = "error: no tool named %r" % name, True
            reply(msg_id, {"content": [{"type": "text", "text": text}],
                           "isError": is_error})
        elif method in ("resources/list", "prompts/list"):
            reply(msg_id, {method.split("/")[0]: []})
        elif method == "ping":
            reply(msg_id, {})
        else:
            reply(msg_id, error={"code": -32601, "message": "method not found: %s" % method})
    return 0


if __name__ == "__main__":
    sys.exit(main())
