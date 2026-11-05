#!/usr/bin/env python3
"""A minimal website chatbot with jichi as its engine (Python standard library only).

Read docs/CHATBOT.md first: it explains every choice below, and what this example
does NOT do (TLS, accounts, rate limiting per visitor) and must not be exposed
without.

How it works. GET / serves index.html. POST /api/chat takes
{"conversation": <token or null>, "message": "..."} and runs ONE headless jichi
turn for it:

    jichi --config config.json --readonly --output json -q --deadline 120s
          --max-tool-calls 12 --budget-tokens 60k --lease off
          [--session <id>] --prompt-b64 <"/ask " + the message, base64>

in the knowledge folder, with HOME pointing at this example's own state folder,
and answers {"conversation": <token>, "reply": "...", ...}. `/ask` is the
knowledge folder's own command (.jichi/commands/ask.md): it runs the turn as the
`helpdesk` profile (.jichi/agents/helpdesk.md), whose text replaces jichi's
coding-agent persona. The server always writes the command name itself, so a
visitor's text is only ever its argument. The first turn of a
conversation creates a jichi session; later turns continue it with --session.
A turn that ends with no answer text -- measured with small local models, which
sometimes end a turn after their tool calls without writing anything -- is
retried once in the same session with a short request to answer.

Environment (all optional):
    CHATBOT_PORT       port to listen on, default 8080 (always on 127.0.0.1)
    CHATBOT_WORKSPACE  the knowledge folder, default ./knowledge
    CHATBOT_CONFIG     the jichi config, default ./config.json
    CHATBOT_HOME       HOME for the jichi processes, default ./state
    JICHI              the jichi binary, default "jichi" on PATH
"""

import base64
import http.server
import json
import os
import secrets
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = int(os.environ.get("CHATBOT_PORT", "8080"))
WORKSPACE = os.path.abspath(os.environ.get("CHATBOT_WORKSPACE", os.path.join(HERE, "knowledge")))
CONFIG = os.path.abspath(os.environ.get("CHATBOT_CONFIG", os.path.join(HERE, "config.json")))
STATE_HOME = os.path.abspath(os.environ.get("CHATBOT_HOME", os.path.join(HERE, "state")))
JICHI = os.environ.get("JICHI", "jichi")

MAX_MESSAGE_CHARS = 2000   # a question, not a document
TURN_TIMEOUT_SECS = 180    # the outer bound; jichi's --deadline is the inner one
MAX_CONCURRENT_TURNS = 2   # a local model answers one request at a time anyway
NUDGE = ("You ended your last turn without an answer. Answer my previous question "
         "now, in a few sentences, from what you found.")

# The server, not the visitor, knows which jichi session belongs to a conversation:
# a visitor holds only a random token, so it cannot name someone else's session.
_sessions = {}             # token -> jichi session id
_turn_locks = {}           # token -> lock: one turn at a time per conversation
_guard = threading.Lock()
_slots = threading.BoundedSemaphore(MAX_CONCURRENT_TURNS)


def _lock_for(token):
    with _guard:
        if token not in _turn_locks:
            _turn_locks[token] = threading.Lock()
        return _turn_locks[token]


