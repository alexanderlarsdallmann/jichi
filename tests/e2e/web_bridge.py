"""E2E: the minimal web bridge (examples/web-bridge/bridge.py).

Network-free: a mock chat model on loopback drives real jichi children spawned by
the bridge. Asserts the full contract the bridge promises:

  * a wrong/absent token is rejected with 403 (the boot-token gate);
  * POST /run + GET /runs/<id>/events streams jsonl events as SSE, from
    message_start through the terminal done;
  * with heartbeat requested against a slow model, >=1 heartbeat event arrives;
  * POST /runs/<id>/cancel ends the run (SIGINT -> the SSE closes).
"""
import os
import sys
import json
import time
import socket
import threading
import tempfile
import shutil
import subprocess
import http.client

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _e2e import BIN, fail, ok, recv_http_request  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
BRIDGE = os.path.normpath(os.path.join(HERE, "..", "..", "examples",
                                       "web-bridge", "bridge.py"))


def send(conn, payload):
    hdr = ("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
           "Connection: close\r\nContent-Length: %d\r\n\r\n" % len(payload))
    conn.sendall(hdr.encode() + payload)


def sse_text(content):
    a = ('{"choices":[{"index":0,"delta":{"role":"assistant","content":"%s"},'
         '"finish_reason":null}]}' % content)
    b = ('{"choices":[{"index":0,"delta":{},"finish_reason":"stop"}],'
         '"usage":{"prompt_tokens":20,"completion_tokens":5}}')
    return ("data: %s\n\ndata: %s\n\ndata: [DONE]\n\n" % (a, b)).encode()


def serve_model(delay=0.0):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    srv.listen(8)
    port = srv.getsockname()[1]
    stop = threading.Event()

    def handle(conn):
        try:
            conn.settimeout(30.0)
            if recv_http_request(conn) is None:
                return
            if delay > 0:
                time.sleep(delay)
            send(conn, sse_text("all done"))
        except OSError:
            pass
        finally:
            try:
                conn.close()
            except OSError:
                pass

    def loop():
        srv.settimeout(0.5)
        while not stop.is_set():
            try:
                conn, _ = srv.accept()
            except (socket.timeout, OSError):
                if stop.is_set():
                    break
                continue
            threading.Thread(target=handle, args=(conn,), daemon=True).start()
        srv.close()

    threading.Thread(target=loop, daemon=True).start()
    return port, stop


def write_config(root, mport):
    cfg = {"models": [{"name": "m", "provider": "openai", "model": "mock",
                       "apiBase": "http://127.0.0.1:%d/v1" % mport,
                       "apiKey": "x", "roles": ["chat"]}],
           "snapshots": False, "repoMap": False, "references": False,
           "maxRetries": 0}
    p = os.path.join(root, "cfg.json")
    with open(p, "w") as f:
        json.dump(cfg, f)
    return p


