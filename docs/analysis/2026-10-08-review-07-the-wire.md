# Review 7 — The wire: network, providers, JSON and configuration

*Part of the 2026-10-08 review of jichi at master `17bbb895` (M913); start at
[Review 0](2026-10-08-review-00-overview.md). This chapter rests on the complete
report for `src/net/`, `src/provider/`, `src/json/`, `src/config/` and
`src/platform/`: about 10,500 lines read in full. Its six code excerpts were
checked line for line against the source, and its highest finding was re-read
in the source. Written by Claude Opus 5.5 on threadwork, 2026-10-08.*

---

## Who this is for, and what you will be able to do

**For** a self-learner or junior developer who wants to see how a C program
talks to a language model over HTTP: building the request, reading a stream of
events, telling one kind of failure from another, and deciding which
configuration to trust.

**After reading it you will be able to:**

- follow a request from the agent loop to the server and the streamed reply
  back into a message;
- explain why a streaming parser needs state that survives between calls;
- describe jichi's configuration trust model: what an untrusted project config
  may change, and why unknown keys are dropped;
- name the four ways a network call can fail in jichi, and why they are told
  apart.

**What you need first:** [Annai 6](../reading/annai-06-the-wire.md) is the
guided tour of the wire, and
[Fukabori 2](../reading/fukabori-02-the-provider-abstraction.md) argues the
provider design. [Fukabori 8](../reading/fukabori-08-streaming-and-the-no-buffering-invariants.md)
is the deep dive on streaming.

---

## 1. The five parts

- **Platform** (`src/platform`): portability shims for C89:
  - `jc_status`, the result every fallible function returns;
  - file helpers, among them `jc_write_file_atomic` (§3.5);
  - `jc_home_dir`, clocks;
  - `jc_sleep_ms`, which sleeps in 50 ms slices so an abort is honoured;
  - an advisory file lock, disk-space probes;
  - `jc_snprintf`, bounded formatting that C89 itself lacks.
- **JSON** (`src/json`): `cJSON.c` is an original C89 implementation of the
  cJSON *API* (not a copy of that library): a recursive-descent parser building
  a tree of nodes, and a printer. `jc_json.c` adds null-safe getters, among them
  the strict and lenient boolean readers that CLAUDE.md's rule is about.
- **Configuration** (`src/config`): the loader merges the global `~/.jichi` with
  a project file. A project file arrives with a `git clone`, so it passes the
  **trust step** (§2). `jc_configedit.c` is the writer behind `jichi config set`.
- **Providers** (`src/provider`): a table of function pointers (a *vtable*) with
  two dialects, OpenAI chat completions and Anthropic messages:
  - `build_request` turns the history, the system prompt and the tools into a
    JSON body;
  - `on_event` consumes one streamed event.

  The agent loop never branches on which provider it talks to.
- **Network** (`src/net`): `jc_http.c` wraps libcurl:
  - one reused connection per process;
  - three timeouts: connect, stall, and the dead stream;
  - no redirects on requests that carry a key;
  - an SSRF guard for URLs a model supplies.

  `jc_sse.c` turns arbitrary byte chunks into server-sent events. The rest are
  small clients for embeddings, reranking, speech and images.

**One request, end to end:**

```
 agent loop
   | build_request(history, system, tools) ---> provider (openai | anthropic)
   v                                               | body = JSON text
 jc_http_stream(request) --libcurl--> server --bytes--> write callback
                                                  -> jc_sse_feed -> events
                                                  -> on_event(provider)
                                                  -> text, tool calls, usage
                                                  -> the assistant message
 failure: jc_http_classify(ok, aborted, timed_out, connected)
          -> OK / HTTP error / TIMEOUT / ABORTED
```

**Invariants worth knowing:**

- the request body is owned by the HTTP layer once it starts uploading, and
  freed when the upload ends;
- the provider holds a pointer to `config.model`, which is refreshed in place
  when the model changes;
- the JSON parser bounds nesting at 256 levels;
- a server-sent-event line is capped at 4 MB;
- `max_tokens` is always sent.

---

## 2. Which configuration to trust

A project's `.jichi/config.json` arrives with a `git clone`, written by whoever
wrote the repository. It could turn fences off. So unless the user has trusted
that exact file (its absolute path plus its SHA-256, recorded in
`~/.jichi.d/trusted-configs.json`), it is filtered key by key:

