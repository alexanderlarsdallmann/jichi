#!/usr/bin/env python3
"""make_charts.py -- the case-study charts of the four jichi drives, as static SVG.

Reads the TSV tables in data/ (one row per landing, run, gate run, downtime span, lost-time set-back and
caught defect) and writes svg/*.svg. Standard library only; run it as `python3 -I make_charts.py <dir>`,
where <dir> holds data/ and receives svg/. Every number a chart shows is in the TSV it reads, so the table
beside each chart on the pages is the accessible form of the same data.

The colours are the four slots of a validated categorical palette (light and dark), checked for colour-blind
separation; each chart also labels its marks in text, so no identity rests on colour alone. Each SVG carries
its own light and dark colours and follows the reader's colour scheme.
"""
import csv
import os
import sys
from datetime import datetime, timezone, timedelta
from zoneinfo import ZoneInfo

BERLIN = ZoneInfo("Europe/Berlin")
DRIVES = ["hardening-1", "harness", "hardening-2", "harness-port"]
NAMES = {"hardening-1": "Hardening 1", "harness": "Harness", "hardening-2": "Hardening 2",
         "harness-port": "Harness port"}
CUT = datetime(2026, 11, 22, 17, 22, 29, tzinfo=timezone(timedelta(hours=1)))

STYLE = """<style>
.s{fill:#fcfcfb}.t1{fill:#0b0b0b}.t2{fill:#52514e}.tm{fill:#898781}
.g{stroke:#e1e0d9}.ax{stroke:#c3c2b7}.wash{fill:#898781;fill-opacity:.16}
.d0{fill:#2a78d6;stroke:#2a78d6}.d1{fill:#eb6834;stroke:#eb6834}.d2{fill:#1baf7a;stroke:#1baf7a}
.d3{fill:#eda100;stroke:#eda100}.ring{stroke:#fcfcfb}
.l0{fill:none;stroke:#2a78d6}.l1{fill:none;stroke:#eb6834}.l2{fill:none;stroke:#1baf7a}.l3{fill:none;stroke:#eda100}.good{fill:#0ca30c}.crit{fill:#d03b3b;stroke:#d03b3b}
.ink{fill:#52514e}.down{fill:#898781;fill-opacity:.5}
text{font-family:system-ui,-apple-system,"Segoe UI",sans-serif;font-variant-numeric:tabular-nums}
@media (prefers-color-scheme: dark){
.s{fill:#1a1a19}.t1{fill:#f0efec}.t2{fill:#c3c2b7}.g{stroke:#2c2c2a}.ax{stroke:#383835}
.d0{fill:#3987e5;stroke:#3987e5}.d1{fill:#d95926;stroke:#d95926}.d2{fill:#199e70;stroke:#199e70}
.d3{fill:#c98500;stroke:#c98500}.ring{stroke:#1a1a19}
.l0{stroke:#3987e5}.l1{stroke:#d95926}.l2{stroke:#199e70}.l3{stroke:#c98500}.ink{fill:#c3c2b7}}
</style>"""


def esc(s):
    return (str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;"))


def read(path):
    with open(path, newline="", encoding="utf-8") as f:
        return [r for r in csv.DictReader(f, delimiter="\t")]


def ts(s):
    if not s:
        return None
    s = s.strip().replace("Z", "+00:00")
    if " " in s and "T" not in s:
        s = s.replace(" ", "T", 1)
    d = datetime.fromisoformat(s)
    if d.tzinfo is None:
        raise ValueError("time without an offset: " + s)
    return d


def svg_open(w, h, title, desc):
    return ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" role="img">'
            % (w, h, w, h), "<title>%s</title><desc>%s</desc>" % (esc(title), esc(desc)), STYLE,
            '<rect class="s" x="0" y="0" width="%d" height="%d" rx="8"/>' % (w, h)]


