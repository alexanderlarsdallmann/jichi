# Vision input (M29)

jichi can attach **images** to a turn so a multimodal model can see them — from the
CLI, the TUI, and an ACP editor. (Audio stays out of scope.)

## Enabling a vision model

Vision is opt-in per model via a config flag — jichi never guesses from the model
id:

```jsonc
{ "models": [
    { "name": "claude", "provider": "anthropic", "model": "claude-...",
      "vision": true }
] }
```

If you attach an image to a turn whose active model is **not** `vision: true`,
the image is **dropped with a warning** (the turn still runs on the text) rather
than sending an invalid request. `/status` shows `vision: yes/no`.

## Attaching images

- **CLI / headless:** `--image <path>` (repeatable):
  ```sh
  jichi --image diagram.png -p "what does this diagram show?"
  ```
- **In any message (TUI or `-p`):** an `@`-reference whose path has an image
  extension attaches as an image (not inlined as text), or use the explicit
  `@img:<path>` form for any extension:
  ```
  what's wrong with @screenshot.png ?
  summarize @img:/abs/path/chart.webp
  ```
- **ACP editor:** when the active model is vision-capable, jichi advertises
  `promptCapabilities.image: true`; the editor's image prompt blocks
  (`{type:"image", data, mimeType}`) are attached to the turn. See docs/ACP.md.

Supported types: PNG, JPEG, GIF, WebP. Each image is capped at 5 MB; file reads
go through the **M24 path fence** (so an autonomous run can't pull in images from
outside the workspace).

## How it works

Images live on the message as base64 (`struct jc_image` on `jc_message`); the
two providers serialize a user message that carries images as a typed **content
array** — Anthropic `{type:"image",source:{type:"base64",…}}`, OpenAI
`{type:"image_url",{url:"data:…;base64,…"}}`. Within a live session the images
stay in memory and are re-sent each turn (the APIs are stateless), so the model
can refer back to them.

**Persistence limitation:** images are turn-ephemeral. A saved session records a
`[image: <media_type>]` placeholder, not the base64 bytes (which would bloat the
session file), so a `--resume`d session notes that images were sent without
re-sending them.

## Implementation

- Pure cores: `jc_base64` (`src/util/jc_base64.c`) + `jc_image_media_type`
  (`src/util/jc_image.c`) — M29a, unit-tested.
- Data model + loader: `struct jc_image` + helpers in `src/chat/jc_message.c`;
  `jc_app_load_image` (read via the fence + base64) in `src/chat/jc_app.c` — M29b.
- Serializers: `src/provider/jc_provider_anthropic.c` /
  `jc_provider_openai.c` — M29b.
- Surfaces: `--image` + `hl_attach_images` (`src/main.c`), `@`-image refs
  (`src/command/jc_refs.c`, `JC_REF_IMAGE` + `jc_refs_attach_images`), the
  `vision` capability (`src/config/jc_config.c`) — M29c.
- ACP: `jc_acp_prompt_images` + advertised capability (`src/acp/`) — M29d.

Tests: `tests/test_base64.c`, `tests/test_vision.c`, `tests/test_acp.c`, and the
E2E `tests/e2e/vision.py`.