| Class | Example meaning | From an untrusted file |
|---|---|---|
| **SAFE** | a cosmetic or harmless setting | applies |
| **NARROW** | a bound: `maxSubagentIters`, `maxToolIters` | applies only in the stricter direction |
| **GATED** | something that writes outside the project, such as `logging` | dropped and listed |
| *unknown* | a key the table does not know | dropped and listed |

**The design is fail-closed:** a key nobody classified is dropped, so a new
config key cannot become a hole by being forgotten. A lint holds the trust table
against the keys the loader reads. §3.6 shows the filter. Finding F5 shows its
known limit: a key is classified on its own, but one key can change another's
default.

---

## 3. Code to study

Six excerpts, each copied exactly from `17bbb895`.

### 3.1 State across chunk boundaries — `src/net/jc_sse.c:89-114`

```c
    for (i = 0; i < n; i++) {
        char c = bytes[i];
        if (c == '\r') {
            /* WHATWG allows three line endings: CRLF, LF, and a bare CR not
             * followed by LF. So a CR ends a line here and now. Remember it, so
             * that the LF of a CRLF -- which may arrive in the NEXT chunk -- is
             * swallowed rather than ending a second, empty line. That one bit of
             * state is what keeps the every-chunk-size property (M775). */
            handle_line(p, (p->line.data != NULL) ? p->line.data : "",
                        p->line.len);
            jc_sb_clear(&p->line);
            p->last_was_cr = 1;
        } else if (c == '\n') {
            if (p->last_was_cr) {
                p->last_was_cr = 0; /* the LF of a CRLF; the CR already ended it */
            } else {
                handle_line(p, (p->line.data != NULL) ? p->line.data : "",
                            p->line.len);
                jc_sb_clear(&p->line);
            }
        } else {
            p->last_was_cr = 0;
            if (p->line.len < (jc_size)JC_SSE_FIELD_MAX) {
                jc_sb_append_char(&p->line, c);
            } else {
                p->overflowed = 1; /* drop bytes of a pathologically long line */
```

**What it teaches.** A network delivers bytes in arbitrary pieces. A streaming
parser must produce the same events whether the input arrives whole or one byte
at a time. That needs **state that survives between calls**.

- **The one bit of state.** `last_was_cr` stops a CR LF line ending that is
  split across two chunks from ending *two* lines. An empty second line would be
  a false event boundary.
- **The bound.** The line buffer is capped, so a hostile stream that never sends
  a newline cannot grow memory without limit.
- **The test.** The test that pins this feeds the same bytes at every chunk size
  and compares the events.

**A lesson the code teaches by omission** (F15): `overflowed` is set and nobody
reads it. A limit that fires silently is only half a fence.

### 3.2 Bound recursion at its single entry — `src/json/cJSON.c:509-523`

```c
        case '[': {
            /* The ONLY recursion in this parser, so the depth bound lives here
             * rather than in parse_object/parse_array -- each of those has five
             * return points, and a counter decremented at five of them is a
             * counter that leaks at the sixth someone adds (M472). */
            cJSON *v;
            if (c->depth >= JC_JSON_MAX_DEPTH) {
                c->ok = 0;
                return NULL;
            }
            c->depth++;
            v = (*c->p == '{') ? parse_object(c) : parse_array(c);
            c->depth--;
            return v;
        }
```

**What it teaches.** In a recursive-descent parser, the *input* decides how deep
the C stack goes. On a small stack, a few kilobytes of `[` once crashed the
process.

**The lesson is where the limit sits.** The counter is incremented and
decremented around the *one* call that recurses, so no early return inside
`parse_object` or `parse_array` (each has five) can leak it. The error travels
out of deep recursion through the context (`c->ok = 0`), the C89 way.

### 3.3 A pure classifier, split from its I/O — `src/net/jc_http.c:279-294`

```c
jc_status jc_http_classify(int ok, int aborted, int timed_out, int connected)
{
    if (ok) {
        return JC_OK;
    }
    if (aborted) {
        return JC_ERR_ABORTED;
    }
    /* A timeout *after* the connection was established means the server took us
     * but then stopped sending -- a stalled/too-slow model. A timeout during
     * connect (or any other failure) is a transport/network condition. */
    if (timed_out && connected) {
        return JC_ERR_TIMEOUT;
    }
    return JC_ERR_HTTP;
}
```