def text(x, y, s, cls="t2", size=12, anchor="start", weight="400"):
    return ('<text class="%s" x="%.1f" y="%.1f" font-size="%d" text-anchor="%s" font-weight="%s">%s</text>'
            % (cls, x, y, size, anchor, weight, esc(s)))


def hbar(x0, y, length, thick, cls, tip):
    """A horizontal bar from the baseline x0: square at the baseline, a 4px rounded data end."""
    r = min(4.0, length / 2.0, thick / 2.0)
    x1 = x0 + length
    d = ("M%.1f,%.1f H%.1f Q%.1f,%.1f %.1f,%.1f V%.1f Q%.1f,%.1f %.1f,%.1f H%.1f Z"
         % (x0, y, x1 - r, x1, y, x1, y + r, y + thick - r, x1, y + thick, x1 - r, y + thick, x0))
    return '<path class="%s" d="%s" stroke-width="0"><title>%s</title></path>' % (cls, d, esc(tip))


def dot(x, y, cls, tip, r=4.5):
    return ('<circle class="%s ring" cx="%.1f" cy="%.1f" r="%.1f" stroke-width="2"><title>%s</title></circle>'
            % (cls, x, y, r, esc(tip)))


def fmt_h(h):
    return "%.1f h" % h


# ---------------------------------------------------------------- the day axis shared by the timelines
def days_between(t0, t1):
    d = t0.astimezone(BERLIN).date()
    while True:
        dt = datetime(d.year, d.month, d.day, tzinfo=BERLIN)
        if dt > t1:
            return
        if dt >= t0:
            yield dt
        d = d + timedelta(days=1)


def axis_days(out, x_of, t0, t1, y_top, y_bot, label_every):
    for i, dt in enumerate(days_between(t0, t1)):
        x = x_of(dt)
        if label_every > 2 and i % label_every:
            continue
        out.append('<line class="g" x1="%.1f" y1="%d" x2="%.1f" y2="%d" stroke-width="1"/>' % (x, y_top, x, y_bot))
        if i % label_every == 0:
            out.append(text(x, y_bot + 16, dt.strftime("%m-%d"), "tm", 11, "middle"))


