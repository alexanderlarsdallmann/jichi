#!/usr/bin/env python3
"""What mid-turn compaction actually does under pressure. A measurement.

DEFERRED.md carries two rows that both wait on a workload rather than an
opinion: "decide what jichi should DO when mid-turn compaction cannot reach its
target" (the M321 row, three options a/b/c), and "a mid-turn mechanism for turns
eliding cannot save" (the M588 row, whose revisit condition is a workload that
presses at a window that is ALREADY generously sized). This script reads the
existing telemetry and answers both -- no run is made, so nothing here is tuned
to make a point.

    python3 tests/measure/compaction_pressure.py            # ~/.jichi.d/telemetry
    python3 tests/measure/compaction_pressure.py DIR [DIR...]

THE INSTRUMENT, written down so it is not re-invented slightly differently every
time it is used -- M706's rule, and this script exists because M700 re-invented
it once already:

  population  a `compact` event with phase=midturn, before/after/target present,
              AND `pressed` true.

              NOT `target > 0`. jc_compact.c:1296 sets the target
              unconditionally -- "a property of the LIMIT, not of this pass" --
              so `target > 0` also admits the eager zero-loss dedup, which never
              had a target to miss. On that early return `local.before` and
              `local.after` are assigned from ONE `effective` value, so
              before == after is true BY CONSTRUCTION there and is not an
              observation. jc_agent.c's emit site had already said it about the
              `short` field: "an unpressured pass never had a target to fall
              short of ... 19 of 19 such events in the measured workload were
              false."

  declared    a pass presses an UNDER-DECLARED limit when some model_call the
              session SERVED counted more input than the limit THAT PASS was
              aiming at. Per pass, not per session: a session may raise its
              limit mid-run -- which is the remedy M459's notice asks for -- and
              one such session here has a served 203,264 that refutes its
              150,000 declaration and not its 256,000, so one of its pressed
              passes drops and two are kept. The
              presence of `in_tok` IS the proof it was served: jc_agent.c adds
              in_tok/cache_read_in/cache_write_in only inside the
              `st == JC_OK && http_status < 400` branch. The size is
              in_tok + cache_read_in + cache_write_in, which is exactly how
              jc_agent.c computes app->last_prompt_tokens (M494). Those passes
              are the M459 case -- compacting toward a target never needed --
              and are excluded, because DEFERRED.md asks for a CORRECTLY
              declared window.

              This test is one-sided and says so: it can prove a window too
              small, never prove one exactly right. A session whose requests
              never passed the declared limit gives no evidence either way.

  ordinal     a pressed pass's position within its (session, turn). The marginal
              yield of the Nth pass is the finding this script exists to expose,
              and it cannot be seen in any aggregate that pools the ordinals.

FIRST MEASUREMENT (2026-09-22, M710, this machine's 49,600 events / 243
sessions). 313 passes carry a positive target; 64 of them were never pressed;
1 more presses a limit proven under-declared. The core is 248.

  - 200 of 248 (81%) elided NOTHING. 43 reduced and stayed over target. 5
    reached it. 228 (92%) ended `unrelieved` -- still above the 80% high-water,
    so the same turn re-triggers next round. `after <= before` held 248/248.
  - The requests went out and were SERVED, every time: 0 of 234 counted over the
    declared limit, max 0.980 of it, median 0.862. ZERO context-overflow
    rejections in the whole corpus (jc_text_is_context_overflow's six
    signatures, matched against every failed model_call).
  - The estimate is not the problem: |served - `after`| median 251 tokens,
    226 of 234 within 5%.
  - The exhaustion curve is the answer. 1st pressed pass of a turn: 19,465
    tokens reclaimed. 2nd: 15,306. 3rd-10th: 6,007. 11th and later: 161 -- and
    those are 78% of all pressed passes. The first pass is 4% of the passes and
    gets 40% of the reclaim. Only 12 turns, so the early bands are thin; the
    194-pass tail is the well-powered one.
  - Per window: 65,536 -> 107 pressed, fixed overhead 16,259 (24.8% of the
    window, corroborating M588's independently-measured 16,268 / 25% to nine
    tokens); 196,608 -> 1 pressed, 0 unrelieved (M588's control reproduces);
    150,000 -> 138 pressed, all one session, of which 128 are ONE TURN that ran
    the same 676-byte `run_tests` 120 times for 13-token replies.
    ELIDE_MIN_BYTES is 800, so not one of those 120 results was ever eligible.

See docs/analysis/2026-09-22-compaction-decided.md.
"""
import collections
import glob
import json
import os
import statistics
import sys

