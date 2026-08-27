"""Model-gated: drives the full ACP terminal/* delegation round-trip with a live
model. Runs only when JC_E2E_MODEL is set (against JC_E2E_REAL_CONFIG, a config
that can actually reach a model); skips otherwise so CI stays offline.

Acts as an ACP *client* that advertises the `terminal` capability, then sends a
prompt asking the agent to run a shell command via run_terminal_command. When the
agent delegates execution, it issues terminal/create -> terminal/wait_for_exit ->
terminal/output -> terminal/release requests back to us; we service them (acting
as the editor's terminal) and assert the command was delegated (a unique marker
appears in the create request) and the lifecycle completes cleanly.

No assertion depends on the model's wording -- only on the protocol traffic -- so
it is deterministic given any tool-calling model.
"""
import os, sys, json, subprocess, tempfile, shutil, threading

try:
    import queue
except ImportError:
    import Queue as queue

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _e2e

MARKER = "jichi_terminal_marker_42"

model = os.environ.get("JC_E2E_MODEL")
if not model:
    print("skip: JC_E2E_MODEL not set"); sys.exit(0)
cfg = os.environ.get("JC_E2E_REAL_CONFIG", "")
if not cfg or not os.path.exists(cfg):
    print("skip: JC_E2E_REAL_CONFIG not set to an existing config"); sys.exit(0)


def main():
    ws = tempfile.mkdtemp(prefix="jichi_acp_term_")
    argv = [_e2e.BIN, "--config", cfg, "--model", model, "--no-route",
            "--auto", "--acp"]
    p = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         cwd=ws, env=dict(os.environ, LANG="C", LC_ALL="C"),
                         bufsize=1, text=True)

    state = {"saw_create": False, "saw_release": False, "create_cmd": ""}

    # A background thread does blocking readline()s into a queue. (Mixing
    # select() with a buffered text stream silently drops lines already sitting
    # in Python's read buffer, which deadlocks the request/response protocol.)
    lines = queue.Queue()

    def reader():
        for line in iter(p.stdout.readline, ""):
            lines.put(line)
        lines.put(None)  # EOF sentinel

    threading.Thread(target=reader, daemon=True).start()

    def send(obj):
        p.stdin.write(json.dumps(obj) + "\n")
        p.stdin.flush()

    def reply(mid, result):
        send({"jsonrpc": "2.0", "id": mid, "result": result})

    def read_until(pred, deadline=150.0):
        """Read jichi's stdout, servicing its terminal/* requests, until pred(msg)."""
        while True:
            try:
                line = lines.get(timeout=deadline)
            except queue.Empty:
                _e2e.fail("timeout waiting for ACP message")
            if line is None:
                _e2e.fail("ACP server closed stdout unexpectedly")
            line = line.strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except ValueError:
                _e2e.fail("non-JSON line: %r" % line)
            method = msg.get("method")
            mid = msg.get("id")
            # A request from the agent (method + id): service it as the editor.
            if method is not None and mid is not None:
                if method == "terminal/create":
                    state["saw_create"] = True
                    pr = msg.get("params", {})
                    state["create_cmd"] = json.dumps(pr)
                    reply(mid, {"terminalId": "t1"})
                elif method == "terminal/wait_for_exit":
                    reply(mid, {"exitStatus": {"exitCode": 0}})
                elif method == "terminal/output":
                    reply(mid, {"output": MARKER + "\n", "truncated": False,
                                "exitStatus": {"exitCode": 0}})
                elif method in ("terminal/release", "terminal/kill"):
                    if method == "terminal/release":
                        state["saw_release"] = True
                    reply(mid, {})
                elif method == "session/request_permission":
                    # Shouldn't happen under --auto, but allow defensively.
                    reply(mid, {"outcome": {"outcome": "selected",
                                            "optionId": "allow_once"}})
                else:
                    reply(mid, {})
                continue
            # A notification (method, no id): ignore.
            if method is not None:
                continue
            # A response to one of our requests.
            if pred(msg):
                return msg

    try:
        send({"jsonrpc": "2.0", "id": 1, "method": "initialize",
              "params": {"protocolVersion": 1,
                         "clientCapabilities": {"terminal": True}}})
        read_until(lambda m: m.get("id") == 1)

        send({"jsonrpc": "2.0", "id": 2, "method": "session/new",
              "params": {"cwd": ws}})
        snew = read_until(lambda m: m.get("id") == 2)
        sid = snew.get("result", {}).get("sessionId")
        if not sid:
            _e2e.fail("no sessionId from session/new: %r" % snew)

        prompt = ("Use the run_terminal_command tool to run exactly this shell "
                  "command: echo " + MARKER + " -- then reply with its output.")
        send({"jsonrpc": "2.0", "id": 3, "method": "session/prompt",
              "params": {"sessionId": sid,
                         "prompt": [{"type": "text", "text": prompt}]}})
        done = read_until(lambda m: m.get("id") == 3)
        if "result" not in done:
            _e2e.fail("session/prompt errored: %r" % done)

        if not state["saw_create"]:
            _e2e.fail("agent did not delegate execution (no terminal/create)")
        if MARKER not in state["create_cmd"]:
            _e2e.fail("delegated command missing marker: %s"
                      % state["create_cmd"])
        if not state["saw_release"]:
            _e2e.fail("agent did not release the terminal")
        _e2e.ok("acp terminal/* delegation round-trip")
    finally:
        try:
            p.stdin.close()
        except Exception:
            pass
        try:
            p.wait(timeout=10)
        except Exception:
            p.kill()
        shutil.rmtree(ws, ignore_errors=True)


if __name__ == "__main__":
    main()
