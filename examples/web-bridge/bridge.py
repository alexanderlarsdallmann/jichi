#!/usr/bin/env python3
"""jichi web bridge -- the single-file "watch a long run" track.

This is the minimal alternative to the Phoenix sidecar described in
docs/proposals/2026-07-web-frontend.md: ~1 file of Python **stdlib only**, no
framework, no dependency. It owns jichi processes as children, drives
them over the already-stable machine contract (`-p ... --output jsonl`), and
streams each jsonl event to the browser as Server-Sent Events. It can watch an
autonomous run, show its cost, and cancel it -- and nothing more, by design.

What it deliberately does NOT do (the graduation line to the Phoenix design):
interactive tool approvals (headless has no ask channel -- autonomous runs
only), session resume/attach, multi-instance leases/queues beyond one mutex per
workspace, auth beyond a localhost bind + boot token, telemetry dashboards. The
moment you want approvals in the browser you are re-implementing the ACP
SessionServer state machine here -- that is the sign to graduate to Phoenix.

Security posture (this is remote code execution by design -- a click runs an
agent that runs shell as your Unix user):
  * binds 127.0.0.1 by default; a non-local bind is refused unless you pass
    --allow-remote AND set a token (the sanctioned remote path is an SSH tunnel);
  * a boot token (the Jupyter model) is printed once and required on every
    request; a wrong/absent token gets 403;
  * the config travels to each child on stdin (--config-stdin), so API keys stay
    as apiKeyEnv *names* in the file and never touch argv / /proc/*/cmdline
    (the M129 transport design);
  * every run is envelope-bounded (--auto with a token budget + deadline);
  * one mutex per workspace is the entire governance model.

Run:  JICHI_API_KEY=... python3 bridge.py --config config.example.json
"""
import argparse
import hmac
import json
import os
import secrets
import signal
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

RUNS = {}
RUNS_LOCK = threading.Lock()
_WS_LOCKS = {}
_WS_GUARD = threading.Lock()


def workspace_lock(ws):
    """One mutex per workspace -- the whole governance model for this track."""
    with _WS_GUARD:
        lk = _WS_LOCKS.get(ws)
        if lk is None:
            lk = threading.Lock()
            _WS_LOCKS[ws] = lk
        return lk


class Run(object):
    def __init__(self, rid, prompt, workspace):
        self.id = rid
        self.prompt = prompt
        self.workspace = workspace
        self.proc = None
        self.lines = []            # captured jsonl event lines (raw str)
        self.cond = threading.Condition()
        self.done = False
        self.exit_code = None
        self.stop_reason = None
        self.started = time.time()


class Bridge(object):
    def __init__(self, binary, config_bytes, roots, defaults, log_dir):
        self.binary = binary
        self.config_bytes = config_bytes
        self.roots = [os.path.realpath(r) for r in roots]
        self.defaults = defaults          # {budget_tokens, deadline, max_tool_calls}
        self.log_dir = log_dir

    def workspace_ok(self, ws):
        real = os.path.realpath(ws)
        if not os.path.isdir(real):
            return None
        for root in self.roots:
            if real == root or real.startswith(root + os.sep):
                return real
        return None

    def spawn(self, run, opts):
        argv = [self.binary, "-p", run.prompt, "--output", "jsonl",
                "--auto", "--no-session"]
        budget = opts.get("budget_tokens", self.defaults["budget_tokens"])
        deadline = opts.get("deadline", self.defaults["deadline"])
        max_calls = opts.get("max_tool_calls", self.defaults["max_tool_calls"])
        if budget:
            argv += ["--budget-tokens", str(budget)]
        if deadline:
            argv += ["--deadline", str(deadline)]
        if max_calls:
            argv += ["--max-tool-calls", str(max_calls)]
        hb = opts.get("heartbeat", 0)
        if hb:
            argv += ["--heartbeat", str(int(hb))]
        if self.config_bytes is not None:
            argv += ["--config-stdin"]

        errf = subprocess.DEVNULL
        if self.log_dir:
            errf = open(os.path.join(self.log_dir, run.id + ".stderr"), "wb")
        # start_new_session: its own process group, so cancel can sweep the
        # whole tree (background children, parallel worktrees) with one signal.
        run.proc = subprocess.Popen(
            argv, cwd=run.workspace, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=errf, bufsize=1,
            universal_newlines=True, start_new_session=True,
            env=dict(os.environ))
        if self.config_bytes is not None:
            try:
                run.proc.stdin.write(self.config_bytes)
            except (BrokenPipeError, ValueError):
                pass
        try:
            run.proc.stdin.close()
        except (BrokenPipeError, ValueError):
            pass
        threading.Thread(target=self._pump, args=(run, errf),
                         daemon=True).start()

    def _pump(self, run, errf):
        try:
            for line in run.proc.stdout:
                line = line.rstrip("\n")
                if not line:
                    continue
                with run.cond:
                    run.lines.append(line)
                    try:
                        ev = json.loads(line)
                        if ev.get("type") == "done":
                            run.stop_reason = ev.get("stop_reason")
                    except ValueError:
                        pass
                    run.cond.notify_all()
        finally:
            code = run.proc.wait()
            with run.cond:
                run.exit_code = code
                run.done = True
                run.cond.notify_all()
            if errf not in (None, subprocess.DEVNULL):
                try:
                    errf.close()
                except OSError:
                    pass

    def cancel(self, run):
        """Graceful: SIGINT to the process group (exit 130)."""
        if run.proc and run.proc.poll() is None:
            try:
                os.killpg(os.getpgid(run.proc.pid), signal.SIGINT)
            except (ProcessLookupError, PermissionError):
                pass


