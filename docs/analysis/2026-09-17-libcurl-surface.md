# What jichi uses libcurl for, and whether writing our own would be viable

*2026-09-17 (M648). The operator asked two questions: which features of libcurl
does jichi actually use, and would a minimal in-house HTTP client be viable?
The first is a count and is answered exactly. The second is answered **no**, on
a measurement that already exists — and the reason is not "HTTP is hard" but
that the footprint the idea is chasing **is not in the part we would replace**.*

---

## 1. The surface, counted

Every call is in **one translation unit**. `grep -rl 'curl_' src/ include/`
returns exactly `src/net/jc_http.c` and `include/jc_http.h` — **953 + 200
lines**. There is no second door.

**Eleven distinct functions**, in descending use:

| Function | Calls |
|---|--:|
| `curl_easy_setopt` | 36 |
| `curl_easy_getinfo` | 5 |
| `curl_easy_strerror` | 4 |
| `curl_slist_append` / `curl_slist_free_all` | 2 / 2 |
| `curl_easy_perform` | 2 |
| `curl_easy_cleanup` | 2 |
| `curl_global_init` / `curl_global_cleanup` | 1 / 1 |
| `curl_easy_init` / `curl_easy_reset` | 1 / 1 |

**Thirty-seven `CURLOPT_` options**, which group into six jobs:

| Job | Options |
|---|---|
| **Request shape** | `URL`, `POST`, `POSTFIELDS`, `POSTFIELDSIZE`, `HTTPHEADER`, `USERAGENT` |
| **Streaming, both directions** | `WRITEFUNCTION`/`WRITEDATA` (SSE deltas out), `READFUNCTION`/`READDATA` + `SEEKFUNCTION`/`SEEKDATA` (the request body uploaded by callback and freed the moment it is sent; `SEEK` is what makes a retry able to rewind), `HEADERFUNCTION`/`HEADERDATA` |
| **Time bounds** | `CONNECTTIMEOUT`, `TIMEOUT`, `LOW_SPEED_LIMIT`+`LOW_SPEED_TIME`, `EXPECT_100_TIMEOUT_MS` |
| **TLS** | `SSLVERSION` (floor at TLS 1.2), `SSL_VERIFYPEER`, `SSL_VERIFYHOST` |
| **Fences** | `PROTOCOLS`/`PROTOCOLS_STR` and `REDIR_PROTOCOLS`/`REDIR_PROTOCOLS_STR`, `FOLLOWLOCATION` (**off** by default), `MAXREDIRS`, `SOCKOPTFUNCTION` (sets `FD_CLOEXEC` on every socket), `OPENSOCKETFUNCTION` (the SSRF address guard, installed only for the guarded request) |
| **Liveness / cancellation** | `PROGRESSFUNCTION`/`PROGRESSDATA`, `XFERINFOFUNCTION`/`XFERINFODATA`, `NOPROGRESS` |

**What is conspicuously absent, and this is the useful half of the count:**

- **No proxy support at all** — no `CURLOPT_PROXY` anywhere.
- **No `CURLOPT_HTTP_VERSION`** — jichi never asks for HTTP/2 or HTTP/3. It
  speaks whatever the library negotiates, and its own model of the exchange is
  HTTP/1.1.
- No cookies, no authentication helpers (`Authorization` is a plain header), no
  `multi` interface, no FTP/SMTP/file, no DNS or interface pinning, no mime API.

jichi also keeps **one cached easy handle, stamped with `getpid()`**
(`g_reuse`, `g_reuse_pid`), so connection reuse survives across calls and is
correctly abandoned after a fork — which matters because `spawn_parallel` forks.

**The floor is libcurl 7.19.4 (2009)**, and the two places that need newer are
guarded: `LIBCURL_VERSION_NUM >= 0x072000` and `>= 0x075500` for the `_STR`
protocol options.

---

## 2. Would an in-house client be viable?

**No — and the measurement that settles it was taken at M430, before the
question was asked.**

### The argument that is usually made, and why it fails here

The case for replacing libcurl is footprint and dependency count. Both are
already measured, on curl 8.18.0 with `SIZE=1` so configuration is the only
variable (`LOW_MEMORY.md`):