# ---------------------------------------------------------------- 1. cumulative landings, all four drives
def burnup(D, out_dir):
    L = [r for r in D["landings"] if r["landed_at"]]
    down = [r for r in D["downtime"] if r.get("within_drive_span") == "yes" and r["end"]]
    runs = D["runs"]
    t0 = min(ts(r["start"]) for r in runs)
    t1 = CUT
    W, H = 920, 420
    x0, x1, yt, yb = 64, 760, 56, 360
    span = (t1 - t0).total_seconds()
    x_of = lambda t: x0 + (t - t0).total_seconds() / span * (x1 - x0)
    total = len(L)
    y_of = lambda n: yb - n / float(total) * (yb - yt)
    out = svg_open(W, H, "Landings on master, cumulative, four drives",
                   "Each step is one gated batch landing on master. Grey bands: the loop was parked, stopped or paused.")
    out.append(text(24, 30, "Landings on master over 35 days: four drives, %d landings" % total, "t1", 15, "start", "600"))
    for r in down:
        a, b = ts(r["start"]), ts(r["end"]) if r["end"] else CUT
        if b < t0 or a > t1:
            continue
        xa, xb = x_of(max(a, t0)), x_of(min(b, t1))
        out.append('<rect class="wash" x="%.1f" y="%d" width="%.1f" height="%d"><title>%s</title></rect>'
                   % (xa, yt, max(xb - xa, 1.0), yb - yt, esc("%s, %s: %s to %s (%s)" % (
                       NAMES.get(r["drive"], r["drive"]), r["kind"], a.astimezone(BERLIN).strftime("%m-%d %H:%M"),
                       b.astimezone(BERLIN).strftime("%m-%d %H:%M"), r.get("cause", "")))))
    for n in range(0, total + 1, 10):
        y = y_of(n)
        out.append('<line class="g" x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke-width="1"/>' % (x0, y, x1, y))
        out.append(text(x0 - 8, y + 4, str(n), "tm", 11, "end"))
    axis_days(out, x_of, t0, t1, yt, yb, 7)
    out.append('<line class="ax" x1="%d" y1="%d" x2="%d" y2="%d" stroke-width="1"/>' % (x0, yb, x1, yb))
    n = 0
    for k, dr in enumerate(DRIVES):
        rows = [r for r in L if r["drive"] == dr]
        rs = [ts(r["start"]) for r in runs if r["drive"] == dr]
        re_ = [ts(r["end"]) if r["end"] else CUT for r in runs if r["drive"] == dr]
        ds, de = min(rs), min(max(re_), CUT)
        pts = [(x_of(ds), y_of(n))]
        for r in rows:
            t = ts(r["landed_at"])
            pts.append((x_of(t), y_of(n)))
            n += 1
            pts.append((x_of(t), y_of(n)))
        pts.append((x_of(de), y_of(n)))
        d = "M" + " L".join("%.1f,%.1f" % p for p in pts)
        out.append('<path class="l%d" d="%s" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"/>' % (k, d))
        out.append(dot(pts[-1][0], pts[-1][1], "d%d" % k, "%s: %d landings, %s to %s" % (
            NAMES[dr], len(rows), ds.astimezone(BERLIN).strftime("%m-%d %H:%M"), de.astimezone(BERLIN).strftime("%m-%d %H:%M"))))
        if k < len(DRIVES) - 1:
            out.append(text(x_of(de) - 8, y_of(n) - 10, "%s: %d" % (NAMES[dr], len(rows)), "t2", 12, "end"))
        else:
            out.append(text(x_of(de) + 8, y_of(n) + 4, "%s: %d" % (NAMES[dr], len(rows)), "t2", 12))
    lx = x0
    for k, dr in enumerate(DRIVES):
        out.append('<rect class="d%d" x="%d" y="40" width="14" height="4" rx="2"/>' % (k, lx))
        out.append(text(lx + 20, 46, NAMES[dr], "t2", 12))
        lx += 130
    out.append('<rect class="wash" x="%d" y="38" width="14" height="10"/>' % lx)
    out.append(text(lx + 20, 46, "loop parked, stopped or paused", "t2", 12))
    out.append(text(x0, H - 14, "Times in Europe/Berlin. The harness-port drive is still running; counted to 2026-11-22 17:22.", "tm", 11))
    out.append("</svg>")
    write(out_dir, "landings-cumulative.svg", out)


