# Media generation (`generate_image` / `generate_audio`)

jichi can create media on demand: two agent tools call OpenAI-compatible
generation endpoints and save the result into the workspace.

- `generate_image` — text → image (`POST {apiBase}/v1/images/generations`).
- `generate_audio` — text → speech (`POST {apiBase}/v1/audio/speech`).

Both are **off until you configure a backend**, **mutating** (so they are
permission-gated and respect the workspace path fence), and they **save to a
file and return the path** — never inline image/audio data (which would flood
the context).

## Configuration — model roles

The backends are selected by **model role**, exactly like `embed`/`rerank`: add
a model entry to your config with the `image` and/or `audio` role. Its
`apiBase`/`apiKey`/`model` are used for that endpoint, and fallback chains apply.

```jsonc
{
  "models": [
    { "name": "chat", "provider": "openai", "model": "...", "roles": ["chat"] },

    { "name": "img", "provider": "openai", "model": "dall-e-3",
      "apiBase": "https://api.openai.com", "apiKeyEnv": "OPENAI_API_KEY",
      "roles": ["image"] },

    { "name": "tts", "provider": "openai", "model": "tts-1",
      "apiBase": "https://api.openai.com", "apiKeyEnv": "OPENAI_API_KEY",
      "roles": ["audio"] }
  ],
  "imageGenMaxBytes": 0,   // 0 => built-in cap (16 MB)
  "audioGenMaxBytes": 0    // 0 => built-in cap (32 MB)
}
```

**When a backend fails, the tool says which failure it was (M500).** Both
`generate_audio` and `transcribe_audio` report the HTTP status, the endpoint, and
what it implies — a `5xx` explicitly states that the arguments are not the cause,
because the alternative (one sentence for every failure) makes a model retry the
call with a different path or format. `jichi doctor` still only proves the *role
is configured*; a role can be present and the server broken.

`generate_image` is registered only when some model declares `role: "image"`;
`generate_audio` only when some model declares `role: "audio"`. `jichi doctor`
notes when each role is present. A single local server can serve all roles —
just point the role models at it.

## Tools

| Tool | Args | Behaviour |
|---|---|---|
| `generate_image` | `prompt` (req), `path` (req), `size` (opt, e.g. `"1024x1024"`), `model` (opt), `source` (opt) | Generates an image and writes it to `path`. The extension (`.png`/`.jpg`/`.webp`) selects the output format. `model` picks a configured image model by name (see *Per-workflow model selection*); `source` is a workspace image to **edit** (see *Image editing*). |
| `generate_audio` | `text` (req), `path` (req), `voice` (opt) | Synthesizes speech and writes it to `path`. The extension (`.mp3`/`.wav`/`.opus`/`.aac`/`.flac`/`.pcm`) selects the format. |

### Per-workflow model selection

`generate_image` uses the **first** model declaring `role: "image"` by default.
Configure **several** image models and the agent (or you) can pick one per
workflow with the optional `model` argument — it resolves the same name/index
selector as `--model` and is validated to actually carry the `image` role. Give
each model an optional **`description`** to guide the choice; the tool's schema
enumerates the configured image models with their descriptions, and `/status`
lists them. Example:

```jsonc
"models": [
  { "name": "flux-schnell", "provider": "openai", "model": "flux.1-schnell",
    "apiBase": "http://127.0.0.1:8080/v1", "roles": ["image"],
    "description": "fast generalist — icons, illustrations, watercolors" },
  { "name": "anime", "provider": "openai", "model": "illustrious-xl",
    "apiBase": "http://127.0.0.1:8080/v1", "roles": ["image"],
    "description": "manga/anime, Danbooru-style tags" }
]
```

A prompt like *"generate a manga panel … (use the anime model)"* leads the agent
to call `generate_image` with `model: "anime"`.

### Image editing

Pass a `source` workspace image to **edit** it instead of generating from
scratch (img2img / FLUX **Kontext**). The source is read through the path fence,
**raw base64-encoded** (standard alphabet, no `data:` prefix), and sent to the
model as a `ref_images` entry. (The OpenAI-compatible images API — and LocalAI —
base64-decode each `ref_images` value directly, or fetch it if it is an
`http(s)://` URL; a `data:` URI would fail that decode and the source would be
silently ignored.) This requires an editing-capable image model (e.g.
`flux.1-kontext-dev`); point a `role: "image"` model at it and select it with
`model`. Use it for screenshot edits, recolours, and variations.

Each returns a short confirmation like `Saved generated image (12345 bytes) to
art/cube.png`. Errors (no role model, unsupported extension, oversized response,
HTTP/provider failure, fence denial) come back as a tool error, never a crash.

## Wire shapes

- **Image:** the request sends `response_format: "b64_json"` (plus `output_format`
  from the path extension and an optional `size`); the response
  `{"data":[{"b64_json": "..."}]}` is base64-decoded to the image bytes. If a
  backend instead returns `{"data":[{"url": "..."}]}`, jichi fetches that URL. When
  editing, an optional `ref_images: ["<raw-base64>"]` array carries the source
  image (raw standard base64 — no `data:` prefix; omitted entirely for plain
  text-to-image, so that request is unchanged).
- **Audio:** the request sends `{model, input, voice?, response_format}`; the
  success body is **raw binary audio** (an HTTP-error body is JSON), written to
  the file verbatim — so embedded NUL bytes are preserved.

Only the OpenAI-compatible shape is supported in v1; it works with OpenAI and the
many local servers that mimic these endpoints. A generic/remappable shape could
be layered on later without changing this path.

## Local backend (LocalAI)