**What it teaches.** "What kind of failure was this?" is decided from four plain
integers, so it can be tested without libcurl or a network.

The **precedence is the policy**: an abort wins over a timeout, and a timeout
counts as a stalled model only if a connection existed. That distinction sends a
stalled model to the stronger tier, and retries a failed connection as network
trouble.

**The caveat** (finding F3): a pure core is only as correct as the facts its I/O
shell hands it. When the streaming callback aborts, the shell never calls this
function, and the abort is reported as success.

### 3.4 Repair the wire without lying to the model — `src/provider/jc_provider.c:191-217`

```c
char *jc_prov_args_wire(const char *args_json)
{
    cJSON *parsed;
    cJSON *wrap;
    char *out;

    if (args_json == NULL || args_json[0] == '\0') {
        return jc_strdup("{}");
    }
    parsed = cJSON_Parse(args_json);
    if (parsed != NULL) {
        int ok = cJSON_IsObject(parsed);
        cJSON_Delete(parsed);
        if (ok) {
            return jc_strdup(args_json);
        }
        /* Valid JSON but not an object (a bare string, array or number): the
         * server expects an object, so wrap it like the unparseable case. */
    }
    wrap = cJSON_CreateObject();
    if (wrap == NULL) {
        return jc_strdup("{}");
    }
    cJSON_AddStringToObject(wrap, "_unparsed_arguments", args_json);
    out = cJSON_PrintUnformatted(wrap);
    cJSON_Delete(wrap);
    return (out != NULL) ? out : jc_strdup("{}");
```

**What it teaches.** A model's broken tool-call arguments stay in the history
and are re-sent with every later request. A strict server rejects the whole
request every time, and the run is stuck.

**This function keeps the request *valid*:** the arguments are always an object.
**It also keeps it *truthful*:** the raw text survives under a self-describing
key, instead of a silent `{}` that would contradict the tool error the model
reads next. When the text is already an object it is returned *as is*, not
reprinted, which avoids the number-rounding problem of F10.

### 3.5 Atomic replacement in strict C89 and POSIX — `src/platform/jc_platform_posix.c:354-378`

```c
    jc_snprintf(tmp, sizeof(tmp), "%s.tmp%ld", path, (long)getpid());
    fd = open(tmp, O_CREAT | O_EXCL | O_WRONLY | JC_O_BINARY, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        remove(tmp);
        fd = open(tmp, O_CREAT | O_EXCL | O_WRONLY | JC_O_BINARY, S_IRUSR | S_IWUSR);
    }
    if (fd < 0) {
        return JC_ERR_IO;
    }
    f = fdopen(fd, "wb");
    if (f == NULL) {
        close(fd);
        remove(tmp);
        return JC_ERR_IO;
    }
    put = fwrite(data, 1, len, f);
    if (fclose(f) != 0 || put != len) {
        remove(tmp);
        return JC_ERR_IO;
    }
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return JC_ERR_IO;
    }
    return JC_OK;
```

**What it teaches.** `rename()` within one directory is atomic: a reader sees
the old file or the new one, never half of it.

- **Why the temp name uses the process id.** `mkstemp` belongs to an extension
  (XSI) of strict POSIX-2001, so the name is built from the pid, and `O_EXCL`
  refuses a name that already exists.
- **No temp file is left behind.** Every error path removes it.
- **`fclose` is checked.** Buffered write errors surface only there, and a
  common bug is to ignore its result.

**The trade-offs are stated in the header:**

- the file is created 0600, which is right for private records and wrong for
  workspace files, so those use another function;
- there is no `fsync`, so this protects against a crashed process, not a power
  cut.

### 3.6 A fail-closed filter over a linked list — `src/config/jc_config_trust.c:457-481`

