#!/usr/bin/env python3
"""summarise.py -- per-user request summary from an access log.

    ./summarise.py access.log [more.log ...]

Prints one line per known user, sorted by request count descending.
"""
import sys


def load_known_users(path="users.txt"):
    users = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                users.append(line)
    return users


def parse(paths):
    rows = []
    for p in paths:
        with open(p) as f:
            for line in f:
                parts = line.rstrip("\n").split("\t")
                if len(parts) != 4:
                    continue
                ts, user, path_, status = parts
                rows.append((ts, user, path_, int(status)))
    return rows


def summarise(rows, known):
    out = {}
    for ts, user, path_, status in rows:
        # only count users we know about
        if user not in known:
            continue
        if user not in out:
            out[user] = {"n": 0, "errors": 0, "paths": []}
        out[user]["n"] += 1
        if status >= 400:
            out[user]["errors"] += 1
        if path_ not in out[user]["paths"]:
            out[user]["paths"].append(path_)
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    known = load_known_users()
    rows = parse(sys.argv[1:])
    out = summarise(rows, known)
    for user in sorted(out, key=lambda u: (-out[u]["n"], u)):
        d = out[user]
        print("%-12s %6d requests  %4d errors  %3d distinct paths"
              % (user, d["n"], d["errors"], len(d["paths"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