INDEX_HTML = """<!doctype html><html><head><meta charset=utf-8>
<title>jichi bridge</title><style>
body{font:14px/1.5 system-ui,sans-serif;margin:2rem;max-width:52rem}
textarea{width:100%;height:5rem;font:inherit}
#log{white-space:pre-wrap;background:#111;color:#ddd;padding:1rem;border-radius:6px;min-height:8rem}
.tool{color:#7cf}.hb{color:#888}.done{color:#8f8}.err{color:#f88}
button{font:inherit;padding:.3rem .8rem}</style></head><body>
<h2>jichi bridge <small style=color:#888>(autonomous runs; watch + cancel only)</small></h2>
<textarea id=p placeholder="Task for an autonomous run..."></textarea>
<p><button id=go>Run</button> <button id=stop disabled>Cancel</button>
<span id=st style=color:#888></span></p>
<div id=log></div>
<script>
var tok=new URLSearchParams(location.search).get("token")||"";
var log=document.getElementById("log"),cur=null,es=null;
function add(cls,txt){var s=document.createElement("span");if(cls)s.className=cls;
  s.textContent=txt;log.appendChild(s);log.scrollTop=log.scrollHeight;}
function q(u){return u+(u.indexOf("?")<0?"?":"&")+"token="+encodeURIComponent(tok);}
document.getElementById("go").onclick=function(){
  var prompt=document.getElementById("p").value;if(!prompt)return;
  log.textContent="";document.getElementById("st").textContent="starting...";
  fetch(q("/run"),{method:"POST",headers:{"Content-Type":"application/json"},
    body:JSON.stringify({prompt:prompt,heartbeat:5})})
   .then(function(r){return r.json();}).then(function(j){
     if(j.error){add("err",j.error);return;}
     cur=j.id;document.getElementById("stop").disabled=false;
     document.getElementById("st").textContent="run "+cur;
     es=new EventSource(q("/runs/"+cur+"/events"));
     es.onmessage=function(e){var ev=JSON.parse(e.data);
       if(ev.type==="text")add("",ev.text);
       else if(ev.type==="tool_call")add("tool","\\n[tool] "+ev.name+" "+(JSON.stringify(ev.args||{}))+"\\n");
       else if(ev.type==="tool_result")add("tool","[result "+(ev.is_error?"ERR":"ok")+"]\\n");
       else if(ev.type==="heartbeat")add("hb","\\u00b7");
       else if(ev.type==="done"){add("done","\\n[done: "+(ev.stop_reason||"")+"  $"+(ev.cost||0)+"]\\n");
         es.close();document.getElementById("stop").disabled=true;}};
     es.onerror=function(){if(es)es.close();};});};
document.getElementById("stop").onclick=function(){if(cur)fetch(q("/runs/"+cur+"/cancel"),{method:"POST"});};
</script></body></html>"""