| | system libcurl | minimal libcurl | **minimal + static musl (mbedTLS)** |
|---|---:|---:|---:|
| shared libraries | 34 | 9 | **0** |
| `--version` peak RSS | 10,012 KB | 5,988 KB | **804 KB** |
| can make a model call? | yes | yes | **yes** |

Against a curl-free static build at ~500 KB RSS that **cannot make a model call
at all**, the complete recipe lands **within ~300 KB while keeping
networking**. So the entire prize available to any replacement — in-house or
otherwise — is bounded above by roughly 300 KB of resident memory, and that is
the *upper* bound, because:

**jichi talks HTTPS, so a replacement does not remove a dependency — it
substitutes one.** An in-house client still links mbedTLS or OpenSSL. Writing
TLS is not on the table at any budget. And the 804 KB figure is dominated by
mbedTLS and the static libc, **not** by curl's HTTP layer, so removing the HTTP
layer specifically reclaims a fraction of an already-small number.

*Honest gap:* the **split of those 804 KB between curl's HTTP code and
mbedTLS+musl is not measured.** It could be, by linking the same static binary
against mbedTLS with a stub HTTP layer and diffing the RSS. If anyone wants to
reopen this question, that is the measurement to take first — and this page
should not be cited as though it had been.

### What would have to be written, from jichi's own usage

The 37 options are not decoration; each is load-bearing. A replacement must
provide, at minimum: HTTPS POST/GET with custom headers; chunked transfer
decoding; a streaming response reader that hands bytes to the SSE framer as
they arrive; a streaming request body with a rewind hook for retries; header
callbacks; connect, total and stall (low-speed) timeouts on a non-blocking
socket; TLS with SNI, peer and hostname verification, and a 1.2 floor; a
protocol allow-list and a redirect policy; `Expect: 100-continue` suppression;
and socket-creation hooks for `FD_CLOEXEC` and the SSRF address guard. Plus
connection reuse with fork-correct invalidation.

That is a real HTTP/1.1 client: on the order of **2,500–4,000 lines of C89**,
*plus* the TLS binding — and **every one of those lines is security-relevant**.
Set against the **953 lines** that currently buy all of it.

### What would be lost, which is the part that is not about size

- **The hardening is the expensive part, and it is already there.** The SSRF
  guard, `FD_CLOEXEC` on every socket, the protocol allow-list, redirects off by
  default, the TLS floor and verification, three independent time bounds. Each
  was added for a reason recorded in the ROADMAP. A fresh client starts with
  none of them and re-earns each one by being wrong first.
- **Portability that has been paid for.** This layer compiles and runs across
  five libcs and four kernels, on 14 architectures including five big-endian,
  back to a 2009 libcurl. A new client's portability claims would all be
  **Never compiled** on day one, and this project does not let a verdict be
  assumed.
- **Protocol drift is somebody else's job.** TLS versions get deprecated and
  servers change behaviour. Delegating that is the point of a dependency.

### The middle path already exists

The recipe that gets the footprint **without** writing a line of HTTP is
`scripts/minimal-curl.sh`: a single-TLS-backend libcurl, statically linked
against musl. It is deliberately **not** part of `make` — it downloads and
compiles a third-party dependency, and no build of jichi may ever require that.
It is a measurement of a toolchain, not a property of jichi. `--version` at
**804 KB, zero shared libraries, model call verified**.

---

## 3. The recommendation

**Keep libcurl. Do not write an HTTP client.** The dependency rule that jichi
already states — *libcurl and nothing else, linked rather than vendored* —
survives this question rather than being challenged by it.

Two things are worth doing instead, and neither is a rewrite:

1. **Leave the surface where it is: one file, 11 functions, 37 options.** Its
   real virtue is not its size but that it is *auditable in one sitting* and
   that no second translation unit can open a socket behind its back. If a
   future change needs a curl call outside `jc_http.c`, that is the thing to
   refuse.
2. **If footprint is the goal, point people at `minimal-curl.sh`,** which is
   already measured, already reproducible, and already documented for the ≤64 MB
   tier.

**What would change this verdict:** a requirement to run somewhere libcurl
genuinely cannot go. That is not any row in today's matrix — libcurl builds on
all of them — so the question is currently hypothetical, and the honest answer
to a hypothetical is to say which measurement would answer it, which §2 does.
