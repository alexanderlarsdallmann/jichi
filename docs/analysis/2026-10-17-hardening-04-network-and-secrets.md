# Hardening review 4 — The network and the keys

*This chapter is part of the 2026-10-17 hardening and platform review of jichi at commit
`e5d832a4`. Every `path:line` below refers to that commit.*

*Start with [the overview](2026-10-17-hardening-00-overview.md). Drafted by reviewer C (Claude Opus 5.5), edited by the review's lead.*

*It rests on reviewer C's report. That report read these files in full: the HTTP layer
(`src/net/`), the six network tools, the MCP HTTP transport, the logging, telemetry and audit
writers, and the secret registries. It also read the parts of the configuration, platform, setup and
`main.c` code that handle API keys. The lead re-checked the central claims by hand. One finding was
measured offline against the repository's own mock model server; the others were verified by reading
the code.*

*Draft by Claude Opus 5.5 (reviewer C), 2026-10-17.*

---

## Who this is for, and what you will be able to do

**For** a self-learner or junior developer who knows some C, not jichi, and wants to see how a
program that talks HTTPS keeps four things under control: the URLs it is asked to fetch, how long it
waits, how much it accepts, and the secret key it sends.

**After reading it you will be able to:**

- follow one request from jichi's tool code through libcurl to the server and back;
- explain why jichi checks the *address of the socket* rather than the *name in the URL*, and what
  "time of check, time of use" means;
- tell a **fence** from a **cap**, in jichi's vocabulary, and say which timeouts are which;
- list every place an API key lives while jichi runs, and every file it could end up in;
- read each finding below, judge its severity, and name the test that would prove a fix.

**What you need first:**

- [Annai 6: the wire](../reading/annai-06-the-wire.md), the guided tour of how jichi makes a model
  call.
- [Review 7: the wire](2026-10-08-review-07-the-wire.md) and
  [Review 9: reaching outside](2026-10-08-review-09-reaching-outside.md). These are the previous
  review's chapters on the same code.
- [HARDENING.md](../HARDENING.md) §2-§4, the project's own account of its secret, network and sink
  hardening.
- [VOCABULARY.md](../VOCABULARY.md) for jichi's terms. The two that matter here are explained below
  as well.

**Where it fits:** chapter 4 of the hardening review; the other chapters cover input parsing,
the execution fences, files and persistence, the build and platforms, and resources.

---

## 1. Two words first: fence and cap

jichi's documentation uses two words for limits, and this chapter needs both.

- **Fence.** A boundary that refuses rather than warns. It limits *what* can happen: which paths a
  tool may write, which addresses a URL may reach. A fence stays on all the time.
- **Cap.** A limit on *how much*: how long, how many bytes, how many tokens. The project's rule in
  `CLAUDE.md` ("Caps versus fences") is that caps stay off for measurement runs. The reason: a cap
  that fires does not only hide the real answer; it produces a plausible wrong one.

Timeouts come in both kinds:

- **Connect bound (a fence).** `timeouts.connect` fires only while no connection exists yet.
- **Dead-stream bound (a fence).** `timeouts.deadStream` fires only when *nothing at all* is arriving
  on an open connection.
- **Stall and request bounds (caps).** `timeouts.stall` and `timeouts.request` cut a call that is
  still working.

Keep this distinction in mind for decision D7 and finding C-09.

---

## 2. How a request is made

Every byte jichi sends over the network goes through one wrapper around libcurl,
`src/net/jc_http.c`. `jc_http_perform` collects a whole response; `jc_http_stream` hands each chunk to
a callback, which the model call uses so that text appears while the model is still writing. A caller
describes its request in a `struct jc_http_request` (`include/jc_http.h:48-136`) and sets only what
differs from the defaults. A zeroed struct means: no redirects, no address check, no size cap, no
time cap.

### The shape, in pseudocode

```text
request(req):
    handle = the one cached curl handle      # reused: warm TLS
    reset handle's options                   # nothing leaks from
                                             # the last request
    set URL, headers, method, body
    set: follow redirects only if req asks
    set: http and https only, at most 5 hops
    set: verify TLS peer and host name
    if req.block_private_addrs:
        install the connect-time address check
    set timeouts that req asked for
    perform
    report status, body or a diagnosis
```

### The path of one model call, as a text diagram

```text
agent loop (src/chat/jc_agent.c)
  |  provider builds JSON body + headers
  v
jc_http_stream (src/net/jc_http.c)
  |  apply_common: options for THIS request
  v
libcurl  ---- TLS ---->  model server
  |
  |  chunks of the reply
  v
SSE parser (src/net/jc_sse.c)
  |  one event at a time
  v
provider on_event: text and tool calls
```

**In words.** The agent loop asks the provider to turn the conversation into a JSON body and a set
of headers; one of the headers carries the API key. `jc_http_stream` configures libcurl for exactly
this request, and libcurl sends it over TLS. The reply comes back in chunks. The server-sent-events
(SSE) parser splits the chunks into events and hands them to the provider. The provider turns them
into text and tool calls.

The process keeps one curl handle for its whole life (`jc_http.c:481`), so the TLS connection to the
model server stays warm between turns (M227). Before every request one function, `apply_common`
(`jc_http.c:875-1015`), resets the handle and configures it again, so no option leaks from one
request into the next. The security posture is written into the source rather than inherited from
libcurl's defaults:

