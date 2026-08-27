#!/usr/bin/env python3
"""Generate deterministic .ipynb fixtures for the jichi x JupyterHub test bench.

WHY DETERMINISTIC.  The notebook section of the documentation makes a cost claim
("a notebook costs you N times what the same code costs"), and a cost claim that
cannot be re-derived is an anecdote.  Every byte here is reproducible: fixed cell
ids (nbformat >= 4.5 mints random ones otherwise), a fixed PRNG seed for the
image noise, no timestamps, no execution.

Three fixtures, chosen to bracket the one number that matters -- jichi's
JC_CAP_READ_DEFAULT of 256 KB:

  small.ipynb    5 cells, no outputs           -- well under the cap
  outputs.ipynb  5 cells, one big PNG output   -- over the cap, so read_file truncates
  large.ipynb    200 cells, no images          -- many cells, no binary

Each is also written as a jupytext-paired .py, because "use the paired .py" is
the recommendation and the reader deserves the size difference, not the advice
alone.
"""
import base64
import json
import os
import random
import struct
import sys
import zlib


def png_noise(side, seed=0):
    """A real, valid PNG of pseudo-random pixels -- deterministic, incompressible.

    Noise on purpose: a solid colour would compress to a few hundred bytes and
    would understate what a real plot or image output costs.
    """
    rnd = random.Random(seed)
    raw = bytearray()
    for _ in range(side):
        raw.append(0)                                    # filter byte: None
        raw.extend(rnd.randbytes(side * 3))              # RGB
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))
    ihdr = struct.pack(">IIBBBBB", side, side, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 6))
            + chunk(b"IEND", b""))


def cell(kind, src, cid, outputs=None):
    c = {"cell_type": kind, "metadata": {}, "source": src, "id": cid}
    if kind == "code":
        c["execution_count"] = None
        c["outputs"] = outputs or []
    return c


def notebook(cells):
    return {
        "cells": cells,
        "metadata": {
            "kernelspec": {"display_name": "Python 3", "language": "python",
                           "name": "python3"},
            "language_info": {"name": "python", "version": "3.11.0"},
        },
        "nbformat": 4,
        "nbformat_minor": 5,
    }


CODE = [
    "import pandas as pd\nimport numpy as np\n",
    "df = pd.read_csv('measurements.csv')\ndf.head()\n",
    "def clean(frame):\n"
    "    \"\"\"Drop rows with a missing sensor reading.\"\"\"\n"
    "    return frame.dropna(subset=['reading'])\n",
    "cleaned = clean(df)\nprint(len(cleaned), 'rows survived')\n",
    "cleaned.groupby('station')['reading'].mean().plot(kind='bar')\n",
]


def build_small():
    cells = [cell("markdown", "# Station readings\n\nA short analysis.\n", "md-0")]
    for i, src in enumerate(CODE):
        cells.append(cell("code", src, "code-%d" % i))
    return notebook(cells)


def build_outputs(png):
    b64 = base64.b64encode(png).decode("ascii")
    cells = [cell("markdown", "# Station readings\n\nWith a rendered figure.\n", "md-0")]
    for i, src in enumerate(CODE):
        outs = None
        if i == len(CODE) - 1:
            outs = [{
                "output_type": "display_data",
                "data": {"image/png": b64, "text/plain": ["<Figure size 640x480>"]},
                "metadata": {},
            }]
        cells.append(cell("code", src, "code-%d" % i, outs))
    return notebook(cells)


def build_large(n=200):
    cells = []
    for i in range(n):
        if i % 10 == 0:
            cells.append(cell("markdown", "## Step %d\n" % i, "md-%d" % i))
        else:
            cells.append(cell(
                "code",
                "step_%d = cleaned[cleaned.station == %d]['reading'].mean()\n"
                "print('step %d ->', step_%d)\n" % (i, i, i, i),
                "code-%d" % i))
    return notebook(cells)


def write(path, nb):
    # sort_keys + a trailing newline: byte-stable across runs and machines.
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(nb, fh, indent=1, sort_keys=True, ensure_ascii=False)
        fh.write("\n")
    return os.path.getsize(path)


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    # 280x280 RGB noise -> a PNG of roughly a quarter megabyte, so that
    # outputs.ipynb lands clearly ABOVE jichi's 256 KB read cap.
    png = png_noise(280, seed=1729)
    made = []
    for name, nb in (("small", build_small()),
                     ("outputs", build_outputs(png)),
                     ("large", build_large())):
        p = os.path.join(outdir, name + ".ipynb")
        made.append((name, p, write(p, nb)))

    # The paired .py, so the recommendation carries its own evidence.
    paired = {}
    try:
        import jupytext
        for name, p, _ in made:
            nb = jupytext.read(p)
            q = os.path.join(outdir, name + ".py")
            jupytext.write(nb, q, fmt="py:percent")
            paired[name] = os.path.getsize(q)
    except Exception as exc:                                  # noqa: BLE001
        print("jupytext unavailable (%s) -- .py pairs not written" % exc,
              file=sys.stderr)

    cap = 256 * 1024
    lines = ["# Notebook fixtures -- sizes, measured\n",
             "Generated by `jhub-make-fixtures.py`; byte-stable across runs.\n",
             "jichi's `JC_CAP_READ_DEFAULT` is **%d bytes (256 KB)**.\n" % cap,
             "| fixture | .ipynb bytes | paired .py bytes | ratio | over the 256 KB read cap? |",
             "|---|---:|---:|---:|---|"]
    for name, p, size in made:
        py = paired.get(name)
        ratio = ("%.1fx" % (size / py)) if py else "-"
        lines.append("| `%s.ipynb` | %d | %s | %s | %s |" %
                     (name, size, py if py else "-", ratio,
                      "**yes**" if size > cap else "no"))
    lines.append("")
    with open(os.path.join(outdir, "SIZES.md"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))
    for line in lines:
        print(line)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "fixtures")