> **LM Studio does not generate images.** It serves text/multimodal-*input* LLMs
> and embeddings over an OpenAI-compatible API but ships no `/v1/images/generations`
> endpoint and no diffusion backend. Use it (if you like) for chat/embeddings and
> run a separate image backend.

[**LocalAI**](https://localai.io) is a drop-in for jichi's wire format: it
implements `POST /v1/images/generations` with the same JSON, and runs SDXL /
SD 3.5 / FLUX through its `diffusers` and `stablediffusion-ggml` (GGUF) backends.
FLUX Kontext editing is supported on the same endpoint via `ref_images`, which is
what jichi's `source` argument drives.

### Setup A — prebuilt binary (no root, no Docker)

The quickest path on a CUDA host **without a container runtime** is the single
LocalAI binary; it downloads its GPU backends + models from a gallery at runtime.
`scripts/setup-localai.sh` automates it (download → run on `127.0.0.1:8080` →
install a TTS voice + an image model):

```sh
scripts/setup-localai.sh                 # install + run (foreground)
scripts/setup-localai.sh --dry-run       # preview
# override the gallery ids if you like:
LOCALAI_IMAGE=flux.1-schnell scripts/setup-localai.sh
```

> **Verified (v4.6.2, RTX 4070 Ti SUPER, 16 GB):** image generation works
> end-to-end through the binary — the CUDA `stablediffusion-ggml` backend
> auto-downloads and `sd-1.5-ggml` renders a 256×256 image in ~4 s on the GPU.
> Two rough edges to know about: (1) a freshly-installed backend registers a
> beat *after* its download job reports 100% — the first request can 500 with
> `backend not found`; just retry. (2) In this build the `sherpa-onnx` TTS
> backend failed to load (`run.sh` exit 2); prefer a `whisper`/`piper` variant
> that loads for you, or the Docker image (Setup B) for TTS. Run LocalAI from a
> throwaway directory: it drops `backends/` and `data/` in the **current working
> directory**.

### Setup B — Docker (CUDA host; simplest for the full backend set)

```sh
# 1. Run LocalAI with GPU support, bound to localhost.
docker run -p 8080:8080 --gpus all localai/localai:latest-gpu-nvidia-cuda-12

# 2. Install a model from the gallery (or `local-ai run <name>`).
#    e.g. flux.1-schnell (fast, Apache-2.0) or an SDXL diffusers model.

# 3. Smoke-test the endpoint directly (note whether it returns b64_json or url;
#    jichi handles both).
curl http://127.0.0.1:8080/v1/images/generations \
  -d '{"model":"flux.1-schnell","prompt":"a red cube","size":"512x512"}'
```

Then point a `role: "image"` model at it (keyless local server — omit `apiKey`):

```jsonc
{ "name": "flux-schnell", "provider": "openai", "model": "flux.1-schnell",
  "apiBase": "http://127.0.0.1:8080/v1", "roles": ["image"],
  "description": "fast generalist — icons, illustrations, watercolors" }
```

`jichi doctor` then reports *image-role model present (generate_image enabled)*.

### Recommended models (≈16 GB VRAM)

| Workflow | Model | Notes |
|---|---|---|
| Default / icons / watercolors / general illustration | **FLUX.1-schnell** | Apache-2.0, fast (1–4 steps). GGUF Q8 (`stablediffusion-ggml`) or `diffusers` fp8. |
| Best-quality illustration | FLUX.1-dev (Q8 GGUF) | Higher fidelity, slower, **non-commercial** license. |
| Manga / anime | **Illustrious XL** (SDXL) | `diffusers`, ~7 GB, Danbooru-tag prompts. |
| Pixel art | SDXL + pixel-art LoRA | SDXL renders crisp pixels; FLUX over-smooths. |
| Screenshot editing | **FLUX.1-Kontext-dev** | Editing via `source`/`ref_images`. |

LoRA / style selection (pixel-art, watercolor) is a LocalAI-side concern: define
one `image` model per styled checkpoint and they appear to jichi as additional
named models to pick from — no further jichi changes needed.

A copy-paste starter config lives in `examples/config.local-image.json`.

## Safety

- **Path fence:** the destination is checked with the same M24 fence as
  `write_file` before any API call; a path outside the workspace is refused.
- **Permissions:** both tools are mutating, so they go through the normal
  permission verdict (auto-approved under `--auto`/AUTO mode, like other mutating
  tools).
- **Size caps:** a response larger than `imageGenMaxBytes` / `audioGenMaxBytes`
  (built-in 16 MB / 32 MB) is rejected before anything is written.
- **Abort:** an interrupted turn cancels the in-flight generation request.

## Implementation

- Net modules (`src/net/jc_imagegen.{c,h}`, `src/net/jc_audiogen.{c,h}`): pure
  body builders + the image response parser (`jc_imagegen_parse`) and the
  path-extension format helpers, plus the I/O orchestration. The image path uses
  `jc_net_post_json` (JSON response); the audio path uses `jc_http_perform` with
  an explicit length because the body is binary.
- `jc_base64_decode` (`src/util/jc_base64.c`) — length-returning base64 decode
  (binary-safe; tolerates line-wrapped input), the inverse of the M29 encoder.
- Tools: `src/tools/jc_tool_imagegen.c`, `src/tools/jc_tool_audiogen.c`; the
  `image`/`audio` roles in `include/jc_config.h`; registration in `src/main.c`.
- Tests: `tests/test_imagegen.c`, `tests/test_audiogen.c`, the decode/round-trip
  cases in `tests/test_base64.c`, and the loopback-mock e2e
  `tests/e2e/imagegen.py` / `tests/e2e/audiogen.py`.
