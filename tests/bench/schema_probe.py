#!/usr/bin/env python3
"""Tool-schema pressure probe (docs/BENCH_LOCAL_GPU.md).

Replays a captured jichi request body against a live OpenAI-compatible endpoint
while varying ONE thing at a time about the advertised tool array, and reports
whether the model still emits a tool call.

Why this exists: on the bench a small model stopped calling tools entirely once
the full tool set was advertised -- it answered with a single end-of-turn token.
That is a *model* response to schema pressure, not a transport bug, so the useful
question is which dimension of the schema causes it: the number of tools, the
total bytes, or specific heavyweight entries. This probe answers that without
touching any C, and it doubles as the offline pre-test for the deferred
"compact schema mode" item: `--mode compact` applies exactly the transformation
that item proposes (first-sentence descriptions, per-arg prose dropped) and
measures whether tool calling survives it.

Usage:
  python3 schema_probe.py --body body.json --mode count
  python3 schema_probe.py --body body.json --mode compact
  python3 schema_probe.py --body body.json --mode drop-one

Requires: a captured request body (see capture_body.py) and a reachable model.
Python stdlib only.
"""
import argparse
import copy
import json
import sys
import urllib.request

TRIALS_DEFAULT = 3


def post(url, body, timeout=300):
    req = urllib.request.Request(
        url, data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read().decode("utf-8", "replace")


def classify(sse):
    """-> (verdict, completion_tokens, prompt_tokens).

    verdict: 'tool' (a native tool call), 'text' (prose only), 'empty' (the
    single-token stop that is the failure signature), 'error'.
    """
    tool = False
    text = 0
    ptok = ctok = 0
    for line in sse.splitlines():
        if not line.startswith("data: "):
            continue
        payload = line[6:].strip()
        if payload == "[DONE]":
            continue
        try:
            o = json.loads(payload)
        except ValueError:
            continue
        u = o.get("usage") or {}
        ptok = u.get("prompt_tokens", ptok)
        ctok = u.get("completion_tokens", ctok)
        for ch in o.get("choices") or []:
            d = ch.get("delta") or {}
            if d.get("tool_calls"):
                tool = True
            if d.get("content"):
                text += len(d["content"])
    if tool:
        return "tool", ctok, ptok
    if text > 0:
        return "text", ctok, ptok
    return "empty", ctok, ptok


def first_sentence(s):
    """The compact-mode description rule: keep the first sentence, drop the rest."""
    if not s:
        return s
    for i, c in enumerate(s):
        if c == "." and (i + 1 == len(s) or s[i + 1] in " \n"):
            return s[:i + 1]
    return s


def compact(tools):
    """Apply the proposed compact-schema transformation (proposal §5.4):
    first-sentence tool descriptions, per-argument prose dropped."""
    out = copy.deepcopy(tools)
    for t in out:
        fn = t.get("function", {})
        fn["description"] = first_sentence(fn.get("description", ""))
        props = (fn.get("parameters") or {}).get("properties") or {}
        for p in props.values():
            p.pop("description", None)
    return out


def run(url, body, tools, trials):
    b = dict(body)
    b["tools"] = tools
    verdicts = []
    for _ in range(trials):
        try:
            v, ctok, ptok = classify(post(url, b))
        except Exception as e:                      # noqa: BLE001
            v, ctok, ptok = "error(%s)" % type(e).__name__, 0, 0
        verdicts.append((v, ctok, ptok))
    ntool = sum(1 for v, _, _ in verdicts if v == "tool")
    ptok = max(p for _, _, p in verdicts)
    return ntool, verdicts, ptok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--body", default="body.json")
    ap.add_argument("--url", default="http://127.0.0.1:1234/v1/chat/completions")
    ap.add_argument("--mode", default="count",
                    choices=["count", "compact", "drop-one", "compact-count"])
    ap.add_argument("--trials", type=int, default=TRIALS_DEFAULT)
    args = ap.parse_args()

    body = json.load(open(args.body))
    tools = body["tools"]
    names = [t["function"]["name"] for t in tools]
    print("body: %d tools, %d B of tool JSON, model=%s"
          % (len(tools), len(json.dumps(tools)), body.get("model")))

    if args.mode == "count":
        print("\nSweeping the tool COUNT (prefix of the real array):")
        print("  %-6s %-7s %-9s %s" % ("ntools", "bytes", "prompt_tok",
                                       "tool-calls/trials"))
        for n in (1, 2, 4, 6, 8, 10, 12, 14, 16, len(tools)):
            if n > len(tools):
                continue
            sub = tools[:n]
            ntool, v, ptok = run(args.url, body, sub, args.trials)
            print("  %-6d %-7d %-9d %d/%d  %s"
                  % (n, len(json.dumps(sub)), ptok, ntool, args.trials,
                     ",".join(x[0] for x in v)))

    elif args.mode == "compact":
        print("\nFull vs compact schemas (the item-1 pre-test):")
        for label, tt in (("full", tools), ("compact", compact(tools))):
            ntool, v, ptok = run(args.url, body, tt, args.trials)
            print("  %-8s %6d B  prompt_tok=%-6d  tool-calls %d/%d  %s"
                  % (label, len(json.dumps(tt)), ptok, ntool, args.trials,
                     ",".join(x[0] for x in v)))

    elif args.mode == "compact-count":
        # The decisive item-1 question: compaction obviously saves tokens, but
        # does it raise the tool COUNT at which calling collapses? If the two
        # columns break at the same count, compaction buys budget, not ability.
        print("\nBreaking count, full vs compact schemas:")
        print("  %-6s | %-22s | %s" % ("ntools", "full", "compact"))
        comp_all = compact(tools)
        for n in (4, 6, 7, 8, 9, 10, 12, len(tools)):
            if n > len(tools):
                continue
            fn_, fv, fp = run(args.url, body, tools[:n], args.trials)
            cn_, cv, cp = run(args.url, body, comp_all[:n], args.trials)
            print("  %-6d | %d/%d  %5dB %5dtok | %d/%d  %5dB %5dtok"
                  % (n, fn_, args.trials, len(json.dumps(tools[:n])), fp,
                     cn_, args.trials, len(json.dumps(comp_all[:n])), cp))

    else:  # drop-one
        base_n, _, _ = run(args.url, body, tools, args.trials)
        print("\nBaseline (all %d tools): %d/%d tool calls"
              % (len(tools), base_n, args.trials))
        print("Dropping ONE tool at a time (looking for a single culprit):")
        for i, nm in enumerate(names):
            sub = tools[:i] + tools[i + 1:]
            ntool, v, _ = run(args.url, body, sub, args.trials)
            flag = "  <-- recovers" if ntool > base_n else ""
            print("  without %-24s %d/%d%s" % (nm, ntool, args.trials, flag))

    return 0


if __name__ == "__main__":
    sys.exit(main())