```c
    it = proj->child;
    while (it != NULL) {
        cJSON *next = it->next;
        const struct key_class *k = class_entry(it->string);
        if (k != NULL && k->cls == JC_CFGTRUST_SAFE) {
            it = next;
            continue;
        }
        detach(proj, it);
        if (k != NULL && k->cls == JC_CFGTRUST_NARROW && narrow != NULL) {
            /* Copy the key first: our cJSON_AddItemToObject frees the item's
             * own key before copying the new one, so passing it->string would
             * read freed memory. */
            char keybuf[96];
            jc_snprintf(keybuf, sizeof keybuf, "%s",
                        it->string != NULL ? it->string : "");
            cJSON_AddItemToObject(narrow, keybuf, it);
        } else {
            char line[900];
            jc_cfgtrust_describe(it->string, it, line, sizeof line);
            push_line(ignored, a, line);
            cJSON_Delete(it);
        }
        it = next;
    }
```

**What it teaches.** Three habits in 25 lines:

1. **Fail closed.** Only a key the table knows as SAFE stays. An unknown key
   falls into the "ignored" branch.
2. **Iterate safely while changing the list.** `next` is saved before `detach`
   unlinks the node.
3. **Read the library's aliasing contract.** This cJSON frees an item's old key
   inside `AddItemToObject`, so passing `it->string` as the new key would read
   freed memory. The copy into `keybuf` is the fix.

---

## 4. What the review found

Fifteen findings: one high, four medium, the rest low. Status is the
reviewer's.

### High

- **F1 — `config set` wipes a config file that has comments** (verified by the
  reviewer, and re-read in the source).
  - **The cause.** The loader accepts `//` comments, but the writer's loader
    (`jc_config_edit_load`, `src/config/jc_configedit.c:92-114`) parses the file
    as strict JSON. A file with comments fails to parse and is treated as
    *empty*, and the writer saves only the one key being set.
  - **What the reviewer saw.** A trusted project config lost its model,
    `pathFence`, `permissions.deny` and `configEditable`. The file **stayed
    trusted** in its new, wiped form, and only one backup copy is kept.
  - **The fix,** about an hour: use the loader's comment-aware reader, and refuse
    to save over a non-empty file that cannot be parsed.

### Medium

- **F2 — a server error sent inside a running stream becomes the model's
  answer** (`jc_provider_openai.c:396-405`, `jc_provider_anthropic.c:455-461`).
  An Anthropic "Overloaded" error is not retried, and a context overflow reported
  this way is not recognised. The vtable has no channel for "the server reported
  an error", so errors are laundered into text. *Lesson:* if your interface can
  only carry success, failures will be disguised as success.
- **F3 — an abort caught by the streaming callback is reported as success,**
  HTTP 200 (`src/net/jc_http.c:1165-1184`; confirmed with a loopback test).
- **F4 — the default 30-second stall timeout also fires before the first byte**
  (`src/net/jc_http.c:966-986`; confirmed with a loopback test). A local model
  that needs more than 30 s to process a long prompt is aborted as "stalled" and
  retried, which is exactly where long runs operate.
- **F5 — gaps in the trust table** (security class). Some keys an untrusted
  project config may set can loosen a fence or a safety net, because one key can
  change another's default; and narrowing a bound to 0 can widen one path. The
  details are kept with the maintainers.

### Low (selected)

- **F7** — a malformed `~/.jichi` is fatal on its own, but silently dropped when
  a project config exists.
- **F8** — the context-overflow classifier knows three spellings, and only looks
  at HTTP 400.
- **F9** — wire and config numbers are cast to `int` or `long` without a range
  check: undefined behaviour, confirmed with the undefined-behaviour sanitizer.
- **F10** — the JSON printer is lossy for non-integers (`%g`), a correctness
  problem for every writer that round-trips.
- **F12** — a server-supplied image URL is fetched without the private-address
  guard (security class).
- **F14** — streamed OpenAI tool calls without an `index` merge into slot 0, and
  calls beyond 32 vanish silently.

---

## 5. The design, seen whole

**What is strong.**

- **`jc_http.c` is the most carefully hardened file in the slice:**
  - no redirects on credentialed requests;
  - a stated TLS floor;
  - close-on-exec on every socket;
  - an SSRF guard at connect time;
  - a process-id-stamped connection handle for fork safety;
  - pure helpers outside the libcurl guard, for testing.
- **The trust model is well built:** bound to content hashes, fail-closed, its
  store outside the workspace, and every withheld key described in plain words.
- **The provider vtable is clean:** the loop truly never branches on dialect.