```c
/* src/net/jc_http.c:888-905 (abridged) */
curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,
                 req->follow_redirects ? 1L : 0L);
...
curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
...
curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
```

Further down the same function, TLS peer and host-name verification are switched on explicitly and
TLS 1.2 is the minimum version (`jc_http.c:945-951`). No config key, flag or environment variable
turns verification off: the only `SSL_VERIFY` lines in `src/` are those two.

---

## 3. Design decisions

Each decision is set out the same way: what was decided, why (with its record), what it implies,
and the lesson you can take to any codebase.

### D1. A request that carries a key never follows a redirect (M472)

**What was decided.** `follow_redirects` defaults to 0. Only three callers set it, and none of them
sends a credential:

- `fetch_url` (`src/tools/jc_tool_fetch.c:79`)
- `@rss:` feeds (`src/command/jc_refs.c:239`)
- documentation URLs (`src/index/jc_docs.c:339`)

**Why.** A redirect moves a request, *headers included*, to a host nobody chose. libcurl drops the
standard `Authorization` header on a cross-host hop. It cannot know that other headers, such as
Anthropic's `x-api-key`, are credentials too. The 2026-08-17 source audit showed the key arriving at a
redirect target (its finding H1). Stripping credentials hop by hop was rejected, because headers an
operator defines cannot be classified. The record is [HARDENING.md](../HARDENING.md) and the
2026-08-17 analysis pages.

**What it implies.**

- *Good.* The rule is total and simple.
- *Good.* An unfollowed redirect is explained, with its target, instead of showing a bare "HTTP 302"
  (`jc_http.c:374-393`).
