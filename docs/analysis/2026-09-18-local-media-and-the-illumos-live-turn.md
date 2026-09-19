# A local speech and image stack, a live model turn on illumos, and eight corrections

**2026-09-18 (M663).** Two questions the operator asked in one sitting: *does
jichi compile and run on illumos now?* and *can speech and image generation run
locally instead of on a gateway?* Both are answered. What makes the session worth
a page is not the answers — it is that **eight things went wrong on the way, and
every one of them was found by running something rather than by reasoning about
it.** Four were my mistakes, three were misleading metadata, one was a trap this
project has a written note about and I walked into anyway.

---

## 1. Why local at all

The operator asked which HRZ gateway models suit speech and image generation.
The answer, read from LiteLLM's `/v1/model/info` rather than `/v1/models`
(the latter gives neither modes nor pricing):

| Mode | In the free `jlu/` namespace |
|---|---|
| chat | `qwen3-coder-next`, `qwen3.8-27b`, `gemma-4-26b-it`, `gpt-oss-20b` |
| embedding / rerank | `qwen3-embedding`, `jina-rerank` |
| audio transcription | `whisper-1` |
| audio speech | `tts-1-hd` — **but see below** |
| image generation | **none** |
| video generation | **none** |

**Two pricing traps, pointing in opposite directions.**

`jlu/tts-1-hd` declares `input_cost_per_token: 0.0` **and**
`input_cost_per_character: 3e-05`. It is free by every signal jichi checks and
bills per character. `CLAUDE.md`'s rule is *"they publish
`input_cost_per_token: 0.0` or no price at all"*, and `doctor` warns on **missing**
pricing — neither sees a per-character field. Whether HRZ actually bills it is a
question for HRZ; LiteLLM may be auto-attaching OpenAI's published price to a
model *named* `tts-1-hd`. jichi cannot tell the difference, and neither can we
from here.

The other way: all 81 image models are `openai/*`, and many show
`input_cost_per_token: 0`. **That number is meaningless for image models**, which
bill per image — `dall-e-3`'s record has `max_tokens: null` and every per-token
field zero while the real price lives elsewhere. It would be an easy way to talk
oneself into a priced run.

So image and video are not reachable without spending, and the field the project
uses to decide that cannot answer the question. Hence: local.

---

## 2. What decided the tool

`LocalAI`, and not because it is the best at any one thing. **jichi's
`generate_image`, `generate_audio` and `transcribe` tools already speak the
OpenAI-compatible API**, so LocalAI drops in as a backend with *no jichi changes*
— and the install can then be verified through jichi's own tools rather than
through `curl`, which is the difference between "the service works" and "the
feature works". `piper` and `stable-diffusion.cpp` are each better in isolation
and would each need a shim.

**The machine**, read from ROCm rather than from `lspci`:

    Marketing Name:  AMD Radeon RX 7800 XT
    Name:            gfx1101
    VRAM:            16368 MB
    ROCm:            7.2.2

*(Correction 1.* I first reported a 7700 XT with no ROCm. `lspci` prints
`Navi 32 [Radeon RX 7700 XT / 7800 XT]` — a combined device string — and my
output was cut at 100 characters, so I read the first name as the whole answer
and never checked for ROCm at all. `mem_info_vram_total` and `rocminfo` settle
both. A truncated read is not a short answer; it is a different one.)*

38 ROCm backends are offered, so the GPU is usable rather than a CPU fallback.

**Everything lives in `~/development/localai/`** — binary, backends, models — with
`start.sh` binding **loopback only**. Nothing needs re-downloading, and the bind
was verified by effect: `ss` reports `LISTEN 127.0.0.1:8090`, not `0.0.0.0`.
LocalAI's default is `:8080`, i.e. every interface, so loopback had to be asked
for explicitly.

---

## 3. The live model turn on illumos

Until this session every illumos check was **offline**: build, unit suite, smoke
drivers, `--version`, `doctor`, `describe`, `context`. "Does jichi run on
Solaris?" had a structural answer, not a live one.

LM Studio binds to `127.0.0.1` only, and the guest is behind QEMU's user-mode
NAT. Rather than exposing LM Studio on the LAN, the host opens an **ssh reverse
tunnel** into the guest, so the guest's own `localhost:1234` forwards back:

```sh
ssh -R 1234:127.0.0.1:1234 -i tier-v-key -p 2299 tierv@127.0.0.1
```

Then, on SunOS 5.11:

```
=== live turn on illumos ===
ILLUMOS LIVE OK
real    0m16.387s
```

**The first attempt failed, and the failure was a lie.** jichi printed
`error: provider error`; a `curl` from the guest printed the truth:

    Failed to load model "qwen/qwen2.5-coder-14b". Error: Failed to load model.

*(Correction 2.* VRAM contention — three models from the M651 translation work
were resident, about 10 GB of 16 GB. This is the exact shape of
`docs/ANECDOTES.md`-adjacent note *"an infrastructure failure is
indistinguishable from a negative verdict unless you look"*, which this project
wrote down after doing it three times in one day. Writing it up as an illumos
defect would have been effortless. The rule that saved it: isolate the subject,
one model resident.)*

