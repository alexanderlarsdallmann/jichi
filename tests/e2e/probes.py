#!/usr/bin/env python3
"""e2e: the bench probes' offline self-tests (M380).

version_probe.py (M374) and cache_probe.py (M378) each carry a
`--mode self-test` that proves their gate/verdict function two-sided with no
model and no network -- but until M380 nothing RAN them: a regression in the
compile gate or the verdict thresholds would have shipped silently, to be
discovered by the next operator mid-probe with a live model on the clock.
This driver wires both into `make e2e`.

Deliberately NOT the smoke tier: that tier is python-free by charter (M209),
and the probes are python -- so their self-tests belong to the tier that
already requires python3, beside the bench they serve.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BENCH = os.path.join(HERE, "..", "bench")

fails = 0
for probe in ("version_probe.py", "cache_probe.py"):
    p = subprocess.run([sys.executable, os.path.join(BENCH, probe),
                        "--mode", "self-test"],
                       capture_output=True, text=True, timeout=300)
    sys.stdout.write(p.stdout)
    # A probe's self-test reports "# self-test: N checks, 0 failed" and exits
    # nonzero on any failure; require both, so a probe that starts exiting 0
    # while printing failures (or vice versa) is caught either way.
    if p.returncode != 0 or " 0 failed" not in p.stdout:
        print("FAIL - %s self-test (rc=%d)" % (probe, p.returncode))
        fails += 1
    else:
        print("ok - %s self-test green" % probe)

if fails:
    sys.exit(1)
print("# probes: both self-tests green")
