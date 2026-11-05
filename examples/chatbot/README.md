# A website chatbot with jichi as its engine

A small, runnable example: a chat page, a server in the Python standard library, a
fixed jichi configuration, and a knowledge folder about a fictional library. jichi
answers visitors from those documents, reads but never changes them, and runs on a free
local model.

**Read [`docs/CHATBOT.md`](../../docs/CHATBOT.md) first.** It explains every setting,
shows what was measured, and lists what this example does not do: it has no TLS, no
accounts and no per-visitor rate limits, and must not be exposed as it is.

| File | What it is |
|---|---|
| `server.py` | the chat backend: one `jichi --readonly -p` process per message |
| `index.html` | the chat page |
| `config.json` | the fixed configuration: chat mode, the path fence on, the core tool profile, the deny list |
| `knowledge/` | the documents, the bot's rules (`AGENTS.md`), its persona (`.jichi/agents/helpdesk.md`) and the `/ask` command (`.jichi/commands/ask.md`) |

Quick start, after reading the guide:

1. Copy this folder **outside** the jichi repository, to a place with no `AGENTS.md` or
   `CLAUDE.md` in any folder above it: `cp -r examples/chatbot ~/chatbot-demo`.
2. Start a local model server with a model that calls tools natively (the guide's §4).
   The config expects LM Studio on port 1234 with `qwen/qwen3.5-9b`.
3. `cd ~/chatbot-demo && python3 server.py`, then open `http://127.0.0.1:8080/`.

`tests/smoke/chatbot_example.sh` checks this example's safety claims on every gate run.
