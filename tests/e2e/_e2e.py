"""Shared helpers for the offline E2E drivers (Python stdlib only).

The agent binary is JC_E2E_BIN; JC_E2E_CONFIG (if set) is passed as --config.
Drivers exit 0 on success, non-zero with a FAIL message otherwise.
"""
import os, sys, pty, select, time, struct, fcntl, termios, subprocess

BIN = os.environ["JC_E2E_BIN"]
CFG = os.environ.get("JC_E2E_CONFIG", "")
# M198 #5: extra flags appended to EVERY invocation, so the whole suite can be
# re-run under a different profile without a second suite. `run.sh --lite` sets
# this to "--lite", which flips ~10 resource defaults at once (snapshots/repoMap/
# references/markdown off, parallel 1, subagent depth 0, smaller context/iters/
# retries/tool caps) -- a corner of the config space development never occupies.
EXTRA = os.environ.get("JC_E2E_EXTRA", "").split()
LITE = "--lite" in EXTRA or "--low-memory" in EXTRA


def _argv(extra):
    a = [BIN]
    if CFG:
        a += ["--config", CFG]
    return a + EXTRA + list(extra)


def skip(msg):
    """Skip this driver (exit 0). For a check that a profile legitimately
    disables -- assert nothing rather than weaken the assertion."""
    print("skip: %s" % msg)
    sys.exit(0)


def skip_if_lite(what):
    """Skip when running under --lite, naming the feature that is off."""
    if LITE:
        skip("%s is disabled under --lite" % what)


class isolated_home(object):
    """Context manager giving the child a private $HOME.

    M198: _e2e deliberately does NOT isolate HOME by default -- a few drivers
    exercise global config/asset discovery under the real one. But starting the
    TUI creates and saves a session, so any driver that spawns it deposits files
    in the developer's real ~/.jichi.d/sessions (a full suite run used to leave
    ~8 behind, recognisable by a workspaceDirectory of /tmp/jichi_e2e_*). Wrap
    the spawn in this when the driver does not need the real HOME:

        with _e2e.isolated_home():
            pid, fd = _e2e.spawn([], cwd=ws)
            ...
    """

    def __init__(self, keep=False):
        self.keep = keep
        self.path = None
        self._old = None

    def __enter__(self):
        import tempfile
        self.path = tempfile.mkdtemp(prefix="jichi_e2e_home_")
        self._old = os.environ.get("HOME")
        os.environ["HOME"] = self.path
        return self.path

    def __exit__(self, *exc):
        if self._old is None:
            os.environ.pop("HOME", None)
        else:
            os.environ["HOME"] = self._old
        if not self.keep and self.path:
            import shutil
            shutil.rmtree(self.path, ignore_errors=True)
        return False


def isolate_home_for_module():
    """Enter an isolated_home for the rest of the process, cleaned up at exit.

    For drivers whose every spawn should use a private HOME; avoids indenting the
    whole file into a `with` block."""
    import atexit
    h = isolated_home()
    h.__enter__()
    atexit.register(h.__exit__)
    return h.path


def run(extra, timeout=20, cwd=None):
    """Non-interactive: returns (returncode, stdout, stderr)."""
    env = dict(os.environ, LANG="C", LC_ALL="C")
    try:
        p = subprocess.run(_argv(extra), capture_output=True, text=True,
                           timeout=timeout, cwd=cwd, env=env)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return 124, "", "timeout"


def spawn(extra, cwd=None, cols=0, color=False):
    """Interactive: fork a PTY running the TUI; returns (pid, fd)."""
    argv = _argv(extra)
    pid, fd = pty.fork()
    if pid == 0:
        if cwd:
            os.chdir(cwd)
        os.environ["LANG"] = "C"; os.environ["LC_ALL"] = "C"
        if not color:
            os.environ["NO_COLOR"] = "1"
        os.execv(BIN, argv)
        os._exit(127)
    if cols:
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, cols, 0, 0))
    return pid, fd


def drain(fd, secs, buf):
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                d = os.read(fd, 4096)
            except OSError:
                return
            if not d:
                return
            buf.append(d)


def text(buf):
    return "".join(b.decode("utf-8", "replace") for b in buf)


def recv_http_request(conn, deadline=15.0):
    """Read a full HTTP request (headers + Content-Length body) robustly.

    The naive mock pattern set a 2s socket timeout and broke out of the
    body loop on the FIRST timeout, returning a TRUNCATED body -- which under
    full-CI load (a large second request carrying, e.g., extracted PDF text)
    silently failed a marker check that looked like a real regression
    (ANECDOTES #18). This keeps reading until the declared Content-Length is
    satisfied, tolerating per-recv timeouts up to an overall wall-clock
    `deadline`, so a slow upload is waited out instead of truncated. Bounded
    by the caller's outer `timeout` wrapper for a genuine hang."""
    buf = b""
    end = time.time() + deadline
    conn.settimeout(0.5)
    # Headers first.
    while b"\r\n\r\n" not in buf and time.time() < end:
        try:
            d = conn.recv(65536)
        except OSError:
            continue  # timeout: keep waiting until the deadline
        if not d:
            return buf
        buf += d
    head, _, body = buf.partition(b"\r\n\r\n")
    clen = 0
    for line in head.split(b"\r\n"):
        if line.lower().startswith(b"content-length:"):
            try:
                clen = int(line.split(b":", 1)[1].strip())
            except ValueError:
                clen = 0
    while len(body) < clen and time.time() < end:
        try:
            d = conn.recv(65536)
        except OSError:
            continue  # timeout mid-body: wait it out, don't truncate
        if not d:
            break
        body += d
    return head + b"\r\n\r\n" + body


def recv_http_head_body(conn, deadline=15.0):
    """recv_http_request, split as (head, body) -- the other shape drivers want.

    M201: twelve drivers each rolled their own reader with the naive
    break-on-first-timeout body loop (ANECDOTES #18), and two of them
    (prose_nudge, constraints_scope) were exactly the drivers seen flaking inside
    a full suite run. A truncated body does not look like a timeout: it looks like
    a WRONG ANSWER, because the mock's `marker in req` test silently fails. Both
    observed suite failures had that signature. Delegating removes the class."""
    raw = recv_http_request(conn, deadline)
    head, _, body = raw.partition(b"\r\n\r\n")
    return head, body


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


def ok(msg):
    print("ok:", msg)