def run_turn(message, session_id):
    """One jichi turn. Returns (reply, session_id, stop_reason, exit_code)."""
    argv = [JICHI, "--config", CONFIG, "--readonly", "--output", "json", "-q",
            "--deadline", "120s", "--max-tool-calls", "12", "--budget-tokens", "60k",
            "--lease", "off"]
    if session_id:
        argv += ["--session", session_id]
    # --prompt-b64: the visitor's text cannot be read as a flag, whatever it says.
    prompt = "/ask " + message
    argv += ["--prompt-b64", base64.b64encode(prompt.encode("utf-8")).decode("ascii")]
    env = dict(os.environ, HOME=STATE_HOME)
    try:
        proc = subprocess.run(argv, cwd=WORKSPACE, env=env, stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=TURN_TIMEOUT_SECS)
    except subprocess.TimeoutExpired:
        return None, session_id, "timeout", None
    try:
        result = json.loads(proc.stdout.decode("utf-8", "replace") or "{}")
    except ValueError:
        result = {}
    return (result.get("text"), result.get("session_id") or session_id,
            result.get("stop_reason"), proc.returncode)


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "jichi-chatbot-example"

    def _send(self, code, obj=None, body=None, ctype="application/json"):
        data = body if body is not None else json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype + "; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path not in ("/", "/index.html"):
            self._send(404, {"error": "not found"})
            return
        with open(os.path.join(HERE, "index.html"), "rb") as f:
            self._send(200, body=f.read(), ctype="text/html")

    def do_POST(self):
        if self.path != "/api/chat":
            self._send(404, {"error": "not found"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = 0
        if length <= 0 or length > 4 * MAX_MESSAGE_CHARS + 1024:
            self._send(400, {"error": "a JSON body of reasonable size is required"})
            return
        try:
            req = json.loads(self.rfile.read(length).decode("utf-8"))
        except ValueError:
            self._send(400, {"error": "the body is not JSON"})
            return
        message = str(req.get("message", "")).strip()
        token = req.get("conversation")
        if not message or len(message) > MAX_MESSAGE_CHARS:
            self._send(400, {"error": "message must be 1 to %d characters" % MAX_MESSAGE_CHARS})
            return
        with _guard:
            if not isinstance(token, str) or token not in _sessions:
                token = secrets.token_urlsafe(18)   # a new conversation
                _sessions[token] = None
        if not _slots.acquire(blocking=False):
            self._send(503, {"error": "busy, try again in a moment", "conversation": token})
            return
        try:
            with _lock_for(token):
                t0 = time.time()
                reply, session_id, stop, code = run_turn(message, _sessions.get(token))
                if not reply and session_id and stop == "done":
                    reply, session_id, stop, code = run_turn(NUDGE, session_id)
                with _guard:
                    _sessions[token] = session_id
                secs = round(time.time() - t0, 1)
        finally:
            _slots.release()
        sys.stderr.write("chat: %s stop=%s exit=%s %.1fs\n" % (token[:6], stop, code, secs))
        if not reply:
            self._send(502, {"conversation": token, "stop_reason": stop,
                             "error": "the assistant could not answer (stop: %s, exit %s)" % (stop, code)})
            return
        self._send(200, {"conversation": token, "reply": reply, "stop_reason": stop, "seconds": secs})

    def log_message(self, fmt, *args):
        pass   # one line per turn is written above; no request bodies in the log


def ancestor_rules(workspace):
    """The rules files jichi would load from the folders ABOVE the workspace.

    jichi reads AGENTS.md (or CLAUDE.md, as a fallback) in every folder from the
    working directory up to the nearest folder that contains .git, or up to / when
    there is none (docs/RULES.md). Anything found above the knowledge folder would
    reach the bot's system prompt, so the server refuses to start instead."""
    found = []
    if os.path.exists(os.path.join(workspace, ".git")):
        return found
    d = os.path.dirname(workspace)
    while True:
        for name in ("AGENTS.md", "CLAUDE.md"):
            if os.path.isfile(os.path.join(d, name)):
                found.append(os.path.join(d, name))
                break
        if os.path.exists(os.path.join(d, ".git")) or os.path.dirname(d) == d:
            return found
        d = os.path.dirname(d)


def main():
    if not os.path.isfile(CONFIG):
        sys.exit("chatbot: no config at %s" % CONFIG)
    rules = ancestor_rules(WORKSPACE)
    if rules:
        sys.exit("chatbot: jichi would load these rules into the bot's system prompt:\n  %s\n"
                 "  Move the knowledge folder where no folder above it has an AGENTS.md or\n"
                 "  CLAUDE.md, or make it its own git repository (git init %s), which\n"
                 "  stops the search there." % ("\n  ".join(rules), WORKSPACE))
    os.makedirs(STATE_HOME, exist_ok=True)
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    sys.stderr.write("chatbot: http://127.0.0.1:%d/  (workspace %s)\n" % (PORT, WORKSPACE))
    httpd.serve_forever()


if __name__ == "__main__":
    main()