**Where the defects sit.** All five verified defects are at the edges between a
pure core and its I/O shell, which is also where the unit tests stop:

- a commented file through `config set`;
- an abort through libcurl's callback;
- a slow first byte;
- a key that changes another key's default;
- an out-of-range number on the wire.

*Lesson:* unit-testing the pure core is necessary and not sufficient; the
seams need an end-to-end test of their own.

**What the reviewer would refactor first, with the cost:**

1. **Make `config set` read comments, and refuse an unparseable file:** an hour
   and one red-first test. It removes a data-loss path.
2. **An error channel in the provider vtable**, through to the agent loop, with
   overflow recognised in-stream: half a day across both providers, plus
   mock-server tests.
3. **Report a callback abort as an abort,** and check for an abort before tools
   run: an hour.
4. **Table-driven configuration:** one row per key, holding its type, its normal
   and lite defaults, its clamp range and its trust class. That one source would
   feed the reader, the trust table, the lint and the docs, and retire F5, F6 and
   F9 as classes. Two to three days.

**On numbers.** Booleans got the lenient rule; numbers did not. They are read
strictly and without a clamp, and a quoted number is silently ignored. One
`jc_json_get_long_clamped(obj, key, default, lo, hi)`, warning once per key on a
wrong type, would cover both problems.

---

## 6. What this means for the context relay

From the reviewer's relay section ([proposal](../proposals/2026-10-context-relay.md)
§6.15, §8.1):

- **Measure pressure on the full prompt:** uncached input plus cache reads plus
  cache writes, as the loop already does for `last_prompt_tokens`.
- **The handover call is late, with a long prompt.** It must fit its own
  `max_tokens` (def-303), and it is exactly the kind of call F4's stall timeout
  aborts.
- **New config keys need trust classes.** Turning the relay on, or raising its
  number of legs, widens an autonomous run, so those keys are NARROW. A notes
  directory outside `~/.jichi.d` is GATED. Flat top-level keys are preferable,
  because the trust filter classifies only top-level keys.
- **Never persist a self-adjusted budget through `config set`.** It wipes
  commented files (F1), rounds numbers (F10), and would record a model's change
  as the user's own trusted edit.

---

## 7. Something to do

**Without a bench (the read-only twin):**

1. In §3.1, a stream is cut after a `\r`, and the next chunk starts with `\n`.
   Trace both calls of the loop with `last_was_cr`. How many lines end? Now
   delete the `if (p->last_was_cr)` branch. How many now?
2. In §3.3, fill in the 16 rows of `(ok, aborted, timed_out, connected)` and the
   status each returns. Which rows can never happen in practice?
3. In §3.6, why is `next` saved *before* `detach`? Write the bug the loop would
   have without it.

**With a bench:** see F1's mechanism without risking a real config, in a fresh
temporary directory:

```sh
cd "$(mktemp -d)"
printf '{\n  // a comment\n  "maxToolIters": 50\n}\n' > commented.json
python3 -c 'import json,sys; json.load(open(sys.argv[1]))' commented.json
```

Run on threadwork on 2026-10-08, the last line of the error was:

```
json.decoder.JSONDecodeError: Expecting property name enclosed in double quotes: line 2 column 3 (char 4)
```

A strict parser refuses the file. That is the step at which `config set` decides
the file is empty, and writes a new one holding only the key being set.

## 8. Read further

- [CONFIG_TUTORIAL.md](../CONFIG_TUTORIAL.md), [ROUTING.md](../ROUTING.md),
  [PROMPT_CACHING.md](../PROMPT_CACHING.md): the references. *Primary for jichi.*
- [Annai 6](../reading/annai-06-the-wire.md),
  [Fukabori 2](../reading/fukabori-02-the-provider-abstraction.md),
  [Fukabori 8](../reading/fukabori-08-streaming-and-the-no-buffering-invariants.md).
  *Primary for jichi.*
- The WHATWG HTML Living Standard, "Server-sent events": the line-ending rules of
  §3.1. *Primary.*
- RFC 8259, *The JavaScript Object Notation (JSON) Data Interchange Format*,
  which allows no comments: why a strict reader refuses F1's file. *Primary.*
- OWASP, "Server Side Request Forgery": the class of F12. *Secondary.*
