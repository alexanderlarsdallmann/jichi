#!/usr/bin/env python3
"""Prompt-cache latency probe (PROMPT_CACHING.md "cached=0 is not proof").

Measured 2026-08-11 on a LiteLLM->vLLM proxy: an identical 18.9k-token prefix
prefilled in 2.97s cold and 0.16s on the next call -- an 18.6x speedup -- while
the usage carried no prompt_tokens_details at all. jichi's cached= counter and
the --cache-audit verdict can only repeat what the wire says, so on such a
backend they read zero through a working cache. This probe mechanizes the one
signal the server cannot hide: repeat-call latency on a byte-identical prefix.

The verdict is asymmetric, the mirror image of version_probe.py's: this probe
can PROVE a cache is live (wire counters, or an unambiguous latency ratio) but
can never prove absence -- a model whose prefill is already fast shows no
measurable ratio either way (measured the same day: 0.75s -> 0.72s on a model
group whose caching state is simply unknown). "no evidence" means exactly that.

Usage:
  python3 tests/bench/cache_probe.py --url http://127.0.0.1:1234/v1/chat/completions \
      --model MODEL_ID [--key-env JLU_API_KEY] [--trials 3]
  python3 tests/bench/cache_probe.py --mode self-test

Python stdlib only. The prefix is stamped unique per run, so earlier runs
cannot pre-warm it.
"""
import argparse
import json
import os
import sys
import time
import urllib.request

CALL_TIMEOUT = 300
PREFIX_REPEATS = 900   # ~18k tokens of stable system prefix
DETECTED_RATIO = 3.0   # cold/warm above this: unambiguous reuse
GRAY_RATIO = 1.5       # between gray and detected: suggestive, not proof


def verdict(cold_s, warm_s, wire_cached):
    """-> (code, human line). Pure; unit-tested by --mode self-test.

    codes: 'wire' (reported by the server), 'latency' (proved by ratio),
    'inconclusive' (suggestive ratio), 'no-evidence' (flat -- NOT absence)."""
    if wire_cached is not None and wire_cached > 0:
        return ("wire", "cache CONFIRMED on the wire (%d cached tokens "
                "reported)" % wire_cached)
    if not warm_s:
        return ("no-evidence", "no warm calls measured")
    med = sorted(warm_s)[len(warm_s) // 2]
    ratio = cold_s / med if med > 0 else float("inf")
    if ratio >= DETECTED_RATIO:
        return ("latency", "cache DETECTED by latency: %.2fs cold -> %.2fs "
                "warm median (%.1fx) with nothing reported on the wire"
                % (cold_s, med, ratio))
    if ratio >= GRAY_RATIO:
        return ("inconclusive", "suggestive but not conclusive: %.2fs -> "
                "%.2fs (%.1fx); rerun with more trials or a larger prefix"
                % (cold_s, med, ratio))
    return ("no-evidence", "no evidence of prefix reuse (%.2fs -> %.2fs, "
            "%.1fx) -- NOT proof of absence: a fast-prefill model can cache "
            "invisibly below measurement noise" % (cold_s, med, ratio))


def chat(url, model, key, sysmsg):
    body = {"model": model, "max_tokens": 8, "temperature": 0,
            "messages": [{"role": "system", "content": sysmsg},
                         {"role": "user", "content": "Say OK"}]}
    headers = {"Content-Type": "application/json"}
    if key:
        headers["Authorization"] = "Bearer " + key
    req = urllib.request.Request(url, data=json.dumps(body).encode(),
                                 headers=headers, method="POST")
    t0 = time.time()
    with urllib.request.urlopen(req, timeout=CALL_TIMEOUT) as r:
        o = json.loads(r.read().decode("utf-8", "replace"))
    dt = time.time() - t0
    usage = o.get("usage", {})
    det = usage.get("prompt_tokens_details") or {}
    return dt, usage.get("prompt_tokens"), det.get("cached_tokens")


def probe(args, key):
    stamp = os.urandom(8).hex()
    sysmsg = (stamp + " You are a careful assistant; the following context "
              "is fixed for this session. " * 1) + \
             ("The jichi project keeps its prefix byte-stable so a caching "
              "backend can reuse it across turns. " * PREFIX_REPEATS)
    cold = None
    warms = []
    wire = None
    for i in range(args.trials + 1):
        dt, ptok, cached = chat(args.url, args.model, key, sysmsg)
        kind = "cold" if i == 0 else "warm"
        print("  call %d (%s): %6.2fs  prompt_tokens=%s  cached=%s"
              % (i + 1, kind, dt, ptok, cached), file=sys.stderr)
        if i == 0:
            cold = dt
        else:
            warms.append(dt)
        if cached is not None:
            wire = (wire or 0) + (cached if i > 0 else 0)
    code, line = verdict(cold, warms, wire)
    print("\n%s cache probe -- %s" % (args.model, time.strftime("%Y-%m-%d")))
    print("verdict: %s" % line)
    if code in ("wire", "latency"):
        print("note: keep the prefix byte-stable (jichi already does, M31d);"
              " prefix stability now beats prefix size on this backend.")
    return 0


SELF_TEST = [
    ("HRZ gemma measured case -> latency", (2.97, [0.16, 0.16], None),
     "latency"),
    ("fast-prefill flat case -> no-evidence", (0.75, [0.70, 0.72], None),
     "no-evidence"),
    ("gray-zone ratio -> inconclusive", (1.0, [0.55], None), "inconclusive"),
    ("wire counters win over flat latency", (5.0, [5.0], 1000), "wire"),
    ("zero wire counter is not confirmation", (5.0, [5.0], 0), "no-evidence"),
]


def self_test():
    failed = 0
    for n, (name, case, want) in enumerate(SELF_TEST, 1):
        got = verdict(*case)[0]
        if got == want:
            print("ok %d - %s" % (n, name))
        else:
            failed += 1
            print("FAIL %d - %s (got %s, want %s)" % (n, name, got, want))
    print("# self-test: %d checks, %d failed" % (len(SELF_TEST), failed))
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--mode", choices=["probe", "self-test"], default="probe")
    ap.add_argument("--url", help="chat/completions endpoint URL")
    ap.add_argument("--model", help="model id to probe")
    ap.add_argument("--key-env", default="",
                    help="env var holding the API key (omit for keyless)")
    ap.add_argument("--trials", type=int, default=3,
                    help="warm repetitions after the cold call (default 3)")
    args = ap.parse_args()

    if args.mode == "self-test":
        sys.exit(self_test())
    if not args.url or not args.model:
        ap.error("--url and --model are required in probe mode")
    key = os.environ.get(args.key_env, "") if args.key_env else ""
    if args.key_env and not key:
        print("error: $%s is empty" % args.key_env, file=sys.stderr)
        sys.exit(1)
    sys.exit(probe(args, key))


if __name__ == "__main__":
    main()
