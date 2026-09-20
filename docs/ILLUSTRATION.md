# Pictures in this documentation: what may be captured, what may be generated

*A rule, a setup, and a measurement (M680). Read the rule first — it is short,
and the setup is only useful to someone who has accepted it.*

---

## 1. The rule

**An image that asserts "this is what the program printed" must be captured
from a run. An image that asserts nothing may be generated.**

That is the whole of it. The rest of this page is why, how, and what happened
when this project tried.

### Why a generated screenshot is not a screenshot

A screenshot is a **record**. It says: *this happened, on a machine, at a time.*
An image model does not record — it produces something plausible. An image that
looks like jichi's terminal output but shows text jichi never printed is a
**fabricated record**, and it belongs in the same category as an invented
citation or a hand-typed test result. This project refuses those elsewhere; it
refuses this.

The trouble is that it would *work*. A reader skims a screenshot; they do not
diff it against a transcript. A fabricated one would be believed precisely
because nobody checks a picture — which is the argument for the rule, not
against it.

### The four kinds, and which is which

| Kind | How it is made | What it may claim |
|---|---|---|
| **Terminal capture** | run the real command, keep the real bytes | *"this is what it printed"* |
| **Annotated capture** | a capture plus arrows or labels drawn on it | the same, with emphasis |
| **Diagram** | mermaid, as everywhere else in this tree | a structure, never a run |
| **Illustration** | generated | **nothing factual** |

A generated image in this tree carries a visible marker saying it was generated,
the same way a bibliography entry carries its verification marker. An unmarked
image is a claim.

---

## 2. Capturing, which is the part that works

Nothing new was needed: `tests/tools/ptydrive` already drives jichi on a **real
pseudo-terminal** and logs exactly the bytes a terminal would receive, colour
and all.

### The recipe, run in the form published

```sh
make smoke-tools                       # builds tests/tools/ptydrive

printf 'waitexit 20\nassertexit 0\n' > /tmp/cap.pd
HOME=/tmp/cleanhome TERM=xterm-256color \
  tests/tools/ptydrive --deadline 30 --cols 96 --log /tmp/doctor.raw \
      /tmp/cap.pd -- ./jichi doctor

python3 scripts/ansi-to-png.py /tmp/doctor.raw docs/images/doctor.png \
      --cols 96 --title "jichi doctor — a fresh machine, before setup"
```

### Three things that matter more than they look

**Capture under an isolated `HOME`.** The first capture of `doctor` made here
showed the author's real home path, a 278 MB session store and the institution's
gateway URL — one person's machine, not what a reader would see. (The first
draft of this very paragraph quoted that path verbatim, and shipped. It was
caught by `snapshot_lint`'s check 9 — but only after the check was repaired:
see the note on the corpus below.) A documentation image should
show *the reader's* situation, which for `doctor` means a machine **before
setup**, where the warnings are the onboarding ones.

**Let the renderer wrap, do not let it clip.** `doctor`'s detail lines run past
the terminal width and a real terminal soft-wraps them. The first version of
`ansi-to-png.py` sized the image to the declared column count and **cut the
overflow off** — a screenshot that misreports what was printed. The second sized
to the longest line and produced a 3,466-pixel picture of 96-column output. Only
the third, which wraps the way a terminal does, shows what a reader sees.

**Keep the transcript beside the image.** The capture is reproducible: the
command, its config and the captured bytes are all that is needed to get the
same picture again. **An image nobody can reproduce is decoration.**

---

## 3. Generating, and an honest measurement of it

### The setup

LocalAI, loopback only, serving an OpenAI-compatible API:

```sh
local-ai run --address 127.0.0.1:8090 \
    --models-path ~/development/localai/models \
    --backends-path ~/development/localai/backends
```

Four things that cost this project time, recorded in
[`ANECDOTES.md`](ANECDOTES.md) §13 and repeated here because they are the ones
that bite:

1. **Pass `--models-path` and `--backends-path`.** Run from a repository,
   LocalAI drops `backends/` and `data/` in the working directory — 7.3 GB of
   them, inside the project tree.
2. **`toolProfile: full`.** The lean `core` profile does not advertise the media
   tools, so a model "that cannot generate images" is usually a model that was
   never offered one.
3. **A model must declare `role: image`** in the config; jichi registers
   `generate_image` only when one does.
4. **Poll the backend registry, not the download job.** A backend registers a
   beat after its download reports 100%, and the first request 500s with
   `backend not found`.

### What it produced here, measured twice

**The plumbing works end to end.** `POST /v1/images/generations` returns 200 in
8–16 s and a fetchable PNG; jichi's `generate_image` writes into the path fence
like any other tool.

**The output was not usable.** Two attempts against `sd-1.5-ggml`:

| Attempt | Prompt asked for | What came back |
|---|---|---|
| 1 | a signpost at a crossroads beside a window frame, teal and amber | a clock-like disc on a flat orange field |
| 2 | the same, with a negative prompt and an explicit step count | an orange field with one small smudge |

So: **no generated image ships in this documentation today**, and the reason is
recorded rather than hidden. A decorative image that ignored its own brief is
padding, and padding in documentation costs the reader's trust in the images
that *are* real.

**What to try next**, for whoever picks this up: confirm the step-count
parameter this backend actually reads (attempt 2 finished in 8 s, which suggests
it ran very few steps whatever the request said); try an SDXL-class model rather
than SD 1.5; and judge the result against the brief rather than against
"looks like an image".

---

## 4. Where the pictures live

`docs/images/`, one directory, with the capture's transcript recoverable from
the recipe above. Today it holds exactly one file, and that file is a record.