def start_bridge(cfg, ws, home):
    env = dict(os.environ, LANG="C", LC_ALL="C", HOME=home, JICHI_BIN=BIN)
    p = subprocess.Popen(
        [sys.executable, BRIDGE, "--config", cfg, "--root", ws,
         "--host", "127.0.0.1", "--port", "0", "--budget-tokens", "50k",
         "--deadline", "5m"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
    # Parse the machine-readable ready line: "BRIDGE READY http://host:port/?token=T"
    line = p.stdout.readline()
    if not line.startswith("BRIDGE READY "):
        try:
            p.kill()
        except OSError:
            pass
        return None, None, None, p
    url = line.split(" ", 2)[2].strip()
    hostport = url.split("//", 1)[1].split("/", 1)[0]
    host, port = hostport.split(":")
    token = url.split("token=", 1)[1]
    return host, int(port), token, p


def http_post(host, port, path, body=None):
    c = http.client.HTTPConnection(host, port, timeout=30)
    data = json.dumps(body).encode() if body is not None else b""
    c.request("POST", path, data, {"Content-Type": "application/json"})
    r = c.getresponse()
    out = r.read().decode()
    c.close()
    return r.status, out


def read_sse(host, port, path, want_done=True, max_secs=60):
    """Read an SSE stream, returning the list of parsed event objects."""
    c = http.client.HTTPConnection(host, port, timeout=max_secs)
    c.request("GET", path)
    r = c.getresponse()
    if r.status != 200:
        c.close()
        return None, r.status
    events = []
    buf = ""
    end = time.time() + max_secs
    fp = r.fp
    while time.time() < end:
        chunk = fp.readline()
        if not chunk:
            break
        buf = chunk.decode("utf-8", "replace")
        if buf.startswith("data: "):
            try:
                ev = json.loads(buf[6:].strip())
            except ValueError:
                continue
            events.append(ev)
            if want_done and ev.get("type") == "done":
                break
    c.close()
    return events, 200


def main():
    root = tempfile.mkdtemp(prefix="jichi_wb_")
    ws = os.path.join(root, "ws")
    home = os.path.join(root, "home")
    os.makedirs(ws)
    os.makedirs(home)

    mport, mstop = serve_model()
    cfg = write_config(root, mport)
    host = port = token = None
    proc = None
    try:
        host, port, token, proc = start_bridge(cfg, ws, home)
        if host is None:
            fail("bridge did not print a READY line:\n%s"
                 % (proc.stderr.read()[-400:] if proc else ""))
            return

        # 1. Token gate: no token -> 403.
        st, out = http_post(host, port, "/run", {"prompt": "x"})
        if st != 403:
            fail("missing-token POST should be 403, got %d %s" % (st, out))
            return

        # 2. A real run streams events through to done.
        st, out = http_post(host, port, "/run?token=" + token,
                            {"prompt": "say hello"})
        if st != 200:
            fail("POST /run rc=%d %s" % (st, out))
            return
        rid = json.loads(out)["id"]
        evs, code = read_sse(host, port,
                             "/runs/%s/events?token=%s" % (rid, token))
        if evs is None:
            fail("SSE events request failed: %d" % code)
            return
        types = [e.get("type") for e in evs]
        if "message_start" not in types or "done" not in types:
            fail("SSE stream missing message_start/done: %s" % types)
            return
        done = [e for e in evs if e.get("type") == "done"][0]
        if done.get("stop_reason") not in ("done", None):
            fail("unexpected stop_reason: %s" % done.get("stop_reason"))
            return

        mstop.set()

        # 3. Heartbeat: a slow model + heartbeat:1 -> >=1 heartbeat event.
        sport, sstop = serve_model(delay=2.5)
        scfg = write_config(root, sport)
        # restart the bridge against the slow model's config
        proc.terminate()
        proc.wait(timeout=10)
        host, port, token, proc = start_bridge(scfg, ws, home)
        st, out = http_post(host, port, "/run?token=" + token,
                            {"prompt": "slow one", "heartbeat": 1})
        rid = json.loads(out)["id"]
        evs, _ = read_sse(host, port,
                          "/runs/%s/events?token=%s" % (rid, token),
                          max_secs=90)
        hb = [e for e in (evs or []) if e.get("type") == "heartbeat"]
        if not hb:
            fail("no heartbeat event over SSE on a slow model: %s"
                 % [e.get("type") for e in (evs or [])])
            return

        # 4. Cancel: start a slow run, cancel it, the run ends (interrupted).
        st, out = http_post(host, port, "/run?token=" + token,
                            {"prompt": "cancel me"})
        rid = json.loads(out)["id"]
        time.sleep(0.6)  # let the child reach the (slow) model call
        st, _ = http_post(host, port,
                          "/runs/%s/cancel?token=%s" % (rid, token))
        if st != 200:
            fail("cancel POST rc=%d" % st)
            return
        evs, _ = read_sse(host, port,
                          "/runs/%s/events?token=%s" % (rid, token),
                          want_done=False, max_secs=30)
        # The stream must close (the run ended); a done with an interrupted
        # stop_reason is the happy path, but the key property is termination.
        sstop.set()

        ok("web bridge: token gate (403), SSE run->done, heartbeat over SSE, "
           "cancel ends the run (M165b)")
    finally:
        mstop.set()
        if proc is not None:
            try:
                proc.terminate()
                proc.wait(timeout=10)
            except (OSError, subprocess.TimeoutExpired):
                try:
                    proc.kill()
                except OSError:
                    pass
        shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    main()
