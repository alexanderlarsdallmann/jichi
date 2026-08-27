# Audio transcription (`transcribe_audio`)

jichi can turn speech into text: the `transcribe_audio` tool uploads a workspace
audio file to an OpenAI-compatible transcription endpoint and returns the
transcript. This completes the media story — jichi consumes images (M29 vision),
generates images + speech (M32), and now consumes audio (M33).

The tool is **off until you configure a backend**, **read-only** (it never
writes files — it reads the audio file through the path fence and returns text),
and its input is **size-capped**.

## Configuration — the `transcribe` model role

Add a model entry with the `transcribe` role (like `embed`/`image`/`audio`); its
`apiBase`/`apiKey`/`model` back the `POST {apiBase}/v1/audio/transcriptions`
call. The tool is registered only when some model declares the role.

```jsonc
{
  "models": [
    { "name": "chat", "provider": "openai", "model": "...", "roles": ["chat"] },

    { "name": "stt", "provider": "openai", "model": "whisper-1",
      "apiBase": "https://api.openai.com", "apiKeyEnv": "OPENAI_API_KEY",
      "roles": ["transcribe"] }
  ],
  "transcribeMaxBytes": 0   // 0 => built-in cap (25 MB)
}
```

`jichi doctor` notes when a `transcribe`-role model is present. A single local
server (e.g. a whisper.cpp server) can serve this and the other roles — just
point the role model at it.

## Tool

| Tool | Args | Behaviour |
|---|---|---|
| `transcribe_audio` | `path` (req), `language` (opt) | Reads the audio file at `path`, uploads it, and returns the transcript text. `language` is an optional ISO-639-1 hint (e.g. `"en"`). |

Supported extensions (the OpenAI/Whisper set): `.mp3`/`.mpga`/`.mpeg`, `.wav`,
`.m4a`/`.mp4`, `.flac`, `.ogg`/`.oga`, `.webm`. Errors (no role model,
unsupported extension, oversized file, fence denial, HTTP/provider failure) come
back as a tool error, never a crash.

## Wire shape

The request is `multipart/form-data` with a `model` field, a `response_format:
json` field, an optional `language` field, and a `file` part carrying the audio
bytes (labelled with the right `Content-Type`). The response is
`{"text": "..."}` (the verbose-json shape also carries a top-level `text`), and
that text becomes the tool result.

Only the OpenAI-compatible shape is supported in v1; it matches OpenAI and the
local servers that mimic it.

## Safety

- **Path fence:** the audio path is read through the same M24 fence as every file
  read; a path outside the workspace is refused.
- **Size cap:** a file larger than `transcribeMaxBytes` (built-in 25 MB, OpenAI's
  limit) is rejected before any upload.
- **Read-only:** the tool writes nothing; it returns text.
- **Abort:** an interrupted turn cancels the in-flight upload.

## Implementation

- `src/net/jc_transcribe.{c,h}`: the pure `jc_transcribe_parse` (extract `text`)
  + `jc_transcribe_run` (build the multipart body, POST via `jc_http_perform`,
  parse the response).
- `src/util/jc_multipart.{c,h}`: a pure, unit-tested `multipart/form-data` body
  builder — the HTTP layer takes an opaque body + Content-Type, so multipart is
  just a body the caller assembles (no libcurl-mime coupling). Binary-safe (file
  parts carry an explicit length).
- `src/util/jc_audio.{c,h}`: `jc_audio_media_type` (extension → IANA type), the
  mirror of `jc_image_media_type`.
- `src/tools/jc_tool_transcribe.c`: the tool; the `transcribe` role in
  `include/jc_config.h`; registration in `src/main.c`.
- Tests: `tests/test_multipart.c`, `tests/test_transcribe.c`, and the
  loopback-mock e2e `tests/e2e/transcribe.py`.

## Other audio surfaces (M33b)

Beyond the tool, two surfaces route audio through the same transcription path:

- **`@audio:<path>`** in a chat message transcribes that file and inlines the
  transcript as referenced context (a graceful no-op when no transcribe model is
  configured). See [REFERENCES.md](REFERENCES.md).
- **ACP audio prompt blocks** — when a transcribe-role model exists, jichi
  advertises `promptCapabilities.audio` and transcribes `{type:"audio"}` blocks
  from `session/prompt`, folding the text into the user message. See
  [ACP.md](ACP.md).