def make_handler(bridge, token):
    class H(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *a):
            pass  # quiet; the caller's access log is the audit surface

        def _authed(self):
            q = parse_qs(urlparse(self.path).query)
            got = (q.get("token", [""])[0] or
                   self.headers.get("X-Bridge-Token", ""))
            if hmac.compare_digest(got, token):
                return True
            self._json(403, {"error": "bad or missing token"})
            return False

        def _json(self, code, obj):
            body = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _body(self):
            n = int(self.headers.get("Content-Length", "0") or "0")
            if n <= 0:
                return {}
            try:
                return json.loads(self.rfile.read(n).decode())
            except ValueError:
                return {}

        def do_GET(self):
            path = urlparse(self.path).path
            if not self._authed():
                return
            if path == "/":
                body = INDEX_HTML.encode()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            if path == "/runs":
                with RUNS_LOCK:
                    rows = [{"id": r.id, "workspace": r.workspace,
                             "done": r.done, "exit_code": r.exit_code,
                             "stop_reason": r.stop_reason}
                            for r in RUNS.values()]
                self._json(200, {"v": 1, "runs": rows})
                return
            if path.startswith("/runs/") and path.endswith("/events"):
                rid = path[len("/runs/"):-len("/events")]
                self._stream(rid)
                return
            self._json(404, {"error": "not found"})

        def do_POST(self):
            path = urlparse(self.path).path
            if not self._authed():
                return
            if path == "/run":
                self._start(self._body())
                return
            if path.startswith("/runs/") and path.endswith("/cancel"):
                rid = path[len("/runs/"):-len("/cancel")]
                with RUNS_LOCK:
                    run = RUNS.get(rid)
                if run is None:
                    self._json(404, {"error": "no such run"})
                else:
                    bridge.cancel(run)
                    self._json(200, {"ok": True})
                return
            self._json(404, {"error": "not found"})

        def _start(self, opts):
            prompt = (opts.get("prompt") or "").strip()
            if not prompt:
                self._json(400, {"error": "prompt required"})
                return
            ws = bridge.workspace_ok(opts.get("workspace") or bridge.roots[0])
            if ws is None:
                self._json(403, {"error": "workspace not in an allowed root"})
                return
            rid = secrets.token_hex(8)
            run = Run(rid, prompt, ws)
            with RUNS_LOCK:
                RUNS[rid] = run

            def launch():
                lk = workspace_lock(ws)
                with lk:  # serialize mutating runs per workspace
                    bridge.spawn(run, opts)
                    while True:
                        with run.cond:
                            if run.done:
                                break
                            run.cond.wait(1.0)
            threading.Thread(target=launch, daemon=True).start()
            self._json(200, {"id": rid, "workspace": ws})

        def _stream(self, rid):
            with RUNS_LOCK:
                run = RUNS.get(rid)
            if run is None:
                self._json(404, {"error": "no such run"})
                return
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "close")
            self.end_headers()
            idx = 0
            try:
                while True:
                    batch = []
                    with run.cond:
                        while idx < len(run.lines):
                            batch.append(run.lines[idx])
                            idx += 1
                        finished = run.done and idx >= len(run.lines)
                        if not batch and not finished:
                            run.cond.wait(1.0)
                    for line in batch:
                        self.wfile.write(
                            ("data: " + line + "\n\n").encode())
                        self.wfile.flush()
                    if finished:
                        break
            except (BrokenPipeError, ConnectionResetError):
                pass  # client navigated away; the run keeps going
    return H


def main():
    ap = argparse.ArgumentParser(description="jichi web bridge")
    ap.add_argument("--binary", default=os.environ.get("JICHI_BIN",
                    "jichi"), help="path to jichi")
    ap.add_argument("--config", default=os.environ.get("JICHI_BRIDGE_CONFIG"),
                    help="config JSON forwarded to each child on stdin")
    ap.add_argument("--root", action="append", default=[],
                    help="allowed workspace root (repeatable; default: cwd)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--token", default=os.environ.get("JICHI_BRIDGE_TOKEN"))
    ap.add_argument("--allow-remote", action="store_true",
                    help="permit a non-127.0.0.1 bind (requires --token)")
    ap.add_argument("--budget-tokens", default="200k")
    ap.add_argument("--deadline", default="30m")
    ap.add_argument("--max-tool-calls", type=int, default=200)
    ap.add_argument("--log-dir", default=None,
                    help="write per-run child stderr here (crash forensics)")
    args = ap.parse_args()

    if args.host not in ("127.0.0.1", "localhost", "::1") \
            and not args.allow_remote:
        sys.stderr.write("refusing non-local bind without --allow-remote\n")
        return 2
    token = args.token or secrets.token_urlsafe(24)
    if args.host not in ("127.0.0.1", "localhost", "::1") and not args.token:
        sys.stderr.write("a non-local bind requires an explicit --token\n")
        return 2

    config_bytes = None
    if args.config:
        with open(args.config, "r") as f:
            config_bytes = f.read()
    roots = args.root or [os.getcwd()]
    if args.log_dir:
        os.makedirs(args.log_dir, exist_ok=True)

    bridge = Bridge(args.binary, config_bytes, roots,
                    {"budget_tokens": args.budget_tokens,
                     "deadline": args.deadline,
                     "max_tool_calls": args.max_tool_calls},
                    args.log_dir)
    httpd = ThreadingHTTPServer((args.host, args.port),
                                make_handler(bridge, token))
    port = httpd.server_address[1]
    url = "http://%s:%d/?token=%s" % (args.host, port, token)
    # A machine-readable ready line (an automation / e2e parses this) + the
    # human URL. The token is a capability -- treat this line like a password.
    sys.stdout.write("BRIDGE READY %s\n" % url)
    sys.stdout.flush()
    sys.stderr.write("jichi bridge on %s (roots: %s)\nOpen: %s\n"
                     % (httpd.server_address[0] + ":" + str(port),
                        ", ".join(roots), url))
    sys.stderr.flush()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
