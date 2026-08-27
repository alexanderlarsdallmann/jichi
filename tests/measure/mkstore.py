#!/usr/bin/env python3
"""Synthesize a jichi session store of a known size (M197). A measurement
helper, never a CI gate.

Writes `--files` valid session JSON files into <home>/.jichi.d/sessions, each
exactly `--bytes` bytes, so the total store size is an exact independent
variable for tests/measure/session_scan.py.

The JSON shape mirrors jc_session_save (src/session/jc_session.c:182-237) and
is read back by jc_session_list (:331-383) and load_from_text (:239-300).

Invariants that are NOT optional (each cost a debugging session to learn):

  * The filename stem MUST equal the "sessionId" field. jc_session_list finds
    files by their .json suffix (:349) but session_path (:23) rebuilds the path
    from sessionId, so a mismatch makes /sessions look fine while every
    /resume reports "no session matching".
  * All ids share a 24-char prefix, so `/resume `+Tab hits the common-prefix
    branch in jc_term (src/tui/jc_term.c:872-889) and prints NOTHING -- the
    full scan cost with no output flood to drain.
  * Exactly one file carries an alias. Two would make jc_session_resolve_alias
    return -2 (ambiguous) and kill the 3-scan /resume variant.
  * Padding is drawn from [a-z ] only, so json.dumps escapes nothing and the
    byte count equals the character count. Sizing is then one pass, not a
    binary search.
  * mtimes are set explicitly, descending with the index, so the newest-first
    sort (meta_cmp_desc, :318, keyed on jc_file_mtime) is deterministic and
    bare /resume has a known target.

Usage:
  python3 tests/measure/mkstore.py --home /tmp/jc-A --workspace /tmp/jc-ws-A \
      --files 50 --bytes 4096 --match 1 --alias wip

Importable:
  from mkstore import build_store
  info = build_store(home, workspace, files=50, bytes_each=4096, match=1)
"""
import argparse
import json
import os
import math
import random
import shutil
import sys

# A fixed v4-shaped id with a 24-char common prefix; %012d keeps 36 chars total.
ID_FMT = "00000000-0000-4000-8000-%012d"
ALIAS_DEFAULT = "wip"
# mtimes descend from a fixed epoch so runs are byte-identical and orderable.
MTIME_BASE = 1750000000


def _session_obj(sid, title, workspace, alias, pad, nmsg=2):
    """Build the dict in jc_session_save's key order (cosmetic, but faithful).

    `nmsg` splits the padding across that many messages. It matters far more than
    it looks (M200): the transient cost of LISTING a store is driven by the number
    of cJSON NODES, not by its bytes. Measured on 243 files of ~70 KB each:
    2 messages/file peaks at 9.3 MB, 200 at 14.2 MB, 2000 at 115 MB -- and this
    developer's real store (243 files, 17 MB, dozens of messages and tool calls
    each) peaks at 193 MB. The original fixture used 2 messages and therefore had
    realistic bytes but ~100x too few nodes, under-reporting this by an order of
    magnitude."""
    obj = {"sessionId": sid, "title": title}
    if alias is not None:
        obj["alias"] = alias
    obj["workspaceDirectory"] = workspace
    obj["mode"] = "chat"
    if nmsg <= 2:
        obj["history"] = [
            {"role": "user", "content": "q"},
            {"role": "assistant", "content": pad},
        ]
    else:
        # Distribute `pad` EXACTLY across nmsg messages: the first nmsg-1 get an
        # equal share, the last absorbs the remainder. Empty contents are allowed
        # (they must be, or the empty-pad overhead probe in _pad_for would not
        # match the filled version and exact sizing would drift).
        per = len(pad) // nmsg
        parts = [pad[k * per:(k + 1) * per] for k in range(nmsg - 1)]
        parts.append(pad[(nmsg - 1) * per:])
        obj["history"] = [
            {"role": "user" if k % 2 == 0 else "assistant", "content": parts[k]}
            for k in range(nmsg)
        ]
    return obj


def _dumps(obj):
    # separators: no spaces, matching cJSON_PrintUnformatted (M140).
    return json.dumps(obj, separators=(",", ":"))


def _pad_for(sid, title, workspace, alias, target, nmsg=2):
    """Exact-size targeting in one pass.

    Every field is ASCII with no JSON escapes, so len(dumps) grows 1:1 with the
    padding length. Returns (pad, actual_len).
    """
    empty = _dumps(_session_obj(sid, title, workspace, alias, "", nmsg))
    overhead = len(empty)
    pad_len = target - overhead
    if pad_len < 0:
        raise ValueError(
            "--bytes %d is below the %d-byte minimum for this session shape"
            % (target, overhead))
    pad = "a" * pad_len
    text = _dumps(_session_obj(sid, title, workspace, alias, pad, nmsg))
    return pad, len(text)


def _assert_safe(home, sessions):
    """Never touch the developer's real store."""
    real_home = os.path.realpath(home)
    if real_home == os.path.realpath(os.path.expanduser("~")):
        raise SystemExit("mkstore: refusing to write into the real HOME (%s)"
                         % real_home)
    if os.path.isdir(sessions):
        stray = [n for n in os.listdir(sessions) if n.endswith(".json")]
        if stray:
            raise SystemExit(
                "mkstore: %s already holds %d .json file(s); refusing to mix "
                "stores. Remove it first or pass a fresh --home."
                % (sessions, len(stray)))