---

## 4. The speech path, and six more corrections

The goal was never "LocalAI can speak" — it was **`generate_audio` can speak**.

*(Correction 3.* I installed `piper-en_US-lessac-medium-crispasr` because the
name starts with `piper`. It is packaged for **crispasr**, a speech
*recognition* backend, which duly crashed with exit 2. The gallery's TTS entries
are named `voice-*` and declare `backend: piper`; the backend is a field, not a
prefix.)*

*(Correction 4.* `backends/piper` is an **empty stub directory**; the installed
backend is `cpu-piper`. `Backend not found backend="piper"` while `ls` shows a
`piper/` directory is a confusing thing to be told, and the model YAML had to be
pointed at `cpu-piper`.)*

*(Correction 5.* `pkill -f 'local-ai run'` matched **my own shell wrapper** and
killed the session along with the service — bare exit 144, no output. This
project has a note titled exactly that, about `pgrep -f` matching the Bash
wrapper. Killing by pid from `pgrep -x` is the form that works.)*

With those fixed the endpoint returns real audio — 86,828 bytes, *RIFF WAVE,
16-bit mono 16 kHz*. But driving it **through jichi** failed differently:

*(Correction 6.* `qwen/qwen2.5-coder-14b` emitted the tool call as **markdown
JSON text** rather than a native `tool_calls` response, so nothing executed and
no file was written. That is the `text` vs `native` distinction M648's sweep
measured and `JC_TOOLPROBE_TEXT` classifies — a model capability, not a jichi or
LocalAI defect.)*

*(Correction 7.* Rather than guessing which model is native, ask the instrument
this project already built: `jichi doctor --live` reports
`✓ --live: tool calling observed "native"` for `gemma-4-12b`, `qwen3.5-9b` and
`bonsai-27b`. The 14B coder was the outlier.)*

**The result, with a native caller:**

```
$ jichi --config localai.json --auto -p 'Use the generate_audio tool to say: ...'
spoken.wav   83,244 bytes
RIFF (little-endian) data, WAVE audio, Microsoft PCM, 16 bit, mono 16000 Hz
```

Written by `generate_audio`, called by a local model, served on loopback. No
`curl` anywhere in that path — which is the whole point of the exercise.

---

## 5. What this says about the tools, not the models

Three of the eight corrections were **names that lied**: a TTS model prefixed
`piper` that was an ASR model, a backend directory that existed and was empty, a
`input_cost_per_token: 0` that meant "billed some other way". Two were **my own
shortcuts**: reading a truncated line as an answer, and a `pkill` pattern that
matched me. One was a **genuine capability difference** dressed as a failure, and
one was an **infrastructure failure** dressed as a capability verdict.

The pattern is the one this project keeps rediscovering: **a name, a field or an
exit code is a claim, and a claim is not a measurement.** Every correction above
came from running the next command and reading what came back — `ss` for the
bind, `file` for the audio, `curl` from the guest for the real error, `rocminfo`
for the GPU, `doctor --live` for the tool-calling mode.

## 6. What is not done

- ~~Image generation is installing as this is written.~~ **Done, and it is on the
  GPU.** `rocm-stablediffusion-ggml` (8.8 GB) plus `sd-1.5-ggml`, driven through
  `generate_image`:

      lighthouse.png   461,583 bytes   PNG 512x512 8-bit RGB   17.7 s cold
      boat.png         346,530 bytes   PNG 512x512 8-bit RGB    9.2 s warm

  **The GPU claim is from the clock, not the filename.** SD 1.5 at 512x512 is a
  minute or more on CPU; 9.2 s warm is the ROCm build doing the work. Confirmed
  independently by the backend registry, which aliases
  `stablediffusion-ggml -> rocm-stablediffusion-ggml`. And the images are real
  rather than a flat fill: 461 KB of PNG for a 786 KB raw frame is 59% of raw,
  where a uniform image compresses to under 1%.

  *(Correction 8, and it is the piper lesson generalised.* The model YAML declares
  `backend: stablediffusion-ggml` while the installed backend is
  `rocm-stablediffusion-ggml`. That resolved, because LocalAI aliases `rocm-X` to
  `X` -- which is exactly why piper failed: an **empty `backends/piper` stub
  directory** shadowed the alias that would otherwise have pointed at
  `cpu-piper`. The registry now reads `piper -> piper` for the stub and
  `stablediffusion-ggml -> rocm-stablediffusion-ggml` for the working one, side by
  side. Checking the alias table beats reading the directory listing.)*
- **`jlu/tts-1-hd`'s real price** is a question for the HRZ admins, not something
  measurable here.
- **jichi does not warn about per-character pricing.** `doctor` checks
  `inputCostPer1M`/`outputCostPer1M`; a gateway that bills per character or per
  image is invisible to it. That is a real gap and it is recorded in
  `DEFERRED.md` rather than fixed here.
- **LM Studio's three M651 models are unloaded** and must be restored:
  `llama-3-elyza-jp-8b`, `llm-jp-4-8b-thinking`,
  `text-embedding-nomic-embed-text-v1.5`.