# ---------------------------------------------------------------- 2. one timeline per drive
def timeline(D, out_dir, dr, k):
    runs = [r for r in D["runs"] if r["drive"] == dr]
    gates = [g for g in D["gates"] if g["drive"] == dr and g["start"] and g.get("in_drive") == "yes"]
    lands = [r for r in D["landings"] if r["drive"] == dr and r["landed_at"]]
    down = [r for r in D["downtime"] if r["drive"] == dr and r.get("within_drive_span") == "yes" and r["end"]]
    t0 = min(ts(r["start"]) for r in runs)
    t1 = min(max(ts(r["end"]) if r["end"] else CUT for r in runs), CUT)
    W = 920
    lanes = ["Lead runs", "Gate runs", "Landings", "Loop down"]
    x0, x1 = 110, 890
    yt = 60
    lh = 34
    H = yt + lh * len(lanes) + 56
    span = (t1 - t0).total_seconds()
    x_of = lambda t: x0 + (t - t0).total_seconds() / span * (x1 - x0)
    days = (t1 - t0).total_seconds() / 86400.0
    out = svg_open(W, H, "%s: a timeline" % NAMES[dr],
                   "Lead runs, gate runs (green or red), landings on master, and the spans the loop was down.")
    out.append(text(24, 30, "%s: %s to %s (%.1f days)" % (NAMES[dr], t0.astimezone(BERLIN).strftime("%Y-%m-%d %H:%M"),
                                                          t1.astimezone(BERLIN).strftime("%Y-%m-%d %H:%M"), days),
                    "t1", 15, "start", "600"))
    yb = yt + lh * len(lanes)
    axis_days(out, x_of, t0, t1, yt - 6, yb, 1 if days < 12 else 2)
    for i, name in enumerate(lanes):
        out.append(text(x0 - 10, yt + lh * i + lh / 2 + 4, name, "t2", 12, "end"))
    # runs: bars, 2px surface gap between neighbours
    y = yt + 8
    for r in runs:
        a = ts(r["start"]); b = ts(r["end"]) if r["end"] else CUT
        xa, xb = x_of(a), x_of(min(b, t1))
        w = max(xb - xa - 2, 1.0)
        out.append('<rect class="d%d" x="%.1f" y="%d" width="%.1f" height="%d" rx="2"><title>%s</title></rect>'
                   % (k, xa, y, w, lh - 16, esc("run %s: %s to %s, rc %s" % (
                       r["run"], a.astimezone(BERLIN).strftime("%m-%d %H:%M"),
                       b.astimezone(BERLIN).strftime("%m-%d %H:%M") if r["end"] else "running", r["rc"] or "-"))))
    # gates: green = filled circle, red = a cross; text legend below says which is which
    yg = yt + lh + lh / 2
    for g in gates:
        a = ts(g["start"])
        if a < t0 or a > t1:
            continue
        x = x_of(a)
        tip = "gate on %s: %s, %s%s" % (g["head_sha"], g["verdict"], a.astimezone(BERLIN).strftime("%m-%d %H:%M"),
                                        "" if g.get("on_ready_head") != "no" else " (a pushed head, not a READY head)")
        if g["verdict"] == "green":
            out.append(dot(x, yg, "good", tip, 4))
        else:
            out.append('<path class="crit" d="M%.1f,%.1f l7,7 m0,-7 l-7,7" stroke-width="2" fill="none" stroke-linecap="round"><title>%s</title></path>'
                       % (x - 3.5, yg - 3.5, esc(tip)))
    # landings: a triangle
    yl = yt + 2 * lh + lh / 2
    for r in lands:
        t = ts(r["landed_at"])
        x = x_of(t)
        out.append('<path class="ink" d="M%.1f,%.1f l6,10 h-12 Z"><title>%s</title></path>'
                   % (x, yl - 6, esc("landing %s: %s at %s, %s commits" % (r["n"], r["batch"], t.astimezone(BERLIN).strftime("%m-%d %H:%M"), r["commits"]))))
    # downtime bands
    yd = yt + 3 * lh + 8
    for r in down:
        a = ts(r["start"]); b = ts(r["end"]) if r["end"] else CUT
        if b < t0 or a > t1:
            continue
        xa, xb = x_of(max(a, t0)), x_of(min(b, t1))
        out.append('<rect class="down" x="%.1f" y="%d" width="%.1f" height="%d" rx="2"><title>%s</title></rect>'
                   % (xa, yd, max(xb - xa, 2.0), lh - 16, esc("%s: %s to %s (%s)" % (
                       r["kind"], a.astimezone(BERLIN).strftime("%m-%d %H:%M"), b.astimezone(BERLIN).strftime("%m-%d %H:%M"),
                       r.get("cause", "")))))
    ng = sum(1 for g in gates if g["verdict"] == "green")
    nr = len(gates) - ng
    ly = H - 16
    out.append(dot(x0, ly - 4, "good", "green gate", 4))
    out.append(text(x0 + 10, ly, "green gate (%d)" % ng, "t2", 12))
    out.append('<path class="crit" d="M%.1f,%.1f l7,7 m0,-7 l-7,7" stroke-width="2" fill="none"/>' % (x0 + 140, ly - 8))
    out.append(text(x0 + 154, ly, "red, invalid or stopped gate (%d)" % nr, "t2", 12))
    out.append('<path class="ink" d="M%.1f,%.1f l6,10 h-12 Z"/>' % (x0 + 350, ly - 10))
    out.append(text(x0 + 362, ly, "landing on master (%d)" % len(lands), "t2", 12))
    out.append('<rect class="down" x="%.1f" y="%.1f" width="14" height="10"/>' % (x0 + 530, ly - 9))
    out.append(text(x0 + 550, ly, "loop parked, stopped or paused", "t2", 12))
    out.append("</svg>")
    write(out_dir, "timeline-%s.svg" % dr, out)


