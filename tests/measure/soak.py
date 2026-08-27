#!/usr/bin/env python3
"""Long-run memory soak for jichi (M180). A measurement, never a CI gate
(the tests/bench precedent).

Drives ONE jichi process through N turns over ACP (machine protocol, no
PTY) against a local mock OpenAI SSE server. Each turn is two model calls
(a read_file tool call, then a text answer), so the loop exercises the
per-turn allocation sites: tool-call copies, history append, snapshots,
session save. After every turn the driver samples the server process's
VmRSS from /proc/<pid>/status and writes a CSV row; at the end it prints
first/last/peak RSS and the per-turn slope, and cross-checks the curve
against jichi's own M180 telemetry (`turn_end` rss_kb).

Usage:
  JC_SOAK_BIN=/path/to/jichi python3 tests/measure/soak.py \
      [--turns 150] [--csv out.csv] [--keep]

Environment: JC_SOAK_BIN (or JC_E2E_BIN) names the binary.
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

BIN = os.environ.get("JC_SOAK_BIN") or os.environ.get("JC_E2E_BIN")


# ---------------------------------------------------------------- mock model
def _read_request(conn):
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = conn.recv(65536)
        if not chunk:
            return None, b""
        data += chunk
    head, _, rest = data.partition(b"\r\n\r\n")
    clen = 0
    for line in head.split(b"\r\n"):
        if line.lower().startswith(b"content-length:"):
            clen = int(line.split(b":")[1].strip())
    body = rest
    while len(body) < clen:
        chunk = conn.recv(65536)
        if not chunk:
            break
        body += chunk
    return head, body


def _send(conn, payload):
    hdr = ("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
           "Connection: close\r\nContent-Length: %d\r\n\r\n" % len(payload))
    conn.sendall(hdr.encode() + payload)


def _sse_text(conn, content):
    chunks = [
        '{"id":"1","object":"chat.completion.chunk","choices":[{"index":0,'
        '"delta":{"role":"assistant","content":"%s"},"finish_reason":null}]}'
        % content,
        '{"id":"1","object":"chat.completion.chunk","choices":[{"index":0,'
        '"delta":{},"finish_reason":"stop"}],'
        '"usage":{"prompt_tokens":50,"completion_tokens":8}}',
    ]
    _send(conn, ("".join("data: %s\n\n" % c for c in chunks) +
                 "data: [DONE]\n\n").encode())


_READS_PER_TURN = int(os.environ.get("SOAK_READS_PER_TURN", "40"))


def _send_error(conn):
    payload = b'{"error":{"message":"soak: injected transient failure"}}'
    hdr = ("HTTP/1.1 500 Internal Server Error\r\n"
           "Content-Type: application/json\r\n"
           "Connection: close\r\nContent-Length: %d\r\n\r\n" % len(payload))
    conn.sendall(hdr.encode() + payload)


def _sse_tool(conn, name, args_json):
    args = json.dumps(args_json).replace("\\", "\\\\").replace('"', '\\"')
    chunk = (
        '{"id":"1","object":"chat.completion.chunk","choices":[{"index":0,'
        '"delta":{"role":"assistant","tool_calls":[{"index":0,"id":"c1",'
        '"type":"function","function":{"name":"%s",'
        '"arguments":"%s"}}]},'
        '"finish_reason":"tool_calls"}]}' % (name, args))
    _send(conn, ("data: %s\n\ndata: [DONE]\n\n" % chunk).encode())


def serve_mock(port_box, stop, profile="read", fails_per_call=0,
               args_bytes=2048):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    srv.listen(64)
    port_box.append(srv.getsockname()[1])
    # retry profile: fail the next `fails_left` attempts with HTTP 500, then
    # serve one success and re-arm. jichi rebuilds the whole request body per
    # attempt (M20e), so every injected failure is one extra full-size
    # build+free cycle -- the glibc high-water vector this profile measures.
    state = {"fails_left": 0}
    lock = threading.Lock()

    def handle(conn):
        try:
            conn.settimeout(10.0)
            head, body = _read_request(conn)
            if head is None:
                return
            if fails_per_call > 0:
                with lock:
                    if state["fails_left"] > 0:
                        state["fails_left"] -= 1
                        _send_error(conn)
                        return
                    state["fails_left"] = fails_per_call
            txt = body.decode("utf-8", "replace")
            # The LAST message decides: after a tool result, answer in text;
            # otherwise ask for the read_file tool. Two model calls per turn.
            last_tool = txt.rfind('"role":"tool"')
            last_user = txt.rfind('"role":"user"')
            if profile == "reads":
                # M199: many read_file calls in ONE turn, to measure the
                # INTRA-turn peak rather than the per-turn slope. Per-turn
                # scratch cannot bound this -- a turn is up to maxToolIters
                # calls -- which is why the per-tool-call arena exists.
                ntools = txt.count('"role":"tool"')
                if ntools >= _READS_PER_TURN:
                    _sse_text(conn, "noted.")
                else:
                    _sse_tool(conn, "read_file", {"path": "note.txt"})
            elif profile == "save":
                # Text-only turns: the driver fattens the history via the
                # prompt (--history-bytes per turn), so what this measures is
                # the RSS cost of the growing history plus jc_session_save's
                # full re-serialization after every turn.
                _sse_text(conn, "noted.")
            elif last_tool > last_user:
                _sse_text(conn, "noted.")
            elif profile == "write":
                # A mutating tool with a fat argument: exercises the
                # checkpoint path (snapshot per turn) and the per-call
                # argument copies -- the M180 arena sites. The argument size
                # is the knob (--args-bytes): the per-call copies scale with
                # args bytes x calls/turn.
                _sse_tool(conn, "write_file",
                          {"path": "out.txt", "content": "x" * args_bytes})
            else:
                _sse_tool(conn, "read_file", {"path": "note.txt"})
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
            except socket.timeout:
                continue
            except OSError:
                break
            threading.Thread(target=handle, args=(conn,), daemon=True).start()
        srv.close()

    threading.Thread(target=loop, daemon=True).start()


# ---------------------------------------------------------------- sampling
def rss_kb(pid):
    try:
        with open("/proc/%d/status" % pid) as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return 0


def hwm_kb(pid):
    """VmHWM: the process's lifetime RSS peak. The tool-arena analysis
    lesson applies here -- slope alone can report 'no problem' while the
    peak shows it -- so the final report prints both."""
    try:
        with open("/proc/%d/status" % pid) as f:
            for line in f:
                if line.startswith("VmHWM:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return 0


# ---------------------------------------------------------------- ACP drive
class Acp:
    def __init__(self, proc):
        self.p = proc
        self.buf = b""
        self.nid = 0

    def send(self, method, params, notify=False):
        obj = {"jsonrpc": "2.0", "method": method, "params": params}
        if not notify:
            self.nid += 1
            obj["id"] = self.nid
        self.p.stdin.write((json.dumps(obj) + "\n").encode())
        self.p.stdin.flush()
        return self.nid

    def wait(self, rid, timeout=30.0):
        end = time.time() + timeout
        while time.time() < end:
            line = self.p.stdout.readline()
            if not line:
                return None
            try:
                m = json.loads(line)
            except ValueError:
                continue
            if m.get("id") == rid and ("result" in m or "error" in m):
                return m
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--turns", type=int, default=150)
    ap.add_argument("--profile",
                    choices=["read", "write", "reads", "retry", "save"],
                    default="read",
                    help="reads = many read_file calls per turn (M199: the\n"
                         "INTRA-turn peak, which per-turn scratch cannot "
                         "bound); retry = every model call fails "
                         "--fails-per-call times first (each failure = one "
                         "extra full request rebuild; measures glibc heap "
                         "high-water, scales with request-body size x retry "
                         "count -- use --history-bytes for realistic bodies); "
                         "save = text-only turns with a growing history "
                         "(--history-bytes per turn; measures history + "
                         "jc_session_save's full re-serialize per turn)")
    ap.add_argument("--fails-per-call", type=int, default=2,
                    help="retry profile: injected HTTP-500 failures before "
                         "each success. Wall-clock cost per call is the "
                         "backoff sum (500ms, 1s, 2s...), so keep --turns "
                         "modest. Requires maxRetries > this; the driver "
                         "sets it.")
    ap.add_argument("--history-bytes", type=int, default=0,
                    help="retry: pad the FIRST prompt by this many bytes so "
                         "every rebuilt request body carries it. save: pad "
                         "EVERY prompt by this many bytes (linear history "
                         "growth).")
    ap.add_argument("--args-bytes", type=int, default=2048,
                    help="write profile: size of each write_file content "
                         "argument (the per-call copy magnitude)")
    ap.add_argument("--fixture-bytes", type=int, default=13,
                    help="size of note.txt, the file the read profile reads "
                         "every turn (default 13 = the historical "
                         "'soak fixture\\n'). M197: read_file copies the WHOLE "
                         "file onto the never-reset session arena "
                         "(src/tools/jc_tool_read.c:79), so the per-turn "
                         "retention this soak measures is proportional to this "
                         "number. At 13 bytes the leak is invisible, which is "
                         "why M180 concluded there was none -- raise it to see "
                         "the real slope.")
    ap.add_argument("--no-telem-check", action="store_true",
                    help="skip the rss_kb telemetry cross-check (for "
                         "pre-M180 binaries)")
    ap.add_argument("--csv", default="")
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()

    if not BIN or not os.access(BIN, os.X_OK):
        print("soak: set JC_SOAK_BIN (or JC_E2E_BIN) to the jichi binary",
              file=sys.stderr)
        return 2

    port_box, stop = [], threading.Event()
    serve_mock(port_box, stop, args.profile,
               fails_per_call=(args.fails_per_call
                               if args.profile == "retry" else 0),
               args_bytes=args.args_bytes)
    while not port_box:
        time.sleep(0.01)
    port = port_box[0]

    ws = tempfile.mkdtemp(prefix="jc-soak-ws-")
    home = tempfile.mkdtemp(prefix="jc-soak-home-")
    telem = os.path.join(ws, "telemetry.jsonl")
    with open(os.path.join(ws, "note.txt"), "w") as f:
        if args.fixture_bytes <= 13:
            f.write("soak fixture\n")
        else:
            f.write("soak fixture\n" + "a" * (args.fixture_bytes - 13))
    subprocess.run(["git", "init", "-q", ws], check=False)
    cfg = {
        "models": [
            {"name": "mock", "provider": "openai", "model": "mock",
             "apiBase": "http://127.0.0.1:%d/v1" % port, "apiKey": "x",
             "roles": ["chat"], "contextLength": 400000},
        ],
        "repoMap": False, "references": False,
        "maxRetries": (max(4, args.fails_per_call)
                       if args.profile == "retry" else 0),
        "maxToolIters": 200,
        "permissions": {"allow": ["write_file"]},
    }
    cfgp = os.path.join(home, "config.json")
    with open(cfgp, "w") as f:
        json.dump(cfg, f)

    env = dict(os.environ, LANG="C", LC_ALL="C", HOME=home)
    proc = subprocess.Popen(
        [BIN, "--config", cfgp, "--log", telem, "--log-level", "metrics",
         "serve"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, env=env, cwd=ws)
    acp = Acp(proc)

    rows = []
    rc = 0
    peak_hwm = 0
    try:
        rid = acp.send("initialize",
                       {"protocolVersion": 1, "clientCapabilities": {}})
        if acp.wait(rid) is None:
            print("soak: no initialize result", file=sys.stderr)
            return 1
        rid = acp.send("session/new", {"cwd": ws, "mcpServers": []})
        m = acp.wait(rid)
        sid = (m or {}).get("result", {}).get("sessionId")
        if not sid:
            print("soak: no sessionId", file=sys.stderr)
            return 1

        base = rss_kb(proc.pid)
        print("turn 0: rss %d KB (baseline)" % base)
        rows.append((0, base))
        for i in range(1, args.turns + 1):
            text = "turn %d: read note.txt" % i
            if args.history_bytes > 0:
                if args.profile == "save":
                    text += " " + "p" * args.history_bytes
                elif i == 1:
                    text += " " + "p" * args.history_bytes
            rid = acp.send("session/prompt", {
                "sessionId": sid,
                "prompt": [{"type": "text", "text": text}]})
            if acp.wait(rid, 120.0) is None:
                print("soak: turn %d hung" % i, file=sys.stderr)
                rc = 1
                break
            r = rss_kb(proc.pid)
            rows.append((i, r))
            if i % 25 == 0 or i == args.turns:
                print("turn %d: rss %d KB (+%d)" % (i, r, r - base))
        peak_hwm = hwm_kb(proc.pid)
    finally:
        try:
            proc.stdin.close()
        except OSError:
            pass
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        stop.set()

    if args.csv:
        with open(args.csv, "w") as f:
            f.write("turn,rss_kb\n")
            for t, r in rows:
                f.write("%d,%d\n" % (t, r))

    if len(rows) >= 2:
        first, last = rows[1][1], rows[-1][1]
        n = rows[-1][0]
        print("---")
        print("soak: %d turns; rss first %d KB, last %d KB, peak %d KB, "
              "VmHWM %d KB" %
              (n, first, last, max(r for _, r in rows), peak_hwm))
        if n > 1:
            print("soak: slope %.1f KB/turn (tail half: %.1f KB/turn)" %
                  ((last - first) / float(max(n - 1, 1)),
                   (rows[-1][1] - rows[len(rows) // 2][1]) /
                   float(max(rows[-1][0] - rows[len(rows) // 2][0], 1))))

    # Cross-check with jichi's own M180 telemetry.
    if args.no_telem_check:
        if not args.keep:
            shutil.rmtree(ws, ignore_errors=True)
            shutil.rmtree(home, ignore_errors=True)
        return rc
    try:
        rss_events = []
        with open(telem) as f:
            for line in f:
                try:
                    o = json.loads(line)
                except ValueError:
                    continue
                if o.get("event") == "turn_end" and "rss_kb" in o:
                    rss_events.append(o["rss_kb"])
        if rss_events:
            print("soak: telemetry rss_kb on %d turn_end events "
                  "(first %d, last %d)" %
                  (len(rss_events), rss_events[0], rss_events[-1]))
        else:
            print("soak: WARNING no rss_kb in telemetry (M180 field missing?)",
                  file=sys.stderr)
            rc = rc or 1
    except OSError:
        pass

    if not args.keep:
        shutil.rmtree(ws, ignore_errors=True)
        shutil.rmtree(home, ignore_errors=True)
    else:
        print("soak: kept ws=%s home=%s" % (ws, home))
    return rc


if __name__ == "__main__":
    sys.exit(main())
