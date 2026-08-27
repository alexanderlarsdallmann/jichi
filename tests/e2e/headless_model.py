"""Model-gated: runs only when JC_E2E_MODEL is set, against a *real* config
(JC_E2E_REAL_CONFIG). Verifies the headless/SSH contract with a live model: a
turn that spawns a subagent must emit no ANSI and no nested 'subagent' banner on
stdout -- only the final answer. (The offline fixture config can't reach a model,
so this test uses the developer's real config, not JC_E2E_CONFIG.)"""
import os, sys, tempfile, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _e2e

model = os.environ.get("JC_E2E_MODEL")
if not model:
    print("skip: JC_E2E_MODEL not set"); sys.exit(0)
cfg = os.environ.get("JC_E2E_REAL_CONFIG", "")
if not cfg or not os.path.exists(cfg):
    print("skip: JC_E2E_REAL_CONFIG not set to an existing config"); sys.exit(0)

ws = tempfile.mkdtemp(prefix="jichi_e2e_")
with open(os.path.join(ws, "NOTE.md"), "w") as f:
    f.write("# Title\nThe secret word is kumquat.\n")

argv = [_e2e.BIN, "--config", cfg, "--model", model, "--no-route", "--auto",
        "-p", "Use the spawn_subagent tool to read NOTE.md, then reply with the "
              "secret word."]
try:
    p = subprocess.run(argv, capture_output=True, text=True, timeout=140, cwd=ws,
                       env=dict(os.environ, LANG="C", LC_ALL="C"))
except subprocess.TimeoutExpired:
    _e2e.fail("headless subagent run timed out")

if p.returncode != 0:
    _e2e.fail("headless subagent run failed (rc=%d)\n%s" % (p.returncode,
              p.stderr[-400:]))
if "\x1b" in p.stdout:
    _e2e.fail("headless stdout must contain no ANSI escapes:\n" + repr(p.stdout[:200]))
if "subagent " in p.stdout:
    _e2e.fail("headless stdout must not contain the nested subagent banner:\n"
              + p.stdout)
if "kumquat" not in p.stdout.lower():
    _e2e.fail("expected the subagent's answer on stdout:\n" + p.stdout)
_e2e.ok("headless model (subagent stays silent on stdout)")