# ---------------------------------------------------------------- 3. dot strips: hours and tokens per landing
def strip(D, out_dir, name, title, unit_label, value_of, fmt, log=False):
    rows = {dr: [] for dr in DRIVES}
    for r in D["landings"]:
        if not r["landed_at"]:
            continue
        v = value_of(r)
        if v is not None:
            rows[r["drive"]].append((v, r))
    allv = [v for dr in DRIVES for v, _ in rows[dr]]
    import math
    vmax = max(allv)
    W, x0, x1 = 920, 130, 860
    yt, rh = 62, 46
    H = yt + rh * len(DRIVES) + 48
    if log:
        vmin = min(v for v in allv if v > 0)
        cand = [m * 10 ** e for e in range(-3, 6) for m in (1, 2, 5)]
        dlo = max(c for c in cand if c <= vmin)
        dhi = min(c for c in cand if c >= vmax)
        lo, hi = math.log10(dlo), math.log10(dhi)
        x_of = lambda v: x0 + (math.log10(max(v, dlo)) - lo) / float(hi - lo) * (x1 - x0)
        ticks = [c for c in cand if dlo <= c <= dhi]
    else:
        step = nice_step(vmax)
        top = step * math.ceil(vmax / step)
        x_of = lambda v: x0 + v / float(top) * (x1 - x0)
        ticks = [i * step for i in range(int(top / step) + 1)]
    out = svg_open(W, H, title, "One dot per landing; the bar marks the median of each drive.")
    out.append(text(24, 30, title, "t1", 15, "start", "600"))
    yb = yt + rh * len(DRIVES)
    for t in ticks:
        x = x_of(t)
        out.append('<line class="g" x1="%.1f" y1="%d" x2="%.1f" y2="%d" stroke-width="1"/>' % (x, yt - 8, x, yb))
        out.append(text(x, yb + 16, fmt(t), "tm", 11, "middle"))
    out.append(text(x1, yb + 34, unit_label, "tm", 11, "end"))
    for k, dr in enumerate(DRIVES):
        yc = yt + rh * k + rh / 2
        vals = sorted(v for v, _ in rows[dr])
        out.append(text(x0 - 12, yc + 4, NAMES[dr], "t2", 12, "end"))
        for v, r in rows[dr]:
            out.append(dot(x_of(v), yc, "d%d" % k, "%s, landing %s (%s): %s" % (NAMES[dr], r["n"], r["batch"], fmt(v)), 4.5))
        if vals:
            m = median(vals)
            xm = x_of(m)
            out.append('<line class="ax" x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke-width="2"/>' % (xm, yc - 13, xm, yc + 13))
            out.append(text(xm, yc - 16, "median " + fmt(m), "t2", 11, "middle"))
    out.append("</svg>")
    write(out_dir, name, out)


def nice_step(vmax):
    import math
    raw = vmax / 6.0
    e = 10 ** math.floor(math.log10(raw))
    for m in (1, 2, 2.5, 5, 10):
        if m * e >= raw:
            return m * e
    return 10 * e