ELIDE_MIN_BYTES = 800           # jc_compact.c
MIDTURN_HIGH_PCT = 80
MIDTURN_TARGET_PCT = 60

# jc_cli.c:277 jc_text_is_context_overflow
OVERFLOW_SIGS = ("n_ctx", "than the context length", "Context size has been exceeded",
                 "context_length_exceeded", "maximum context length",
                 "exceeds the model's context")


def load(dirs):
    """Every compact / model_call / tool_call event, grouped by session."""
    by_sid = collections.defaultdict(list)
    files = 0
    for d in dirs:
        paths = ([d] if os.path.isfile(d)
                 else sorted(glob.glob(os.path.join(d, "*.jsonl"))))
        for p in paths:
            files += 1
            with open(p, errors="replace") as fh:
                for line in fh:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        e = json.loads(line)
                    except ValueError:
                        continue
                    by_sid[e.get("sid")].append(e)
    for sid in by_sid:
        by_sid[sid].sort(key=lambda e: e.get("seq") if e.get("seq") is not None else -1)
    return by_sid, files


def served_size(e):
    """The server's own input count for a request it ACCEPTED, or None.

    `in_tok` is written only on the success branch, so its presence is the
    proof. The sum matches app->last_prompt_tokens exactly.
    """
    if e.get("event") != "model_call" or "in_tok" not in e:
        return None
    return (e["in_tok"] + e.get("cache_read_in", 0) + e.get("cache_write_in", 0))