def build_store(home, workspace, files=50, bytes_each=4096, match=1,
                alias=ALIAS_DEFAULT, seed=1, quiet=False, vary=0.0,
                nmsg=2):
    """Create <home>/.jichi.d/sessions with `files` sessions of `bytes_each`.

    `match` is how many sessions carry the real workspace path (so /sessions
    lists them and bare /resume can find one); the rest get a nonexistent one.
    The alias, if any, goes on the NEWEST matching session.

    Returns a dict manifest: files, bytes_total, bytes_each, sessions_dir,
    newest_id, alias_id, ids.
    """
    sessions = os.path.join(home, ".jichi.d", "sessions")
    _assert_safe(home, sessions)

    need = files * bytes_each
    free = shutil.disk_usage(os.path.dirname(os.path.abspath(home)) or "/").free
    if free < need * 4:
        raise SystemExit(
            "mkstore: need ~%d bytes (4x headroom); only %d free"
            % (need * 4, free))

    ws_real = os.path.realpath(workspace)
    random.seed(seed)
    os.makedirs(sessions, exist_ok=True)
    os.chmod(sessions, 0o700)  # mirrors jc_make_private (jc_session.c:193)

    ids = []
    total = 0
    for i in range(files):
        sid = ID_FMT % i
        # M200: optional size VARIANCE. A store of uniform files lets each
        # parse/free cycle reuse the previous one's heap blocks almost perfectly,
        # so a uniform fixture reports a flat peak even when a real store (files
        # from ~130 B to ~2.2 MB) fragments the allocator badly. `vary` spreads
        # sizes log-uniformly over [bytes_each/(1+vary*9), bytes_each*(1+vary*9)].
        this_bytes = bytes_each
        if vary > 0.0:
            lo = max(200.0, bytes_each / (1.0 + vary * 9.0))
            hi = bytes_each * (1.0 + vary * 9.0)
            this_bytes = int(math.exp(random.uniform(math.log(lo),
                                                    math.log(hi))))
        # i == 0 is the newest (mtime descends with i) and carries the alias.
        this_alias = alias if (i == 0 and alias) else None
        this_ws = ws_real if i < match else ("/nonexistent/ws-%d" % i)
        title = "synth session %d" % i
        pad, actual = _pad_for(sid, title, this_ws, this_alias, this_bytes,
                               nmsg)
        if actual != this_bytes:
            raise SystemExit(
                "mkstore: size targeting failed for %s: wanted %d, got %d"
                % (sid, this_bytes, actual))
        text = _dumps(_session_obj(sid, title, this_ws, this_alias, pad,
                                   nmsg))
        # The filename stem MUST equal sessionId (see the module docstring).
        path = os.path.join(sessions, sid + ".json")
        with open(path, "w") as f:
            f.write(text)
        os.chmod(path, 0o600)
        t = MTIME_BASE - i * 3600
        os.utime(path, (t, t))
        ids.append(sid)
        total += actual

    info = {
        "files": files,
        "bytes_each": bytes_each,
        "bytes_total": total,
        "sessions_dir": sessions,
        "match": match,
        "newest_id": ids[0] if ids else None,
        "alias": alias if (alias and files) else None,
        "alias_id": ids[0] if (alias and files) else None,
        "ids": ids,
    }
    if not quiet:
        print("mkstore: %d files, %d bytes total (%d each), %d matching %s"
              % (total and files or files, total, bytes_each, match, ws_real))
        print("mkstore: newest %s%s"
              % (info["newest_id"],
                 (", alias '%s'" % alias) if info["alias"] else ""))
        print("mkstore: dir %s" % sessions)
    return info


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--home", required=True,
                    help="isolated HOME; the store goes in <home>/.jichi.d/sessions")
    ap.add_argument("--workspace", required=True,
                    help="workspaceDirectory value for the matching sessions")
    ap.add_argument("--files", type=int, default=50)
    ap.add_argument("--bytes", type=int, default=4096, dest="bytes_each",
                    help="exact size of every session file")
    ap.add_argument("--match", type=int, default=1,
                    help="how many sessions claim the real workspace")
    ap.add_argument("--alias", default=ALIAS_DEFAULT,
                    help="alias for the newest session ('' for none)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--messages", type=int, default=2, dest="nmsg",
                    help="messages per session (M200: the LISTING peak scales "
                         "with cJSON node count, not bytes -- 2 vs 2000 is "
                         "9 MB vs 115 MB on the same store size)")
    ap.add_argument("--vary", type=float, default=0.0,
                    help="0..1 size variance around --bytes (M200: a UNIFORM "
                         "store lets each parse reuse the last one's heap "
                         "blocks and hides allocator fragmentation)")
    args = ap.parse_args()

    if args.match > args.files:
        raise SystemExit("mkstore: --match cannot exceed --files")
    build_store(args.home, args.workspace, args.files, args.bytes_each,
                args.match, args.alias or None, args.seed, vary=args.vary,
                nmsg=args.nmsg)
    return 0


if __name__ == "__main__":
    sys.exit(main())
