"""E2E smoke for the stress harness (M185): 2 instances x 3 requests against
a local mock SSE server; the driver CSV and the merged telemetry report must
both account for every request. Offline, no real model."""
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _e2e

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _read_request(conn):
    """M201: delegates to the SHARED robust reader.

    This used to be a local copy of the naive pattern ANECDOTES #18
    documents: a short socket timeout whose body loop breaks on the FIRST
    timeout, silently returning a TRUNCATED request. That does not look
    like a timeout to the caller -- it looks like a wrong answer, because
    the mock's `marker in req` test then fails. Twelve drivers carried it;
    two of them were the ones seen flaking inside a full suite run."""
    head, _b = _e2e.recv_http_head_body(conn)
    return head


def _serve(port_box, stop):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    srv.listen(32)
    port_box.append(srv.getsockname()[1])
    payload = (
        'data: {"id":"1","object":"chat.completion.chunk","choices":'
        '[{"index":0,"delta":{"role":"assistant","content":"ok"},'
        '"finish_reason":null}]}\n\n'
        'data: {"id":"1","object":"chat.completion.chunk","choices":'
        '[{"index":0,"delta":{},"finish_reason":"stop"}],'
        '"usage":{"prompt_tokens":10,"completion_tokens":1}}\n\n'
        "data: [DONE]\n\n").encode()

    def handle(conn):
        try:
            conn.settimeout(10.0)
            if _read_request(conn) is None:
                return
            hdr = ("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                   "Connection: close\r\nContent-Length: %d\r\n\r\n"
                   % len(payload))
            conn.sendall(hdr.encode() + payload)
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
            threading.Thread(target=handle, args=(conn,),
                             daemon=True).start()
        srv.close()

    threading.Thread(target=loop, daemon=True).start()


def main():
    port_box, stop = [], threading.Event()
    _serve(port_box, stop)
    while not port_box:
        pass
    out = tempfile.mkdtemp(prefix="jc-stress-e2e-")
    try:
        p = subprocess.run(
            [sys.executable, os.path.join(ROOT, "examples", "stress",
                                          "stress.py"),
             "--jichi", _e2e.BIN,
             "--server", "http://127.0.0.1:%d/v1" % port_box[0],
             "--model", "mock", "--instances", "2", "--requests", "3",
             "--ramp", "0", "--timeout", "30", "--out", out],
            capture_output=True, text=True, timeout=120)
        if p.returncode != 0:
            _e2e.fail("stress.py failed rc=%d\n%s%s"
                      % (p.returncode, p.stdout, p.stderr))
            return
        if "6 completed, 6 ok" not in p.stdout:
            _e2e.fail("expected 6/6 requests ok:\n%s" % p.stdout)
            return
        r = subprocess.run(
            [sys.executable, os.path.join(ROOT, "examples", "stress",
                                          "report.py"), out],
            capture_output=True, text=True, timeout=30)
        if r.returncode != 0 or "| 6 (6) |" not in r.stdout:
            _e2e.fail("report.py did not account for 6 ok requests:\n%s%s"
                      % (r.stdout, r.stderr))
            return
        # jichi's own telemetry agrees: 6 ok model calls with latencies.
        if "| 6/0/0 |" not in r.stdout:
            _e2e.fail("telemetry calls not 6 ok / 0 err / 0 timeout:\n%s"
                      % r.stdout)
            return
        _e2e.ok("stress harness: 2x3 against the mock -> driver CSV and "
                "telemetry report agree (M185)")
    finally:
        stop.set()
        import shutil
        shutil.rmtree(out, ignore_errors=True)


main()