- *Bad.* A provider behind a redirecting gateway must be configured with its final URL.
- *Gap in the record.* The decision has no row of its own in [DECISIONS.md](../DECISIONS.md). It
  appears only inside the rejection text of [M648](../DECISIONS.md#dec-m648).

**Lesson.** When you cannot tell which data is secret, do not move the data. Do not try to filter it.

### D2. The private-address check is opt-in per request (M131)

**What was decided.** Only requests whose URL a *model* chose set `block_private_addrs`. Today that is
`fetch_url`. Endpoints the operator configured are not checked: the model server, MCP over HTTP,
embedding, rerank and image servers. See [HARDENING.md](../HARDENING.md) §3.

**Why.** Those endpoints live on `localhost` or the local network as a matter of course. A model on
`127.0.0.1:1234` is the documented normal case.

**What it implies.**

- *Good.* Local models keep working.
- *Bad.* Each of the roughly twenty HTTP call sites has to answer "who chose this URL?". Two answers
  are stretched:
  - an image server's reply can name a URL that jichi then fetches (C-07);
  - a feed the user named can redirect jichi elsewhere (C-08).

**Lesson.** A per-request security flag puts a decision at every call site. Write the question next to
the flag (`include/jc_http.h:106-112` does). Audit the call sites as a set, not one at a time.

### D3. The address is checked when the socket is created

**A concept first: time of check to time of use (TOCTOU).** Whatever a program checks can change
before it is used. For a URL, the check would be the host name and the use is the connection; a name
can resolve to a public address at the check and to `127.0.0.1` at the connection (*DNS rebinding*),
and a redirect can move the request after the check passed.

**What was decided.** jichi does not check names. It installs a callback that libcurl calls when it
creates a socket. At that point the name has been resolved and the real address is in hand:

```c
/* src/net/jc_http.c:597-619 */
static curl_socket_t opensocket_guard(void *clientp, curlsocktype purpose,
                                      struct curl_sockaddr *addr)
{
    (void)clientp;
    if (addr == NULL) {
        return CURL_SOCKET_BAD;
    }
    if (purpose == CURLSOCKTYPE_IPCXN) {
        if (addr->family == AF_INET) {
            struct sockaddr_in *s = (struct sockaddr_in *)(void *)&addr->addr;
            const unsigned char *o = (const unsigned char *)&s->sin_addr.s_addr;
            if (jc_net_ipv4_blocked(o[0], o[1], o[2], o[3])) {
                return CURL_SOCKET_BAD;
            }
        } else if (addr->family == AF_INET6) {
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)(void *)&addr->addr;
            if (jc_net_ipv6_blocked((const unsigned char *)&s->sin6_addr)) {
                return CURL_SOCKET_BAD;
            }
        }
    }
    return socket(addr->family, addr->socktype, addr->protocol);
}
```

The string check in `fetch_url` (`jc_tool_fetch.c:49-66`) only gives a clearer message in obvious
cases; the 2026-08-17 audit judged the connect-time check the real defence, and the design sound.

**What it implies.**

- *Good.* Rebinding and redirect-to-internal are covered by construction.
- *Limit.* The callback judges only the address libcurl hands it. Finding C-02 is one case where
  that address is not the final destination.
- *Gap.* Nothing tests the callback itself. NETGUARD's gap page (2026-10-05) notes that no test drives
  a redirect or a rebinding, and `tests/test_http.c` tests the address classifier but not
  `opensocket_guard`.

**Lesson.** Put a security check at the point of use, and test the check itself, not only the
classifier it calls.

### D4. One reused connection per process (M227, M326v, M880, M882)

**What was decided.** One curl handle serves the whole process. It is reset before each request but
keeps its connections (`jc_http.c:792-839`).

**Why.** TLS handshakes are slow, especially on small boards. In one large workload 14% of model calls
died on the connect timeout, and a warm handle avoids most connects (`jc_http.c:395-417`, M326v). The
handle carries its owner's process id, so a forked child never touches it (`jc_http.c:798-820`).

**What it implies.**

- *Good.* Lower latency and fork safety, both argued in the source.
- *Bad.* Global state, correct only while transfers happen one at a time. In M882, a per-transfer
  record lived on the stack. libcurl closed a kept-alive connection during a *later* transfer and
  wrote into the dead frame: a stack-use-after-return that AddressSanitizer caught
  (`jc_http.c:567-581`).

**Lesson.** Anything you hand to a library must live as long as the library may use it, not as long
as your function runs. Read the library's lifetime rules before passing it a pointer.

### D5. Secrets are protected by name and by value (M24, M130, M608, M724, M926)

**What was decided.** Once the configuration is final, `main` registers two things for every model
key (`src/main.c:17164-17202`):

- its *value*, in the redaction registry (`src/util/jc_log.c:149-176`), so that logs, telemetry,
  sessions and request dumps print `***` instead;
- its environment variable's *name*, in the scrub list (`src/util/jc_proc.c:70-106`), so that
  children (the shell, MCP servers, git, verifiers) start without it.

**Why.** The record is [HARDENING.md](../HARDENING.md) §2 and [M608](../DECISIONS.md#dec-m608).
ANECDOTES #110 tells how each fix found the next copy of the key. The name list grows without limit,
and jichi refuses to start if memory runs out while registering (M724, `main.c:17194-17201`). Since
M926 the value registry *copies* each secret; before, it pointed into memory freed every turn.

**What it implies.**

- *Good.* There are two independent layers, and a refusal instead of a silent gap.
- *Bad.* Only what is registered is protected (C-12).
- *Bad.* A name-based scrub cannot see a key copied into another variable. HARDENING.md M894 says so.

**Lesson.** A deny-list protects exactly what it lists. Write that list down where users read.

### D6. Hiding the process from same-user readers (M770, M771, M796-M799)

**What was decided.** At start, jichi makes its environment and memory unreadable to other processes
of the same user (`src/util/jc_proc.c:159-260`, [M770](../DECISIONS.md#dec-m770)):

- **Linux:** `prctl(PR_SET_DUMPABLE, 0)`. `/proc/<pid>/environ` becomes root-only, and an ordinary
  user can no longer trace the process or get a core dump from it.
- **FreeBSD:** `procctl(PROC_TRACE_CTL)`.
- **OpenBSD, NetBSD and illumos:** a partial measure, overwriting the key strings in jichi's own
  environment.

**Why.** After M130, a child could still read the key from its *parent's* `/proc` entry (measured on
2026-09-25).

**What it implies.**

- *Good.* The flag is read back after it is set. `doctor` reports what is in force (five states), not
  what was attempted (`jc_proc.c:175-212`).
- *Good.* The opt-out, `JICHI_KEEP_DUMPABLE`, needs a clear yes, so a typo leaves the protection on.
- *Bad.* macOS, GNU/Hurd, Haiku, Cygwin and MSYS2 have no mechanism (def-30).

**Lesson.** Report the effect, not the attempt. A switch whose state you cannot read back is a switch
you cannot claim is on.

### D7. Caps versus fences: applied to the model call, not yet to the side calls

**What was decided.** The main model call honours `timeouts.connect`, `timeouts.stall`,
`timeouts.request` and `timeouts.deadStream` (`jc_http.c:959-986`). The stall cap and the dead-stream
fence share libcurl's one low-speed window. So the fence still bounds a call when the cap is off for
a measurement.

**Why.** The header explains it with the measurement behind it: a 12,000-token prompt on a local
model sends no bytes for about 36 s while the model prefills (`include/jc_http.h:32-43`).

**What it implies.**

- *Good.* The main call is the best-reasoned timeout code in the tree.
- *Bad.* About ten side calls each set a fixed total cap. None of them sets a connect fence or a
  dead-stream fence, and none reads `timeouts.*` (C-09).

**Lesson.** A rule about timeouts must hold at every line that sets one, not only the line that was
measured. A lint that lists every `timeout_secs = <number>` holds it cheaply.

### D8. Telemetry at the `metrics` tier is on by default (M599)

**What was decided.** jichi appends one JSON line per event to a per-workspace file under
`~/.jichi.d/telemetry/`, mode 0600 (`src/config/jc_config.c:1145`;
[M599](../DECISIONS.md#dec-m599)). The `full` tier, with prompts and file content, stays opt-in.

**Why.** The project's telemetry corpus has answered many design questions without new code.

**What it implies.**

- *Good.* Decisions rest on evidence, not guesses.
- *Bad.* The writer's own comment says logs "get concatenated and shared"
  (`src/util/jc_eventlog.c:150-153`). So every field must pass through the redactor (C-03).
- *Bad.* `src/main.c:18673` still says telemetry is "off by default".

**Lesson.** A sink that is on by default must meet the standard of its most careless reader. Mode
0600 protects a file from other users. It does not protect it from being attached to a bug report.

---

## 4. Findings

**Severity** follows the 2026-10-08 scale:

- **high:** data loss, a crash, a security boundary crossed, or a fence that does not hold;
- **medium:** a wrong result or a misleading report in a plausible case;
- **low:** an edge case, an inconsistency, or a maintenance cost;
- **note:** worth knowing, not a defect.

**Marks** say how each claim is supported:

- **[V]** verified in the tree;
- **[M]** measured;
- **[R]** read but not traced to the end;
- **[GK]** general knowledge, not checked here.

Security findings are described by **class**, as [SECURITY.md](../../SECURITY.md) promises. There
are no exploit inputs or reproduction steps here; the maintainers have them in an internal page.
One further security finding is held back entirely until a failing test exists (§9, step 3).

### Medium

**C-02: The private-address check does not account for a proxy in the environment.**

- *Classification:* medium, [V] and [GK]. Already recorded in the internal portability analysis
  (2026-10-15), and still open.
- *Where:* `src/net/jc_http.c` (`apply_common`, `opensocket_guard`).
- *What happens:* libcurl reads proxy settings from the environment [GK]. jichi sets no proxy option:
  there is no `CURLOPT_PROXY` or `CURLOPT_NOPROXY` in `src/` [V]. With a proxy, the socket goes to the
  proxy, so D3's check judges the proxy's address instead of the destination's.
- *Fix:* make the behaviour explicit for address-checked requests. Either disable the proxy for them,
  or refuse while a proxy variable is set and name it. Have `doctor` list the proxy variables, and
  state the choice in SECURITY.md.
- *Test that goes red first:* a guarded fetch with a proxy variable pointing at a port the driver
  owns.
- *Effort:* S.

**C-03: One telemetry event writes text that bypasses the redactor.**

- *Classification:* medium, [M] and [V]. New. It contradicts the 2026-10-05 telemetry gap page and
  M802.
- *Where:* `src/chat/jc_agent.c`, in the `model_call` failure event.
- *Background:* every telemetry string is meant to pass through the redacting helper:

  ```c
  /* src/util/jc_eventlog.c:267-276 */
  /* Scrub any registered secret before it lands on disk (M132). ... */
  if (jc_redact_active()) {
      red = (char *)malloc((size_t)strlen(s) + 1);
      if (red != NULL) {
          jc_redact_secrets(s, red, (jc_size)strlen(s) + 1);
          s = red;
      }
  }
  ```

- *What happens:* the failed-call event adds three text fields with plain `cJSON_AddStringToObject`
  instead, one of them holding text from the server. The default tier writes it, so a registered
  secret in that text reaches the telemetry file in clear, while `jc_logf` redacts the same text on
  the terminal.
- *Measured* [M]: offline, against the repository's mock server, before and after a one-line change;
  the reproduction and its output are in the internal companion.
- *Fix:* route the three fields through `jc_eventlog_add_text` and redact the copy kept for a
  parallel run's report. Then a lint: no `cJSON_AddStringToObject` on a telemetry object unless the
  value is a constant or allowlisted.
- *Test that goes red first:* extend `tests/smoke/redact_sinks.sh`, which checks four sinks for a
  registered canary, to the failed-call path.
- *Effort:* S.

**C-05: Language-server children inherit the API keys.**

- *Classification:* medium, [V]. Already recorded in
  [Review 9](2026-10-08-review-09-reaching-outside.md) as F8, and still open.
- *Where:* `src/lsp/jc_lsp.c:116-124`.
- *What happens:* the language server is started without the scrub that MCP servers get
  (`src/mcp/jc_mcp_stdio.c:102`), and that git gets since M2 (`src/tools/jc_tool_git.c:93`).
  M2 changed git because git runs programs named by the repository. Some language servers build the
  project, run its build scripts, or load its plugins [GK]. That is code the repository's author
  wrote, and it inherits the API key.
- *Also stale:* the comment at `include/jc_spawn.h:68-72` still says git never scrubs.
- *Fix:* `sr.scrub_env = 1;` at the spawn site, and update the comment.
- *Test that goes red first:* `tests/tools/mocklsp` reports whether `JICHI_API_KEY` reached it.
- *Effort:* S.

**C-06: Only `fetch_url` limits a response's size while it downloads.**

- *Classification:* medium, [V]. Partly recorded ([Review 7](2026-10-08-review-07-the-wire.md) F12,
  and the 2026-10-08 internal MCP and documentation-URL notes); the general form is new.
- *What happens:* `max_bytes` makes the collect callback abort an over-long transfer
  (`src/net/jc_http.c:514-528`), and only `fetch_url` sets it (32 MB, `jc_tool_fetch.c:82`). Every
  other collected response grows without limit: embeddings, rerank and image generation
  (`src/net/net_util.c:87-96`), speech (`src/net/jc_audiogen.c:113-122`), transcription, web search,
  MCP over HTTP (`src/mcp/jc_mcp_http.c:153-163`), model listings, feeds, documentation URLs and
  compaction. The configurable audio and image caps are compared only after the whole body is in
  memory (`src/tools/jc_tool_audiogen.c:107`, `src/tools/jc_tool_imagegen.c:225`). On a 256 MB or
  512 MB board, a broken server then means an out-of-memory kill instead of an error message.
- *Fix:* a default ceiling in `jc_http_perform` when a caller passes 0; the audio and image caps
  passed down as `max_bytes`.
- *Test that goes red first:* the mock server's `body-file` reply, several times the audio cap;
  assert the cap message and a peak memory well below the reply's size.
- *Effort:* S-M.

**C-09: Side calls have fixed time caps, and no connect or dead-stream fence.**

- *Classification:* medium, [V] for the values, [R] for what follows a cut. New as a class (the
  internal MCP note covers MCP's stdio deadline).
- *Where:* each of these sets a hard total limit in the source:

  | Call | Cap | Where |
  |---|---|---|
  | compaction summary | 60 s | `src/chat/jc_compact.c:486` |
  | embeddings, rerank, image generation | 60 s | `src/net/net_util.c:93` |
  | speech / transcription | 60 s / 120 s | `src/net/jc_audiogen.c:119`, `src/net/jc_transcribe.c:105` |
  | MCP over HTTP, every call | 120 s | `src/mcp/jc_mcp_http.c:31` |
  | one-shot helper calls | 60 s | `src/chat/jc_oneshot.c:83` |

- *What happens:* none sets a connect bound (libcurl then waits up to 300 s [GK]) or the dead-stream
  fence, and none reads `timeouts.*`. At the header's own figure of about 200 prompt tokens per
  second on a local model (`include/jc_http.h:36-43`), a 20,000-token transcript needs about 100 s
  before its first byte, so compaction is cut at 60 s exactly when the context is largest. A long
  MCP tool dies at 120 s.
- *Fix:* read these timeouts from configuration; always set the connect and dead-stream fences;
  default the total cap to off or to a measured value.
- *Test that goes red first:* the mock server's delayed reply (`text-stream` with `delay`) on the
  summary request, with `stall 0`.
- *Effort:* M.

### Low

**C-04: Preserved tool output is not redacted, and its directory is not private.**

- *Classification:* low, [V].
- *Where:* `src/util/jc_toolout.c`.
- *What happens:* long command output, compaction elisions, sub-agent answers and verifier output are
  kept in `~/.jichi.d/tool-output/<pid>/` (M339) without redaction. The session file is redacted
  (M802), and so is the request dump in the same source file (`jc_toolout.c:143-151`).
- *Directory mode:* 0755 (`jc_toolout.c:67`). The files are 0600.
- *Status:* the directory mode was recorded in the 2026-10-08 review. The redaction gap is new.
- *Fix:* redact before writing, and use `jc_mkdir_p_private`.
- *Effort:* S.

**C-07: An image server's reply can name a URL that is fetched without the address check or a size
cap.**

- *Classification:* low, [V]. Review 7, F12, still open.
- *Where:* `src/net/jc_imagegen.c:136-163`.
- *Why not just turn on the check:* LocalAI on `localhost` legitimately replies with a `localhost`
  URL.
- *Fix:* apply the check unless the reply's host and port equal the configured `apiBase`'s, and set
  `max_bytes` to the image cap.
- *Effort:* S.

**C-08: Feeds and documentation URLs follow redirects without the address check.**

- *Classification:* low, [V].
- *Where:* `src/command/jc_refs.c:233-239`, `src/index/jc_docs.c:332-339`.
- *What happens:* [HARDENING.md](../HARDENING.md) §6b records the first URL as a decision: the user
  typed it. That reasoning does not cover where the feed's server sends the request next.
- *Fix:* check every hop after the first, or state the limit in SECURITY.md.
- *Effort:* M.

**C-10: The random source falls back to a predictable generator without saying so.**

- *Classification:* low, [V] and [GK]. Recorded in the internal portability analysis, in def-74, and
  in the 2026-08-17 audit's L1 (partly fixed).
- *Where:* `src/util/jc_uuid.c:27-74`.
- *What happens:* identifiers come from `/dev/urandom`. Without it (DOS, a minimal chroot) they come
  from a time-seeded `rand()`, and nothing reports that. The multipart boundary that separates an
  uploaded file from the rest of a request is one such identifier. It matters only because the
  payload is never checked for it.
- *Fix:*
  - check the payload for the boundary and pick a new one on a match;
  - prefer `getrandom`, `getentropy` or `arc4random_buf` where a probe finds them;
  - let `doctor` report the fallback.
- *Test:* a unit test whose payload contains the boundary.
- *Effort:* S, then M.

**C-11: The address classifier does not cover some reserved IPv6 ranges.**

- *Classification:* low, [V] and [GK]. Recorded in the internal portability analysis.
- *Where:* `src/net/jc_http.c:63-90`.
- *What happens:* some special-purpose ranges can lead to private IPv4 hosts, or are not ordinary
  unicast. They are not refused.
- *Fix:* classify the embedded IPv4 address of the translation ranges, and refuse the rest, against
  RFC 6890.
- *Test:* one unit row per range.
- *Effort:* S.

**C-12: Only model keys and the search key are registered for redaction.**

- *Classification:* low, [V]. Recorded as a class in the telemetry gap page §2 and HARDENING.md M894.
- *Where:* `src/main.c:17180-17192`.
- *What happens:* credentials in an MCP server's `headers`, and in MCP or user-tool `env` values
  (`src/config/jc_config.c:685-686`, `:840`), are held by jichi but never registered.
- *Fix:* register header values whose name marks a credential, and environment values whose name
  ends in `_KEY`, `_TOKEN` or `_SECRET`.
- *Effort:* S.

**C-13: A long log line is cut before it is redacted.**

- *Classification:* low, [V]. Recorded in the 2026-10-08 internal utilities report.
- *Where:* `src/util/jc_log.c:72-81`.
- *What happens:* with a secret registered, a message is formatted into a 4,096-byte buffer and
  redacted after the cut. A secret that straddles the cut is not recognised.
- *Fix:* redact the full text in a heap buffer, and cap the whole error bodies that five network
  clients log.
- *Effort:* S.

**C-14: The `doctor` plain-HTTP warning stays silent for every private address.**

- *Classification:* low, [V]. New; a side effect of M472's fix for audit L4.
- *Where:* `src/main.c:13339-13387`.
- *What happens:* the comment exempts loopback. The code reuses the address classifier, which also
  says yes for `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16` and `100.64.0.0/10`. So a key sent in
  clear over a LAN or Wi-Fi passes in silence. MCP and search URLs are not checked at all.
- *Fix:* exempt loopback only, warn for the private ranges, and check MCP and search URLs too.
- *Test:* a private-address model in `tests/smoke/transport_posture.sh`.
- *Effort:* S.

**C-15: Nothing reports what the linked libcurl can do.**

- *Classification:* low, [V] and [GK]. Partly recorded in the internal portability analysis (no
  CA-bundle setting).
- *Where:* `src/main.c:13389-13391`.
- *What happens:* `doctor` prints "libcurl available" and nothing more; `src/` never calls
  `curl_version_info`. These cases then look like network failures at the first model call:
  - a TLS-free libcurl, which some platform rows use ([PLATFORMS.md](../PLATFORMS.md));
  - an old libcurl;
  - a static binary with no CA bundle.
- *Fix:* `doctor` and `--version` print the libcurl version, the TLS backend and the default CA path
  (`CURLINFO_CAINFO`, libcurl 7.84 or later [GK]). `doctor` fails when an `https://` endpoint meets a
  library without TLS.
- *Effort:* S.

**C-16: The last fallback directory for state is used unchecked.**

- *Classification:* low, [V]. New; a residual of the 2026-08-17 audit's M4.
- *Where:* `src/platform/jc_platform_posix.c:162-166`.
- *What happens:* with `HOME` unset and no password entry, jichi checks its per-user `/tmp` directory
  carefully. When that check fails, it uses a per-process directory without the same check.
- *Fix:* apply the same check there, or keep no state.
- *Effort:* S.

**C-17: A rerank server can trigger undefined behaviour with an out-of-range index.**

- *Classification:* low, [V]. Review 7, F9.
- *Where:* `src/net/jc_rerank.c:71`.
- *What happens:* `(int)idx_node->valuedouble` is undefined in C outside `int`'s range.
- *Fix:* read cJSON's clamped `valueint`, as the embeddings client already does since M725
  (`src/net/jc_embed.c:43-47`).
- *Effort:* S.

**C-18: The lint against unbounded string calls checks only `sprintf`.**

- *Classification:* low, [V]. New.
- *Where:* `tests/smoke/sprintf_lint.sh`.
- *What happens:* `CLAUDE.md` says the lint also covers `strcpy`, `strcat` and `gets`. Its pattern
  matches only `sprintf(`. There are 12 `strcpy` and `strcat` calls outside `cJSON.c`, among them the
  redaction registry's correctly sized copy (`src/util/jc_log.c:174`).
- *Fix:* widen the pattern, add an exact-line allowlist, and floor the count.
- *Effort:* S.

**C-19: Redaction writes the original text if its allocation fails.**

- *Classification:* low, [V].
- *Where:* `src/util/jc_eventlog.c:270-276` and `src/session/jc_session.c:475-482`.
- *Status:* the session half was recorded in the 2026-10-08 review; the event-log half is new.
- *Fix:* write a "redaction failed" marker instead.
- *Effort:* S.

### Notes

**C-20: The key is never wiped, and core dumps stay possible where there is no dumpable flag.**

- *Classification:* note, [V] and [GK].
- *What happens:* the key lives in the configuration arena for the whole session. It also lives in
  the redaction registry, in a stack buffer per request, and in two heap copies per request that are
  freed without being cleared. Wiping the per-request copies gains little while the arena copy
  remains. What matters is keeping the process from being read (D6). On platforms without that, a
  crash can still write a core file containing the key.
- *Fix:* set `RLIMIT_CORE` to 0 at start on every POSIX platform, honouring `JICHI_KEEP_DUMPABLE`;
  clear the header buffers before freeing them.
- *Effort:* S.

**C-21: Small items, all [V].**

- A long `apiBase` is cut silently by the provider's 512-byte endpoint buffer
  (`src/provider/prov_internal.h:38`); refuse instead.
- The setup wizard reads at most 1,023 bytes of a pasted key (`src/main.c:6455`); the rest reaches
  the next prompt, which can echo it.
- `web_search` sends its key in the body as well as the header (`src/tools/jc_tool_websearch.c:171-187`).
- `CURLOPT_NOSIGNAL` is unset; with a synchronous resolver libcurl uses `SIGALRM` [GK]. Decide and
  document it.
- The request-dump option parsed at `src/main.c:1394` is missing from `jichi --help` (checked on a
  build of `e5d832a4`); `JICHI_DUMP_REQUESTS` enables the same dump.

---

## 5. Platform notes

| Platform | What jichi does or lacks here | Recommendation |
|---|---|---|
| Linux (glibc, musl) | `PR_SET_DUMPABLE`; every curl socket close-on-exec (`jc_http.c:533-561`) | `getrandom` behind a probe (C-10) |
| FreeBSD | `procctl(PROC_TRACE_CTL)`; libcurl's compiled-in CA path, no CA option [V] | `getentropy` / `arc4random_buf` [GK] |
| OpenBSD, NetBSD, illumos | environment scrub only; a debugger can still read the key (HARDENING.md §2) | `RLIMIT_CORE` 0 (C-20); `getentropy` [GK] |
| macOS | no protection (def-30) | measure same-user `ps` first; then `PT_DENY_ATTACH` and `RLIMIT_CORE` [GK] |
| GNU/Hurd, Haiku, Cygwin, MSYS2 | no protection; `chmod` is a no-op on an MSYS2 `noacl` mount (PLATFORMS.md) | `RLIMIT_CORE` 0 |
| FreeDOS (DJGPP) | no sockets without a TCP stack; classifier still compiled, fails closed (`jc_http.c:114-121`) | announce the permanent `rand()` fallback (C-10) |
| Pi Zero 2 W, LicheeRV Nano | reused connection avoids a handshake per call; `doctor` TLS peak about 16 MB (LOW_MEMORY.md) | C-06 first: an uncapped body is an OOM kill |
| TLS-free libcurl rows | https fails only at the first call (C-15) | minimal recipe can also drop FTP, FILE, SMB, WebSockets |
| Cross-built TLS libcurl | curl's configure finds no CA bundle when cross-compiling [GK] | pass the CA path explicitly in `scripts/minimal-curl.sh` |
| Old libcurl (floor 7.19.4) | version guards correct (`jc_http.c:546-551`, `:736-744`, `:896-904`, `:945-949`) | none; redirects-off makes old header stripping moot |

---

## 6. Optimizations

None is large; numbers are estimates unless marked measured.

- The SSE parser appends one byte at a time (`jc_sse.c:111-112`). Appending whole runs found with
  `memchr` saves CPU on slow boards, well under 1% of a turn.
- The redactor compares every byte with every secret (`jc_log.c:113-144`) over the whole session at
  each save: about ten million comparisons for a 5 MB session and two secrets. A first-byte `memchr`
  per secret cuts that sharply.
- `doctor --live` downloads the whole model table, 1.6 MB as the project measured
  (`net_util.c:429-431`); a short cache helps slow links.
- Each identifier reopens `/dev/urandom` (`jc_uuid.c:27-37`); the system call of C-10 makes it one
  syscall.

---

## 7. Status of earlier findings in this area

Each line was checked against the code at `e5d832a4` [V].

| Earlier finding | Status at `e5d832a4` | Evidence |
|---|---|---|
| 2026-08-17 H1: key sent on with a redirect | fixed (M472) | `jc_http.c:888-905` |
| 2026-08-17 H2: children inherit the provider socket and logs | fixed (M472) | `jc_http.c:912`, `jc_eventlog.c:117` |
| 2026-08-17 M4: state moved to `/tmp` without `HOME` | fixed (M472); residual C-16 | `jc_platform_posix.c:126-190` |
| 2026-08-17 L1, L2, L4: randomness, multipart name, TLS floor | L2, L4 fixed; L1 partly (C-10); C-14 | `jc_uuid.c`, `jc_multipart.c:37-50` |
| M130, M608, M724: keys in children's environment | fixed, except the LSP (C-05) | `jc_spawn.c:125-127`, `main.c:17164` |
| M770, M771: the parent's `/proc` entries | fixed on Linux and FreeBSD; def-30 open | `jc_proc.c:175-205` |
| M131: `fetch_url` address check; NETGUARD: no guard test | check fixed; C-02 and the test gap open | `jc_tool_fetch.c:49-82` |
| M132, HARDENING §4: private sinks; sessions unredacted | fixed; sessions redacted since M802 | `jc_session.c:475-482` |
| 2026-10-04 audit G4: audit log follows a symlink | open | `jc_audit.c:84-91` |
| 2026-10-08 internal: redaction registry dangling pointer | fixed (M926) | `jc_log.c:164-175` |
| Review 7 F2: in-stream error becomes the answer | fixed (M909) | `jc_agent.c:336-354` |
| Review 7 F3, F4, F8: abort as success; stall default; overflow text | open | `jc_http.c:1165`, `jc_http.h:46`, `jc_http.c:253` |
| Review 7 F9, F12, F15; Review 9 F8 | open (C-17, C-07, C-06, C-05) | see those findings |
| Portability 2026-10-15: git children keep keys | fixed (M2, inside `e5d832a4`) | `jc_tool_git.c:71-93` |
| Portability 2026-10-15: proxy, entropy, IPv6, CA bundle | open (C-02, C-10, C-11, C-15) | `jc_http.c`, `jc_uuid.c` |
| CFG-1 / def-283: a project config sends the key elsewhere | fixed (M851) | `jc_config_trust.c` |
| Telemetry gap §1: every field redacted; NETGUARD §4.2 | not true (C-03); SECURITY.md still silent | `jc_agent.c:579-588` |

Not verified: the converter's output mode (2026-10-08 internal report). The unpushed commit
`bac217bd` adds `-c commit.gpgsign=false` at the git spawn sites; keys were already scrubbed there.

---

## 8. What is done well

- **Keyed requests never follow redirects, and an unfollowed 3xx is explained** (`jc_http.c:374-393`,
  `:848-870`). The risk was removed, not filtered (D1).
- **TLS verification is explicit and cannot be switched off from outside the source**
  (`jc_http.c:945-951`). The floor degrades correctly on old libcurl, and says why (`:922-949`).
- **The address check sits where the socket is created** (`jc_http.c:597-619`). The classifier is
  compiled into socket-less builds too and fails closed on what it cannot read (`:111-129`, M190).
- **Every libcurl socket is close-on-exec**, through the one hook that sees them all
  (`jc_http.c:533-561`).
- **The secret-name registry grows, validates names, and refuses to start rather than leak**
  (`jc_proc.c:70-106`, `main.c:17194-17201`). Process-hiding flags are read back, and `doctor`
  reports five distinct states (`jc_proc.c:159-212`).
- **Most sinks redact.** Session files (M802), request dumps, the audit log and the metrics-tier
  argument summary (M155) all do. `redact_sinks.sh` checks four of them and refuses to pass if it read
  nothing.
- **Private files are written atomically, `O_EXCL`, mode 0600** (`jc_platform_posix.c:355-395`).
- **The setup wizard writes only the variable's name into a config, never a key**
  (`src/setup/jc_setup.c:345-347`). It creates the key file under `umask(077)` and hides the key as it
  is typed (`main.c:6452-6491`, `:6569-6589`).
- **`jc_http.c` is a good file to learn libcurl portability from.** Its comments record measurements,
  earlier mistakes and the libcurl version behind each guard.

---

## 9. Recommendations, ordered by value for effort

1. **C-03 (S).** Route the failed-call telemetry fields through `jc_eventlog_add_text`. Extend
   `redact_sinks.sh` to that path and watch it go red first.
2. **C-05 (S).** Set `scrub_env = 1` for the language server, add the mock-LSP check, and fix the
   comment in `jc_spawn.h`.
3. **The held-back finding (S).** A one-function fix and a smoke-driver test, described in the
   internal companion.
4. **C-02 (S).** Decide the proxy behaviour for address-checked requests. Have `doctor` list proxy
   variables, and add the SSRF paragraph SECURITY.md lacks.
5. **C-06 (S-M).** Add a default response ceiling, and pass the audio and image caps down.
6. **C-04, C-19, C-13 (S each).** Redact tool-output spills in a private directory, make redaction
   fail closed, and redact the full log line.
7. **C-09 (M).** Configure the side calls' timeouts, give them the connect and dead-stream fences, and
   lint literal timeouts.
8. **C-15, C-14 (S).** Make `doctor` report the libcurl version, the TLS backend and the CA path.
   Exempt only loopback from the plain-HTTP warning.
9. **C-10 (S, then M).** Check the multipart boundary against the payload, then add the system random
   sources and a `doctor` line.
10. **C-11, C-07, C-12 (S each).** Cover the IPv6 ranges, check off-origin image URLs, and register
    MCP credentials.
11. **C-20, C-16, C-17, C-18 (S each).** `RLIMIT_CORE` 0, the fallback directory check, the rerank
    cast, and a wider string-copy lint.
12. **Records (S).** Add DECISIONS rows for "keyed requests never follow redirects" and "the address
    is checked at socket creation". Fix the stale comments at `main.c:18673` and `jc_spawn.h:68-72`,
    and list the request-dump option in `--help`.
13. **C-08 (M).** Check redirect hops for feeds and documentation URLs, or document the limit.

---

## 10. Not reviewed, and open questions

**Not reviewed:**
- the ACP and daemon transports;
- MCP stdio framing;
- the converter's output (`src/convert/`);
- `jc_untrusted.c`, the wrapper that marks fetched content as untrusted;
- the TUI's two HTTP calls;
- whether the provider's streamed text and tool arguments have a *total* bound. The SSE field cap is
  per field.

**Open questions:**
- **C-15.** Can `SSL_CERT_FILE` or `SSL_CERT_DIR` in the environment change the trust store of a
  distribution's libcurl that has a compiled-in CA bundle? If so, the environment can substitute
  trust, though not disable verification.
- **Review 7 F3.** Does the agent loop re-check the abort flag after `jc_http_stream` returns success
  on an aborted transfer? Not traced.
- **macOS.** Same-user environment visibility is unmeasured, as HARDENING.md already says.

---

## Read further

- **Primary.** libcurl API documentation, <https://curl.se/libcurl/c/> (in
  [BIBLIOGRAPHY.md](../BIBLIOGRAPHY.md)): the exact contract of `CURLOPT_FOLLOWLOCATION`,
  `CURLOPT_OPENSOCKETFUNCTION`, `CURLOPT_PROTOCOLS_STR` and `curl_easy_reset`.
- **Primary.** RFC 6890, *Special-Purpose IP Address Registries*: the ranges an address classifier
  should know.
- **Primary.** Linux `prctl(2)` and `proc(5)`: what `PR_SET_DUMPABLE` changes about `/proc/<pid>`.
- **Secondary.** OWASP, *Server-Side Request Forgery Prevention Cheat Sheet*: the general shape of the
  problem D2 and D3 solve.
- **Secondary.** Robert C. Seacord, *Effective C*, 2nd ed. (No Starch Press, 2024; in
  BIBLIOGRAPHY.md): bounded strings and undefined conversions (C-17, C-18).
- **In this tree.** [HARDENING.md](../HARDENING.md) §2-§4; [SECURITY.md](../../SECURITY.md);
  [Annai 6](../reading/annai-06-the-wire.md);
  [Fukabori 8](../reading/fukabori-08-streaming-and-the-no-buffering-invariants.md);
  [Review 7](2026-10-08-review-07-the-wire.md) and [Review 9](2026-10-08-review-09-reaching-outside.md).