def median(v):
    v = sorted(v)
    n = len(v)
    return v[n // 2] if n % 2 else (v[n // 2 - 1] + v[n // 2]) / 2.0


# ---------------------------------------------------------------- 4. horizontal bars (lost time, catchers)
def bars(out_dir, name, title, subtitle, items, fmt, unit):
    """items: [(label, value, tip)], drawn in the given order, one colour (a single series)."""
    W = 920
    x0, x1 = 300, 820
    yt, rh, thick = 70, 32, 20
    H = yt + rh * len(items) + 40
    vmax = max(v for _, v, _ in items)
    out = svg_open(W, H, title, subtitle)
    out.append(text(24, 30, title, "t1", 15, "start", "600"))
    out.append(text(24, 50, subtitle, "t2", 12))
    out.append('<line class="ax" x1="%d" y1="%d" x2="%d" y2="%d" stroke-width="1"/>' % (x0, yt - 6, x0, yt + rh * len(items)))
    for i, (label, v, tip) in enumerate(items):
        y = yt + rh * i + (rh - thick) / 2.0
        L = (x1 - x0) * v / float(vmax)
        out.append(text(x0 - 10, y + thick / 2 + 4, label, "t2", 12, "end"))
        out.append(hbar(x0, y, max(L, 2.0), thick, "d0", tip))
        out.append(text(x0 + L + 8, y + thick / 2 + 4, fmt(v), "t1", 12))
    out.append(text(x1, H - 12, unit, "tm", 11, "end"))
    out.append("</svg>")
    write(out_dir, name, out)


def write(out_dir, name, lines):
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, name), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", name)


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else "."
    data, out_dir = os.path.join(base, "data"), os.path.join(base, "svg")
    D = {n: read(os.path.join(data, n + ".tsv")) for n in ("landings", "runs", "gates", "downtime")}
    burnup(D, out_dir)
    for k, dr in enumerate(DRIVES):
        timeline(D, out_dir, dr, k)

    def gate_to_land(r):
        a, b = ts(r["first_gate_start"]), ts(r["landed_at"])
        return None if not a or not b else (b - a).total_seconds() / 3600.0
    strip(D, out_dir, "first-gate-to-landing.svg", "From a batch's first gate to its landing on master (re-gates included)",
          "hours (log scale)", gate_to_land, lambda v: ("%.0f h" % v) if v >= 10 else ("%.1f h" % v), log=True)
    strip(D, out_dir, "output-tokens-per-landing.svg", "Output tokens the models wrote between two landings",
          "million output tokens", lambda r: int(r["out_tok"]) / 1e6 if r["out_tok"] else None,
          lambda v: "%.1f M" % v)

    for fname, svg, title, sub in (
            ("lost-time-stopped.tsv", "lost-time-stopped.svg",
             "Work lost while the team, or part of it, stood still: hours by cause",
             "wall clock or worker hours that produced nothing; deliberate holds by the operator are left out"),
            ("lost-time-delay.tsv", "lost-time-delay.svg",
             "Delay to master while the team kept working: hours by cause",
             "wall clock in which master, a push or a fix waited; the team went on working meanwhile")):
        p = os.path.join(data, fname)
        if os.path.exists(p):
            rows = read(p)
            rows.sort(key=lambda r: -float(r["hours"]))
            bars(out_dir, svg, title, sub,
                 [(r["cause_class"], float(r["hours"]), "%s: %s h in %s set-backs" % (r["cause_class"], r["hours"], r["rows"]))
                  for r in rows], lambda v: "%.1f h" % v, "hours, each stretch counted once per cause")
    cat = os.path.join(data, "catchers-classes.tsv")
    if os.path.exists(cat):
        rows = read(cat)
        rows.sort(key=lambda r: -int(r["count"]))
        bars(out_dir, "defects-by-catcher.svg", "Which check caught each defect first, four drives",
             "confirmed defects, by the check that caught each first. The supervisor's log is the fullest record, so its catches are over-counted",
             [(r["caught_by"], int(r["count"]), "%s: %s defects" % (r["caught_by"], r["count"])) for r in rows],
             lambda v: "%d" % v, "defects")


if __name__ == "__main__":
    main()