def main(argv):
    dirs = argv[1:] or [os.path.expanduser("~/.jichi.d/telemetry")]
    by_sid, nfiles = load(dirs)
    nev = sum(len(v) for v in by_sid.values())
    print("corpus: %d events, %d sessions, %d files" % (nev, len(by_sid), nfiles))

    # --- the declared-window test, one-sided and per session -----------------
    max_served = {}
    for sid, evs in by_sid.items():
        sizes = [served_size(e) for e in evs]
        sizes = [s for s in sizes if s is not None]
        max_served[sid] = max(sizes) if sizes else None

    withtarget, pressed, core, dropped_unpressed, dropped_under = [], [], [], 0, 0
    nxt = {}
    for sid, evs in by_sid.items():
        for i, e in enumerate(evs):
            if not (e.get("event") == "compact" and e.get("phase") == "midturn"
                    and "before" in e and e.get("target", 0) > 0):
                continue
            withtarget.append(e)
            if e.get("pressed") is not True:
                dropped_unpressed += 1
                continue
            pressed.append(e)
            m, lim = max_served.get(sid), e.get("limit")
            if m is not None and lim and m > lim:
                dropped_under += 1
                continue
            core.append(e)
            for j in range(i + 1, len(evs)):
                if evs[j].get("event") == "model_call":
                    s = served_size(evs[j])
                    if s is not None:
                        nxt[id(e)] = s
                    break

    print("\npasses with a positive target      : %d   (the M700 population)" % len(withtarget))
    print("  - never pressed                  : %d   before==after by construction there"
          % dropped_unpressed)
    print("  - pressing an under-declared limit: %d" % dropped_under)
    print("  = THE CORE                       : %d\n" % len(core))
    if not core:
        return 0

    def cls(e):
        if e["before"] == e["after"]:
            return "elided nothing"
        return "reached target" if e["after"] <= e["target"] else "reduced, still over"

    sp = collections.Counter(cls(e) for e in core)
    for k in ("elided nothing", "reduced, still over", "reached target"):
        print("  %-22s %4d  (%4.1f%%)" % (k, sp[k], 100.0 * sp[k] / len(core)))
    for f in ("short", "unrelieved", "latched"):
        n = sum(1 for e in core if e.get(f))
        print("  %-22s %4d  (%4.1f%%)" % ("event says " + f, n, 100.0 * n / len(core)))
    print("  %-22s %4d/%d" % ("after <= before",
                              sum(1 for e in core if e["after"] <= e["before"]), len(core)))

    # --- did anything actually overflow? -------------------------------------
    over = rejected = 0
    ratios = []
    for e in core:
        s = nxt.get(id(e))
        if s is None or not e.get("limit"):
            continue
        ratios.append(s / float(e["limit"]))
        if s > e["limit"]:
            over += 1
    for sid, evs in by_sid.items():
        for e in evs:
            if e.get("event") == "model_call" and e.get("ok") is False:
                blob = "%s %s %s" % (e.get("error"), e.get("error_body"), e.get("response"))
                if any(sig in blob for sig in OVERFLOW_SIGS):
                    rejected += 1
    print("\n  the request that then went out, by the SERVER's count:")
    print("    paired with a served request   : %d of %d" % (len(ratios), len(core)))
    if ratios:
        print("    over the declared limit        : %d" % over)
        print("    served/limit  max %.3f  median %.3f  min %.3f"
              % (max(ratios), statistics.median(ratios), min(ratios)))
    print("    context-overflow REJECTIONS in the whole corpus: %d" % rejected)
    err = [nxt[id(e)] - e["after"] for e in core if id(e) in nxt]
    if err:
        within = sum(1 for e in core if id(e) in nxt
                     and abs(nxt[id(e)] - e["after"]) < 0.05 * nxt[id(e)])
        print("    |served - `after`| median %d tok, %d of %d within 5%% "
              "-- the estimate is not what is wrong"
              % (int(statistics.median([abs(x) for x in err])), within, len(err)))

    # --- the exhaustion curve, which no pooled aggregate can show ------------
    turns = collections.defaultdict(list)
    for e in core:
        turns[(e.get("sid"), e.get("turn"))].append(e)
    bands = [(1, 1, "1st pressed pass of the turn"), (2, 2, "2nd"),
             (3, 10, "3rd-10th"), (11, 10 ** 9, "11th and later")]
    acc = {b[2]: [0, 0, 0] for b in bands}
    for k in turns:
        ps = sorted(turns[k], key=lambda e: e.get("seq", -1))
        for i, e in enumerate(ps, 1):
            for lo, hi, lbl in bands:
                if lo <= i <= hi:
                    a = acc[lbl]
                    a[0] += 1
                    a[1] += 1 if e.get("elided", 0) > 0 else 0
                    a[2] += e["before"] - e["after"]
                    break
    print("\n  MARGINAL YIELD BY POSITION IN THE TURN  (%d turns)" % len(turns))
    print("    %-30s %5s %9s %14s %10s" % ("", "n", "elided>0", "tok reclaimed", "per pass"))
    tot = sum(a[2] for a in acc.values()) or 1
    for _, _, lbl in bands:
        n, el, rec = acc[lbl]
        if n:
            print("    %-30s %5d %8d%% %14d %10d"
                  % (lbl, n, 100 * el // n, rec, rec // n))
    n1, _, rec1 = acc["1st pressed pass of the turn"]
    print("    the 1st pass is %d%% of the passes and gets %d%% of the reclaim"
          % (100 * n1 // len(core), 100 * rec1 // tot))

    # --- per declared window, which is the M588 row's question ---------------
    fixed = collections.defaultdict(list)
    sidlim = {}
    for e in core:
        sidlim.setdefault(e.get("sid"), e.get("limit"))
    for sid, evs in by_sid.items():
        if sid not in sidlim:
            continue
        for e in evs:
            if e.get("event") == "model_call" and "sys_tok" in e:
                fixed[sidlim[sid]].append(e["sys_tok"] + e.get("tools_tok", 0))
    print("\n  BY DECLARED WINDOW  (the M588 row asks whether a GENEROUS one presses)")
    print("    %8s %8s %11s %9s %16s %7s" %
          ("window", "pressed", "unrelieved", "sessions", "fixed overhead", "% win"))
    for lim in sorted({e.get("limit") for e in core if e.get("limit")}):
        es = [e for e in core if e.get("limit") == lim]
        fx = fixed.get(lim, [])
        fm = int(statistics.median(fx)) if fx else 0
        print("    %8d %8d %11d %9d %16d %6.1f%%"
              % (lim, len(es), sum(1 for e in es if e.get("unrelieved")),
                 len({e.get("sid") for e in es}), fm, 100.0 * fm / lim))

    # --- what the elision floor could never reach ----------------------------
    sizes = []
    for sid, evs in by_sid.items():
        if sid not in sidlim:
            continue
        for e in evs:
            if e.get("event") == "tool_call" and "output_bytes" in e:
                sizes.append(e["output_bytes"])
    if sizes:
        below = [s for s in sizes if s < ELIDE_MIN_BYTES]
        print("\n  tool results on sessions that pressed: %d" % len(sizes))
        print("    below the %d-byte elision floor: %d (%d%% of items, %d%% of volume)"
              % (ELIDE_MIN_BYTES, len(below), 100 * len(below) // len(sizes),
                 100 * sum(below) // max(sum(sizes), 1)))
        print("    median result %d bytes" % int(statistics.median(sizes)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
