"""corpus_filter.py -- which runs in a jichi corpus are REAL model runs (M720).

WHY THIS EXISTS.  On 2026-09-23 the workstation's ~/.jichi.d held the corpus that
four register decisions and plan D1 wait on -- and 40 % of its post-M432 tool calls
(1,230 of 3,096) came from agent sessions that had run jichi against `mockmodel`
with the REAL home directory, so their telemetry and journals landed in the same
store as real work.  28 of the 32 journals carrying a stop_reason were synthetic.
Every script in tests/measure/ counted all of it as real.  Nothing had been priced
on it yet -- M710's compaction verdict was re-checked with the probes removed and
is unchanged, 248 core passes either way -- but DEFERRED item 7 counts capped
runs, and a mock cap test is exactly that shape.  So the population is classified
before any rate is computed, and every caller prints what it dropped and why.

THE RULE.  A telemetry session (`sid`) or a run (`run`) is
  SYNTHETIC   when (1) any of its model_call events names a model -- `model` or
              `model_id` -- containing "mock", case-insensitively (the smoke tier's
              write_config names its model "mock"), or (2) at least one of its
              model calls was answered (`ok` true) and EVERY answered call took
              less than SYNTHETIC_MAX_MS;
  UNANSWERED  when none of its model calls was answered -- a server that refused
              every call.  Kept, and counted apart: neither shown real nor fake,
              and it can have made no tool call;
  REAL        otherwise.
A journal is classified by its own `start.model` (M720+) when that names a mock,
and otherwise by the telemetry of its `run` id; a journal whose run has no
telemetry at all is UNVERIFIED -- kept, and counted apart.

THE THRESHOLD, MEASURED rather than chosen (workstation corpus, 2026-09-23, 243
sessions, 23,278 model_call events -- every one carrying `ok` and `latency_ms`):
the slowest answered call of any named-mock session took 1.25 ms (mockmodel
answers from memory over loopback); the fastest REAL session's slowest answered
call took 244.9 ms, the next ones 654, 688 and 826.  50 ms sits 40x above the one
and 5x below the other.  A real LLM call carries a ~16k-token system prompt and
the tool definitions, so its time to first token alone is above it.

WHAT IT CANNOT SEE.  A mock given a realistic delay on purpose passes as real: this
is a filter for accidents, not a defence against an adversary.  Hence the printed
reasons, and --include-synthetic on every caller, so a surprising number can be
traced to the rule rather than argued with.
"""

import collections
import glob
import json
import os

SYNTHETIC_MAX_MS = 50.0
DEFAULT_TELEMETRY = os.path.expanduser("~/.jichi.d/telemetry")


class _Stats(object):
    __slots__ = ("named_mock", "answered_max", "answered", "calls")

    def __init__(self):
        self.named_mock = False
        self.answered_max = None
        self.answered = 0
        self.calls = 0


def _kind(st):
    if st is None:
        return "unverified"
    if st.named_mock:
        return "synthetic"
    if st.answered == 0:
        return "unanswered"
    if st.answered_max < SYNTHETIC_MAX_MS:
        return "synthetic"
    return "real"


def _why(st):
    if st.named_mock:
        return "a model named mock"
    return "every answered call under %g ms" % SYNTHETIC_MAX_MS


class Population(object):
    """Model-call evidence per session and per run, fed one event at a time."""

    def __init__(self):
        self.by_sid = collections.defaultdict(_Stats)
        self.by_run = collections.defaultdict(_Stats)

    def feed(self, e):
        if not isinstance(e, dict) or e.get("event") != "model_call":
            return
        keys = [(self.by_sid, e.get("sid"))]
        if e.get("run"):
            keys.append((self.by_run, e.get("run")))
        mock = any("mock" in str(e.get(k) or "").lower()
                   for k in ("model", "model_id"))
        lat = e.get("latency_ms")
        answered = e.get("ok") is True and isinstance(lat, (int, float))
        for table, key in keys:
            st = table[key]
            st.calls += 1
            if mock:
                st.named_mock = True
            if answered:
                st.answered += 1
                if st.answered_max is None or lat > st.answered_max:
                    st.answered_max = lat

    def feed_files(self, files):
        for f in files:
            try:
                fh = open(f, errors="replace")
            except OSError:
                continue
            with fh:
                for line in fh:
                    if '"model_call"' not in line:
                        continue
                    try:
                        self.feed(json.loads(line))
                    except ValueError:
                        continue
        return self

    def session_kind(self, sid):
        return _kind(self.by_sid.get(sid)) if sid in self.by_sid else "unanswered"

    def run_kind(self, run, start_model=None):
        if start_model is not None and "mock" in str(start_model).lower():
            return "synthetic"
        if not run or run not in self.by_run:
            return "unverified"
        return _kind(self.by_run[run])

    def synthetic_sids(self):
        return set(s for s, st in self.by_sid.items() if _kind(st) == "synthetic")

    def session_report(self, include_synthetic=False):
        """One line naming the population and what was left out, with reasons."""
        kinds = collections.Counter(_kind(st) for st in self.by_sid.values())
        why = collections.Counter(_why(st) for st in self.by_sid.values()
                                  if _kind(st) == "synthetic")
        reasons = ", ".join("%s x%d" % (w, n) for w, n in sorted(why.items()))
        line = ("population: %d real sessions, %d synthetic (%s), %d with no "
                "answered model call" % (kinds["real"], kinds["synthetic"],
                                         reasons or "none", kinds["unanswered"]))
        if include_synthetic:
            return line + " -- synthetic INCLUDED (--include-synthetic)"
        return line + " -- synthetic sessions DROPPED (corpus_filter.py)"


def telemetry_files(paths):
    """Every *.jsonl under the given files/directories, recursively."""
    out = []
    for p in paths:
        if os.path.isdir(p):
            out.extend(sorted(glob.glob(os.path.join(p, "**", "*.jsonl"),
                                        recursive=True)))
        elif os.path.isfile(p):
            out.append(p)
    return out


def journal_report(kinds, include_synthetic=False):
    """kinds: iterable of run_kind() results, one per journal."""
    c = collections.Counter(kinds)
    line = ("population: %d journals of real runs, %d synthetic, %d unanswered, "
            "%d unverified (no telemetry for the run)"
            % (c["real"], c["synthetic"], c["unanswered"], c["unverified"]))
    if include_synthetic:
        return line + " -- synthetic INCLUDED (--include-synthetic)"
    return line + " -- synthetic journals DROPPED (corpus_filter.py)"
