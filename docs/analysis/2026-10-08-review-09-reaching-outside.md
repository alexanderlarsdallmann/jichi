# Review 9 — Reaching outside: the index, LSP, MCP, the converter and the scaffolds

*Part of the 2026-10-08 review of jichi at master `17bbb895` (M913); start at
[Review 0](2026-10-08-review-00-overview.md). This chapter rests on a report that
was **paused** at the operator's request. All the code of the slice had been
read: `src/index`, `src/lsp`, `src/mcp` and `src/convert` in full, and the
scaffold code with part of its templates. Its findings were written in full. Its
explanation was a draft, and it contained no code excerpts. §5 lists what was not
reviewed. The two excerpts below were chosen from the reviewer's candidate list
and copied from the source by a script. Written by Claude Opus 5.5 on
threadwork, 2026-10-08.*

---

## Who this is for, and what you will be able to do

**For** a self-learner or junior developer who wants to see how a coding agent
connects to the world outside its conversation:

- a semantic index of the codebase;
- a language server for diagnostics and refactoring;
- external tool servers (MCP);
- a converter for other tools' configurations;
- the project templates jichi writes.

**After reading it you will be able to:**

- describe how jichi's search pipeline combines embeddings with keyword search;
- read a `Content-Length` framed protocol reader, and say why it needs *two*
  limits;
- explain the UTF-16 column problem that every LSP client must solve;
- explain how a timing bug made a broken file look clean.

**What you need first:** [REPOMAP.md](../REPOMAP.md), [RAG.md](../RAG.md),
[LSP.md](../LSP.md) and [MCP.md](../MCP.md) are the references.

---

## 1. Five ways out

- **The index and retrieval** (`src/index`).
  - **Building.** `jc_index` walks the workspace, cuts each text file into chunks
    of about 1,500 bytes along line boundaries, asks an embedding model for one
    vector per chunk, and caches the vectors under `~/.jichi.d/index/`. A rebuild
    re-embeds only the files that changed.
  - **Searching.** One pipeline, `jc_retrieve`:
    1. embed the query;
    2. take the nearest chunks by cosine similarity;
    3. take the best chunks by a simple keyword score (BM25);
    4. merge the two lists by *Reciprocal Rank Fusion*;
    5. optionally rerank;
    6. trim.
  - **The repo map** (`jc_repomap`) is different: no embeddings. A heuristic
    scanner lists each source file and its top-level symbols into a section of
    the system prompt, bounded by bytes (12 KB by default).
- **The LSP client** (`src/lsp`).
  - It starts a configured *language server*, a separate program such as clangd
    that understands one language.
  - It talks JSON-RPC to it, framed by `Content-Length:` headers (§2.1).
  - It opens files, collects the diagnostics the server publishes, and runs
    definition, references, rename, format and code actions.
- **The MCP client** (`src/mcp`).
  - It connects to configured *Model Context Protocol* servers, over stdio with
    one JSON object per line, or over HTTP.
  - It lists their tools, and registers each one as a jichi tool named
    `<server>__<tool>`.
- **The converter** (`src/convert`, the `jichi-convert` binary) translates a
  Continue, Claude Code or opencode configuration into jichi's.
- **The scaffolds** (`src/scaffold`) are the language and domain packs `jichi
  init` writes: an `AGENTS.md`, agent profiles, commands and skills.

```
 model tool call
   |  codebase_search / search_docs        edit_file/write_file        <srv>__<tool>
   v                                             | (after the write)          |
 jc_search / jc_docs --> jc_retrieve        diagnostics appended         MCP tool
   |   (one index per process)                   v                            v
   v                                        jc_lsp_diagnostics          request
 jc_index_build <-> ~/.jichi.d/index/         | didClose + didOpen        stdio | http
   (walk, chunk, embed, cache)                v Content-Length JSON-RPC   lines | POST+SSE
                                            language server (child)     MCP server
```

---

## 2. Code to study

### 2.1 A framed reader needs two limits — `src/lsp/jc_lsp_proto.c:99-141`

```c
    hdr_len = (jc_size)-1;
    for (i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n') {
            hdr_len = i;
            break;
        }
    }
    if (hdr_len == (jc_size)-1) {
        /* M609: header block not terminated yet. If it has grown past the cap,
         * the terminator is not coming (a hostile or broken server): drop
         * everything and resync, rather than buffer toward an OOM. */
        if (len > (jc_size)JC_LSP_MAX_HEADER) {
            f->buf.len = 0;
            if (data != NULL) {
                data[0] = '\0';
            }
        }
        return 0; /* header block incomplete */
    }
    clen = find_content_length(data, hdr_len);
    if (clen == (jc_size)-1) {
        /* Malformed header block: drop it to resync. */
        jc_size after = hdr_len + 4;
        memmove(data, data + after, len - after);
        f->buf.len = len - after;
        data[f->buf.len] = '\0';
        return 0;
    }
    if (clen > (jc_size)JC_LSP_MAX_BODY) {
        /* Refuse and resync rather than buffer toward it (M472). Dropping the
         * header block is what the malformed-header path above already does, so
         * a server that recovers is not permanently wedged by one bad frame. */
        jc_size after = hdr_len + 4;
        memmove(data, data + after, len - after);
        f->buf.len = len - after;
        data[f->buf.len] = '\0';
        return 0;
    }
    total = hdr_len + 4 + clen;   /* both bounded now: no overflow possible */
    if (len < total) {
        return 0; /* body not fully arrived yet */
    }
```

**What it teaches.** LSP messages are framed like HTTP: a header block, a blank
line (`\r\n\r\n`), then exactly `Content-Length` bytes of JSON. A reader must
handle three things: a header that has not finished arriving, a body that has
not finished arriving, and a peer that lies.

**The two limits answer two different attacks:**

1. **A header that never ends.** Without the first cap, a broken or hostile
   server that sends header bytes forever would make jichi buffer toward running
   out of memory. With it, the reader drops everything and resynchronises.
2. **A header that promises a huge body.** Without the second cap, a server could
   announce a gigabyte and make jichi wait and buffer for it. With it, the frame
   is refused and dropped.

**One line is worth reading twice:** `total = hdr_len + 4 + clen; /* both bounded
now: no overflow possible */`. The addition is safe *only because* both terms
were checked first. That is how size arithmetic should be argued: by stating why
it cannot wrap.

Also notice that every malformed case *resynchronises* (drops the bad frame and
carries on) instead of wedging, so one bad frame does not end the session.

### 2.2 The same problem, line-framed — `src/mcp/jc_mcp_stdio.c:164-196`

```c
static int pop_line(struct jc_sb *buf, jc_size *scanned, char **out)
{
    char *nl;
    jc_size i;

    if (buf->data == NULL || *scanned >= buf->len) {
        if (buf->data != NULL) {
            *scanned = buf->len;
        }
        return 0;
    }
    nl = (char *)memchr(buf->data + *scanned, '\n', buf->len - *scanned);
    if (nl == NULL) {
        *scanned = buf->len;
        return 0;
    }
    i = (jc_size)(nl - buf->data);
    {
        char *line = (char *)malloc(i + 1);
        if (line != NULL) {
            memcpy(line, buf->data, i);
            line[i] = '\0';
        }
        /* Shift the remainder down. */
        memmove(buf->data, buf->data + i + 1, buf->len - i - 1);
        buf->len -= i + 1;
        buf->data[buf->len] = '\0';
        /* The shifted remainder has never been examined. */
        *scanned = 0;
        *out = line;
        return line != NULL;
    }
}
```

**What it teaches.** MCP over stdio uses a simpler framing, one JSON object per
line. Compare it with the ACP reader in
[Review 8](2026-10-08-review-08-tui-setup-acp.md) §2.3: the same remembered
`scanned` offset makes it linear, the same `memmove` handles overlapping memory,
and the same comment states an invariant: *the shifted remainder has never been
examined*, so the scan restarts at 0.

**A question for you:** where is the cap on a line that never ends? (It lives in
the caller, `read_line`, and the reviewer's finding F10 lists the MCP robustness
gaps.)

---

## 3. What the review found

Twenty-one findings: two high, eleven medium, eight low. Status is the
reviewer's. The LSP claims marked verified were checked against a real clangd.

### High

- **F1 — after an edit, the diagnostics can say "No diagnostics." for a file that
  has errors** (verified against a real clangd).
  - **The mechanism.** When jichi re-checks a file, it closes and reopens it.
    clangd first publishes an *empty* set for the closed document, then the real
    set 0.3–0.5 s later. jichi takes the empty set as the latest, and stops
    listening at the next whole second, because the wait is timed with a clock
    that has one-second resolution.
  - **What the reviewer measured.** Started at the wrong phase of the second,
    five re-checks in a row of a file containing an undeclared name reported "No
    diagnostics.". The model sees a clean edit.
  - **It gets worse on slower hardware.** Slower machines and larger files make
    it more likely, not less.
  - **The fix.** Time the wait with the monotonic millisecond clock, and ignore
    the empty set that belongs to the close.
  - *Lesson:* a timeout measured on a one-second clock has a random error of up
    to a second. Choose the clock for the precision you need.
- **F2 — LSP edits are applied at byte columns, but servers send UTF-16 columns**
  (verified with clangd's formatting).
  - **The cause.** The LSP specification counts a position's character in UTF-16
    code units unless the client negotiates otherwise. jichi counts bytes. Each
    two-byte UTF-8 character before an edit shifts every later edit on that line
    by one.
  - **What the reviewer saw.** Formatting `int f(void) { const char *s = "Größe";
    return s[0]; }` ate the characters `";` and `rn`.
  - **Who meets it.** Any formatted, renamed or code-action file with non-ASCII
    text. Under `--auto` the write is approved automatically.
  - **The fix.** Advertise `positionEncodings: ["utf-8", "utf-16"]` and honour
    what the server picks.

### Medium (selected)

- **F3** — a language server that stops reading its input blocks jichi forever,
  and Ctrl-C cannot interrupt the write.
- **F4** — `spawn_parallel` leaks the whole resident codebase index on every
  batch.
- **F5** — rewriting the index cache truncates a file another jichi process has
  memory-mapped: a SIGBUS, or silently wrong vectors.
- **F7** — the index is built once per process and never refreshed, so a long
  session searches a stale codebase.
- **F8** — the language server inherits jichi's API keys: its environment is
  not scrubbed, unlike that of MCP servers (security class).
- **F9** — two MCP servers offering a tool of the same name both register it,
  and the second becomes unreachable.
- **F13** — `jichi-convert` silently drops YAML *flow* lists (`[a, b]`). Roles and
  MCP arguments vanish, and an embedding model can become the active chat model.
- **F14** — a converted provider entry without an `apiBase` addresses a public
  third-party endpoint by default, so prompts can go where the user did not
  intend (security class).

### Low, and the scaffolds (selected)

- **F18** — a scaffold pack's coach names a tool that does not exist
  (`docs_search`; the tool is `search_docs`). No test checks tool names in
  shipped templates.
- **F19** — read-only scaffold profiles are told to do what read-only mode
  refuses: two agents are promised the shell, and four commands are told to
  write their document.
- **F20** — the measured context-hygiene rules reach only the default pack: 31 of
  the 32 scaffolded `AGENTS.md` files leave them out.
- **F21** — six scaffolded skills list their allowed tools inline, a form the
  loader silently reads as *no tools*.

**What F18–F21 have in common:** text that tells a *model* what to do is code
too, and it rots like code when nothing checks it.

---

## 4. The design, seen whole

**What this slice does well** (the reviewer's notes, where written):

- the retrieval pipeline is one function, used by both search tools;
- the LSP framer is pure, with its caps tested;
- MCP tools are registered as ordinary jichi tools, so permissions and fences
  apply to them unchanged.

**Two lessons from the findings:**

- **Protocol peers are adversaries for robustness, even friendly ones.** A
  language server that hangs (F3) or publishes in an unexpected order (F1) is not
  malicious. It is still a peer whose behaviour jichi does not control, so every
  wait needs a precise clock, a bound and an abort.
- **Text for models needs tests too.** The scaffold findings are all "the
  template says something the program does not do". A lint that checks every
  tool name and fence named in a shipped template against the real registry
  would catch F18, F19 and F21 as a class.

---

## 5. What was not reviewed

- The scaffold templates in `src/scaffold/jc_scaffold.c`, lines 420–5733, most of
  the language packs' text. The findings F18–F21 come from the part that was
  read.
- `docs/SCAFFOLDING.md`.
- The report's design-observation and open-question sections, which were not
  written before the pause.

---

## 6. What this means for the context relay

From the reviewer's draft notes:

- **A fresh session rebuilds the repo map,** which is cheap and fresher. It is
  rendered into every request's system prompt, bounded at 12 KB by default (about
  3–4k tokens).
- **MCP tool descriptions and schemas are registered verbatim, with no size
  limit,** and are sent with every request. They are part of a fresh leg's fixed
  cost, which the relay's budget arithmetic must include in its starting prompt
  P0 ([proposal](../proposals/2026-10-context-relay.md) §6.10).
- **The index's disk cache survives a relay.** A warm rebuild re-walks and
  re-chunks, but does not re-embed.
- **Language servers and MCP connections belong to the process.** An in-process
  relay keeps them. A relay through a new process (topology A) must start them
  again; MCP starts its servers one after another, with up to 120 s each.

---

## 7. Something to do

**Without a bench (the read-only twin):**

1. In §2.1, write three byte streams: one whose header never ends, one whose
   header promises 2 GB, and one that is valid but arrives in two halves. Trace
   `jc_lsp_framer_pop` for each. Which `return 0` does each take, and what is
   left in the buffer?
2. For F2, take the line `s = "Größe"; x = 1;`. Count the column of `x` in UTF-8
   bytes and in UTF-16 code units. By how much does a byte-based client miss?
3. For F1, a check starts at 12.7 s on a one-second clock with a 0.4 s grace. At
   which time does it stop listening? If the real diagnostics arrive at 13.1 s,
   does it see them?

**With a bench (the source tree only):** find the tests that pin §2.1's limits,
and the test that is missing.

1. In `tests/test_lsp.c`, read the block at lines 59–76: it pushes more than
   `JC_LSP_MAX_HEADER` bytes of a header that never ends, and checks that the
   buffer was dropped. Read the block from line 384, the bound on
   `Content-Length`.
2. Now search the LSP tests for any non-ASCII text:
   `/usr/bin/grep -n -i 'utf' tests/test_lsp*.c`. On `17bbb895` it finds
   nothing. A position after a two-byte character is exactly the test F2 needs,
   written before its fix.

## 8. Read further

- [REPOMAP.md](../REPOMAP.md), [RAG.md](../RAG.md), [LSP.md](../LSP.md),
  [MCP.md](../MCP.md), [CONVERT.md](../CONVERT.md),
  [SCAFFOLDING.md](../SCAFFOLDING.md): the references. *Primary for jichi.*
- The Language Server Protocol specification, version 3.17, "Base Protocol" and
  "Position" (the UTF-16 default and `positionEncodings`). *Primary.*
- The Model Context Protocol specification (modelcontextprotocol.io). *Primary.*
- Gordon V. Cormack, Charles L. A. Clarke and Stefan Büttcher, "Reciprocal Rank
  Fusion outperforms Condorcet and individual Rank Learning Methods", SIGIR 2009:
  the fusion step of §1. *Primary.*
- Stephen Robertson and Hugo Zaragoza, "The Probabilistic Relevance Framework:
  BM25 and Beyond", *Foundations and Trends in Information Retrieval*, 2009.
  *Secondary.*

---

## Completed on 2026-10-17 (at e5d832a4)

*This section finishes the review above, which was paused on 2026-10-08 at
`17bbb895` (M913). It does not rewrite the chapter: people may have acted on
it, so what changed is said here, next to it. Written by Claude Opus 5.5 on
2026-10-17 against master `e5d832a4`, as part two of the
[2026-10-17 hardening review](2026-10-17-hardening-00-overview.md) ("Phase 1"
below), whose finding ids (A-04, C-05, D-27, ...) are used here. Every earlier
finding was re-read in the code at `e5d832a4`; twelve were re-measured. Every
finding marked "measured" ran in a private copy of the tree: offline, no
model, scripted mock peers from the repository's own test tools
(`tests/tools/mockmodel`, `mocklsp`, `ptydrive`) and, for the language-server
claims, a real clangd 23 on the review workstation.*

**Who this section is for:** the same reader as the chapter, a self-learner or
junior developer. **After reading it you will be able to:**

- say which of the chapter's 21 findings are fixed, which still stand, and
  which changed shape;
- explain why "the position of a symbol" is the hardest part of a
  language-server client, and how guessing it went wrong twice;
- explain what a *process group* is, and why a Ctrl-C meant for jichi also
  reaches the programs jichi started;
- explain why a list a program could not read must never count as "no
  restriction";
- read the ordered list of what to fix next.

**What you need first:** the chapter above (§1 and §3), and the reference
pages [LSP.md](../LSP.md), [MCP.md](../MCP.md), [RAG.md](../RAG.md) and
[CONVERT.md](../CONVERT.md).

### A. What happened between the two dates

About 45 milestones landed (M914 to M959). In this chapter's code only three
commits touched anything, all in the language-server client: **M938** fixed
F1's clock and document versions, **M939** fixed F2's UTF-16 columns, and a
follow-up fixed one uninitialised byte that valgrind found in M939's own code.
`src/index`, `src/mcp`, `src/convert` and `src/scaffold` did not change at
all, so the chapter's line numbers for them are still exact. The context
relay shipped meanwhile (`--handoff-in`, and "topology B", where several legs
of one task run inside one process); §E says what that means here.

### B. The chapter's findings, one by one

| Finding | Then | Now (at `e5d832a4`) |
|---|---|---|
| F1 diagnostics read a broken file as clean | high | **Partly fixed by M938, still open as medium.** The wait now uses the monotonic millisecond clock and a real 2.0 s grace, and each re-open sends a higher document version. Measured: when the server's real answer comes 0.6, 1.4 or 1.9 s after its first (empty) one, the error is reported; at 2.1, 2.6 or 4.0 s, jichi answers `No diagnostics.` after 2.02 s. A slow server on a slow board still produces a false clean. |
| F2 byte columns vs UTF-16 columns | high | **Fixed by M939** (`jc_lsp.c:386-403`, `jc_lsp_proto.c:711-920`, unit test `test_lsp_position_encoding`). Measured: formatting the chapter's `"Größe"` line with clangd now gives the right layout. |
| F3 a server that stops reading blocks jichi | medium | **Still open.** Measured: against a server that stops reading after the handshake, `jichi lsp` on a 300 KB file was still blocked 55 s after a SIGINT (`jc_lsp.c:139-153`; MCP has the same shape, `jc_mcp_stdio.c:120-145`). |
| F4 `spawn_parallel` leaks the resident index | medium | **Still open** (`jc_tool_parallel.c:2133-2139`, `jc_index.c:714, 1076`). |
| F5 cache rewrite under another process's mapping | medium | **Still open** (`jc_index.c:512-531`). |
| F6 HTML comments drop text | medium | **Still open.** Re-measured: byte-for-byte the 2026-10-08 outputs (`jc_docs.c:158-178`). |
| F7 the index is never refreshed | medium | **Still open, and wider:** a topology-B relay keeps the same index across all its legs (§E). |
| F8 language servers inherit the API keys (security class) | low | **Still open**; Phase 1 recorded it as C-05 / B-08 and rated it medium. |
| F9 duplicate MCP tool names | low | **Still open.** Measured: two servers of one name advertise the same tool twice; a 65-character server name leaves no room for the tool part, so every tool of that server gets one name. |
| F10 MCP pairing, deadlines, caps | low | **Still open.** Measured: a server request that carries the id jichi is waiting for is taken as the answer, and the server registers zero tools. The HTTP halves are Phase 1's C-06 and C-09. |
| F11 no test of the LSP transport | medium | **Partly fixed:** M938 added the `lsp_diag_grace` driver and the `mocklsp` mock. Still untested: a server that stops reading, a terminal Ctrl-C, unusual file names, rename positions, the fence. |
| F12 smaller LSP/index items | low | End-of-line edits still delete the newline (measured: `"abc\ndef\n"` became `"adef\n"`); a dead server is still never restarted; documents are never closed; the lexical scorer still re-reads every chunk per query term; the docs fetch is still uncapped (Phase 1 C-06). The symlink item is **superseded** by Phase 1's D-27 and D-29 (both high). |
| F13 YAML flow lists dropped | medium | **Still open.** Re-measured with the same config: the embedder is still selected and the MCP arguments still vanish, with no note. |
| F14 a provider without `apiBase` addresses a public vendor endpoint (security class) | medium | **Still open** (converter output measured; runtime read in `jc_config.c:517-519`). |
| F15 smaller converter items | low | **Still open.** Measured: every output pins `"maxToolIters": 25`, and a config holding a literal key is written readable by everyone under the usual umask. |
| F16 `jichi-convert --assets` writes the config where jichi does not look | medium | **Still open.** Measured: in the output directory, `jichi config path` answers `built-in defaults`. (`jichi setup --import` writes to the right place; it can be the model.) |
| F17 opencode: dropped fences, silent loosest posture (security class) | low | **Still open, wider** (N10 below). |
| F18-F21 the scaffold texts | low-medium | **All still open** (exact lines unchanged; F18 to F21 were also recorded by the 2026-09-29 scaffolding review). F21's mechanism reaches further than skills: see N8. |
| Docs drift | -- | Fixed: LSP.md's "(rare in code)". Still stale: REPOMAP.md's two sentences, `jc_lsp.h`'s "diagnostics-only", the MCP client version string, the `spawn_parallel` comment, the scaffold "exactly ONE config" comments, CONVERT.md's "every lossy item is reported". |

So of 21 findings: **1 fixed, 2 partly fixed, 18 still open** (one sub-item
superseded by Phase 1).

### C. New findings

Each block gives severity on the 2026-10-08 scale (high / medium / low /
note), the evidence mark (**[V]** read in the tree, **[M]** measured), what
happens, the smallest correct fix, the test that should go red first, and the
effort (S small, M medium, L large). Two findings are security-class and are
described by class only, as [SECURITY.md](../../SECURITY.md) promises.

#### N1. `rename_symbol` with a line number renames the wrong symbol, and reports the one you asked for — high, [M]

*A position* in the Language Server Protocol is a line and a column. To
rename a symbol, jichi must tell the server *where* the symbol is. The tool
offers an optional `line` "to disambiguate". When it is given, jichi uses
that line **and column 0**, and ignores the symbol's name
(`src/lsp/jc_lsp.c:1018-1023`). Whatever token starts the line is renamed.

Measured with clangd: in a C file whose line 2 is
`Widget make_widget(void) { ... }`, a call
`rename_symbol {"symbol":"make_widget", "line": 2, "new_name":"build_widget"}`
renamed the **type** `Widget` to `build_widget` at all four places, left
`make_widget` alone, and returned
`Renamed 'make_widget' to 'build_widget': 4 edits across 1 file`. Putting the
return type at the start of the line is the most common C style; Python's
module-level `result = compute(...)` has the same shape. In chat mode the
approval prompt shows the requested name, so a person approves one rename and
gets another.

**Fix (S):** with a line, find the name as a whole word *on that line* and use
its column; refuse if it is not there. Before applying, ask the server
(`textDocument/prepareRename`) for the range it would rename and refuse unless
that text equals the symbol. **Red-first test:** a pure unit test of the
line-limited lookup (column 7, not 0), and a `mocklsp` rename handler that
records the request position.

#### N2. A symbol is found by its first mention, even in a comment: "No references found." for a used function — medium, [M]

Without a line, jichi finds the symbol by searching the file's text for the
first whole-word match (`jc_lsp_proto.c:848-884`), comments included. A doc
comment above a function usually names it. Measured with clangd:
`find_references` for `helper` in a file whose first line is
`/* helper() is called below */` answered `No references found.`; without the
comment it listed the definition and the call. A model told that a function
has no references may delete it.

**Fix (S):** take the position from the server's outline
(`textDocument/documentSymbol`, which jichi already calls for `list_symbols`).
**Test:** the comment-first file as a unit test.

#### N3. File names that need escaping in a URI never get their diagnostics — medium, [M]

A language server names files by `file://` URIs, in which a space is written
`%20` and a non-ASCII letter as its escaped bytes. jichi builds the URI
without escaping (`jc_lsp_proto.c:155-164`) and compares the server's answer
as a plain string. Measured with clangd: `plain.c` got its error in 2.04 s;
`my file.c`, `Größe.c` and `hash#1.c` each waited the full **15 s** and got
`no diagnostics received`, which the edit tools drop, so the edit reads as
clean. A logged session showed the cause: jichi sent `my file.c`, clangd
answered for `my%20file.c`. A rename across such files writes the others and
skips these.

**Fix (S):** escape when building a URI, unescape when reading one, compare
the unescaped forms. **Test:** a unit test of the URI builder, and a
`mocklsp` mode that answers with the escaped name, as clangd does.

#### N4. Ctrl-C in the TUI also stops every MCP server and language server; afterwards their tools fail — medium, [M]

A *process group* is a set of processes the terminal treats as one job. When
you press Ctrl-C, the terminal sends SIGINT to the whole foreground group,
not to one program. jichi starts its MCP servers and language servers without
giving them a group of their own (`jc_lsp.c:117-123`,
`jc_mcp_stdio.c:95-109`), and during a turn the TUI keeps the terminal's
signal keys on (`jc_term.c:191-193`) so that Ctrl-C can stop the turn
(M918). The servers are in the same group and receive it too.

Measured through a real pseudo-terminal, writing the Ctrl-C byte as a
keyboard does: the turn stopped as designed and the session went on, but the
MCP server had received signal 2, and the next MCP call returned
`error: MCP tool call to 'srv' failed (i/o error)`. With a language server,
the edit before Ctrl-C carried its diagnostic and the edit after it carried
none: the server was gone, and nothing restarts it (`jc_lsp.c:487-492`). The
M918 test sends SIGINT to jichi's process id only, so it could not see this.

**Fix (S-M):** start these children in their own process group (the spawn
helper already has the switch, `new_pgroup`); restart a dead language server
or MCP server once; when diagnostics are unavailable, say so in the tool
result instead of saying nothing. **Test:** a pseudo-terminal driver that
sends the Ctrl-C byte mid-turn and then uses both kinds of server.

#### N5. A path fence that does not cover the language-server readers (security class) — medium, [M]

*Class:* the workspace path fence (the check that keeps tools inside the
project unless a reference root allows more) is applied by each file tool
itself. The language-server layer reads files directly
(`src/lsp/jc_lsp.c`, `open_for` and `jc_lsp_diagnostics`), so its read-only
navigation tools reach files the fence refuses to `read_file`. What reaches
the model is symbol names and positions, not file contents; the whole file
reaches the language server. Writes stay fenced. Phase 1 rated the similar
`list_files` case (D-30) medium; this is rated the same.

**Fix (S):** one fence check where the LSP layer opens a file, and filtering
of printed locations when the fence is on. **Test:** a smoke check that a
navigation tool on a path outside the workspace answers "refused by safety
fence", as `read_file` does.

#### N6. One rejected embedding batch fails the whole index, and the model learns nothing — medium, [M]

The index embeds chunks 64 at a time. If the embedding server refuses one
batch, for instance because one minified file is a single 4 KB line (chunks
are only cut between lines), the whole build stops (`jc_index.c:915-920`),
nothing is cached, and the model is told only
`error: failed to build the codebase index` (`jc_search.c:96-99`). Measured:
two `jichi index` runs both failed after re-sending the first, good batch;
the cache stayed empty; a model's `codebase_search` got the bare error.

**Fix (M):** split long lines; on a failed batch retry its items one by one,
skip and count the ones refused, save what succeeded, and name the skipped
files in the result (the same idea as the M483 coverage note, §D.2).
**Test:** the measured scenario as a smoke check.

#### N7. Documentation fetched from a URL is cached by the source's name only — medium, [M]

A `docs` source can be a URL. Its fetched page is cached for a day in a directory under
`~/.jichi.d/docs/` named after the source (`jc_docs.c:283-297`) and reused without checking
that the URL is the same (`:323-327`). Measured with two local mock servers:
project B, whose source was also called `api` but pointed elsewhere, was
served project A's page, and B's server received no page request at all.
Editing a source's URL has the same effect for a day.

**Fix (S):** key the cache by the URL. **Test:** the two-project scenario.

#### N8. A converted tool restriction can silently disappear (security class) — high, [M]

*Class:* a fence that fails open. `jichi-convert` copies one source's agent
files unchanged, on the documented premise that their format is compatible
(`jc_convert_claude.c:93-127`; CONVERT.md). The tool-list field in those files
is written in a form jichi's agent loader cannot read as a list
(`jc_agentdef.c:167-178`), and an agent profile with an empty list gets **no**
tool restriction (`jc_tool_subagent.c:267`). So an agent that the source tool
limited to reading becomes, after conversion, an agent offered every tool,
without a note. The same loader treats any YAML list it cannot parse the same
way, which is F21's mechanism in a place where the list *is* the fence. It is
rated high because the brief's scale puts "a fence that does not hold" there;
in chat mode the mutating calls still ask for approval, in `--auto` they do
not.

**Fix (S-M):** translate the agent files instead of copying them (tool names
mapped, read-only set when nothing mutating remains, an unmapped name
reported); and make the loaders **refuse** a tool-list key they cannot read
rather than treat it as absent. **Tests:** a converter unit test on such an
agent file, and a loader unit test that an unreadable list is refused.

#### N9. Continue: an embedding model can become the chat model; `contextLength` is dropped — medium, [M]

In Continue's YAML, a model with no `roles` gets Continue's default roles,
which include chat. jichi's converter treats it as not-chat and falls back to
the first model (`jc_convert_continue.c:19-34, 308-315`); it also reads
`contextLength` only outside `defaultCompletionOptions`, where Continue puts
it (`:81-85`). Measured: an embedder listed first (with a block-style roles
list) and a chat model without roles gave `selected 'Nomic Embed'`, and the
chat model lost its `contextLength: 32768`, with no note. **Fix (S):** treat
absent roles as chat-capable, read the option where Continue writes it, warn
when the chosen model has no chat role. **Test:** that config in
`tests/test_convert.c`.

#### N10. opencode: deny rules and file-referenced prompts are mapped partly and silently (security class) — low, [M]

*Class:* restrictions in the source config are lost or narrowed during
conversion without a note (`jc_convert_opencode.c:265-285, 351-367, 397-403`):
an agent's per-tool denial can vanish, a file-editing denial covers only some
of jichi's file-writing tools, and a prompt kept in a separate file arrives as
the literal reference text. **Fix (S):** map every denial to the complete set
of jichi tools it means, warn on every skipped form. **Test:** a converter
unit test per form.

#### N11. Searching by name asks only the first language server — medium, [V]

Without a path, `find_definition` and `find_references` ask only the first
configured server (`jc_lsp.c:799-809`), although the tool says "the whole
project is searched". In a project with two servers, a symbol of the second
language is answered `No matching symbol.`. Not measured (one real server was
available). **Fix (S):** ask each configured server and merge.

#### N12. A rules file tells the model a command is governed when it is not — medium, [V]

Two scaffold packs' `AGENTS.md` say a service-manager command is covered by
the privileged-command policy (`jc_scaffold.c:578-582, 1566-1569`). The policy
covers commands run through a privilege launcher such as `sudo`, `doas`,
`pkexec`, `su` or `run0` (`src/util/jc_priv.c:39-41`); the bare command is an
ordinary shell command, so it runs without the gate or an audit row where the
user already has the rights. **Fix (S):** correct the two sentences.
**Test:** a lint that every launcher the templates name is in the policy's
table.

#### Lower-severity new items

- **LSP lifecycle (low, [V]).** A server's request to apply an edit is
  acknowledged as applied before jichi applies it, even when the write is then
  refused; `exit` is sent without waiting for the `shutdown` reply; every
  server request is answered with `null`, including one whose answer must be a
  list; a server whose start-up failed keeps running until jichi exits
  (`jc_lsp.c:268-295, 519-523, 1175-1191, 1291-1295`).
- **What the model is told (low, [V]).** `format_file` advises reading
  diagnostics with two tools that do not show them
  (`jc_tool_lsp_refactor.c:356-362`); overlapping edits are skipped without a
  word (`jc_lsp_proto.c:1020-1031`).
- **MCP sizes (low, [V]).** Tool descriptions, schemas and the resource list
  inside `read_mcp_resource`'s schema have no size bound and ride every
  request; list pagination (`nextCursor`) is ignored, so a paginating server
  loses everything after its first page (`jc_tool_mcp.c:22-52`,
  `jc_mcp_proto.c:134-187`).
- **Retrieval (low/note, [V]).** `codebase_search` reads `top_k` strictly, so
  `"10"` silently becomes 5; the keyword scorer splits identifiers at `_` and
  ignores non-ASCII words; results carry no score, so "nothing relevant" looks
  like five hits; `search_docs` does not list its source names
  (`jc_tool_codebase_search.c:38-39`, `jc_lexical.c:23-44`,
  `jc_search.c:37-63`, `jc_tool_docs.c:22-31`).
- **Scaffold texts (low, [V]).** No template mentions `jichi trust`, although
  every project setting they recommend needs it; seven `AGENTS.md` point to
  model settings their `config.example.json` does not hold; the assignments
  pack says `assign` runs the verify command (only `grade` does); its skeleton
  suggests the bare test-all gate the same pack forbids; and SCAFFOLDING.md
  says `tests/test_scaffold.c` checks what it does not, which is why F18, F19
  and F21 pass.

### D. The design decisions behind these, and their lessons

**D.1 The post-edit wait is a fence, and its length is a trade.**
*Decision (M938):* after the first diagnostics arrive, keep listening for 2.0
s of real time. *Why:* servers often publish an empty set first and the real
one later; the review's clangd took 0.3-0.5 s (F1 above), and the M938
mock stages 1.4 s (its driver's header, `tests/smoke/lsp_diag_grace.sh`). *Implications:* a
clean file now costs 2.0 s per edit; a file whose answer never comes costs 15
s; a server slower than 2.0 s is still read as clean. The protocol offers a
better signal: the server can stamp each answer with the document version it
belongs to (clangd does, measured), and the client can ask for that stamp
(`versionSupport`), which jichi does not yet do. *Lesson:* a timeout chooses
between two wrong answers; when the protocol can tell you which answer is
current, use that and keep the timeout only for "nothing came".

```c
/* src/lsp/jc_lsp.c:202-212 (e5d832a4) */
/* M938: the one LSP clock. Every deadline in this module is a RELATIVE
 * (now + timeout) value, so the whole module must read now from one clock --
 * the monotonic millisecond clock, in seconds. Mixing this with the wall
 * clock (jc_now_seconds) made a monotonic deadline (~4.9e5 s) compare
 * "expired" against wall now (~1.7e9 s) and lsp_recv gave up before reading a
 * single publish. A wall deadline survives a clock step; a monotonic one
 * cannot be stepped. */
static double lsp_now_seconds(void)
{
    return jc_now_millis() / 1000.0;
}
```

Read the comment twice: the fix itself first broke the module by comparing a
deadline on one clock with "now" on another. A deadline is a number on a
particular clock. MCP's reader still uses the whole-second wall clock
(`jc_mcp_stdio.c:203`); the same lesson has one more place to go.

**D.2 Positions: ask, then verify.** jichi works out symbol positions three
ways: by searching the text (N2), by taking column 0 of a given line (N1), and
by asking the server. Only the last is reliable, because only the server knows
what is a comment and what is code. *Lesson:* when a peer owns the truth, ask
it, and check the answer against what you expected (the symbol's text at the
returned range) before you change files.

**D.3 Children inherit more than their environment.** Every program jichi
starts inherits its terminal, its process group, its signal dispositions and
(unless scrubbed) its environment. jichi already handles several of these
deliberately: the shell tool's children lead their own group, SIGPIPE is
reset to the default for every child (M461), MCP servers get a scrubbed
environment. The language servers and MCP servers were left in the terminal's
group (`include/jc_spawn.h:67-71` records that the postures were kept as they
were when the spawn code was unified, M832) and the language servers keep the
keys (F8). *Lesson:* list what a child inherits, decide each item, and test
the decision the way the outside world triggers it (a real terminal's Ctrl-C,
not a signal to one pid).

**D.4 The converter invents nothing, but it still loses things silently.**
*Decision (M709, M718):* the converter writes no provider, no model id and no
key variable that the source did not name, and says so in a note. That rule
held in every measured case. Its losses did not: flow lists, default roles,
`contextLength`, agent tool lists, deny maps and file-referenced prompts all
vanish without a note, while CONVERT.md says every lossy item is reported.
*Lesson:* a translator's contract is two-sided. "I add nothing you did not
say" is half of it; "I tell you everything I dropped" is the other half, and a
test can hold it: one fixture per loss, and an assertion on the number of
notes.

**D.5 An unreadable restriction must close, not open.** jichi's rule for
booleans that a person or a foreign program wrote (CLAUDE.md, M519/M530) is to
read them leniently because a misread `"pathFence": 1` once disabled a fence.
Lists have the same trap the other way round: the agent and skill loaders
read a tool list they cannot parse as "no list", and "no list" means "no
restriction" (N8, F21). *Lesson:* when a field exists to restrict, the
fallback for "present but unreadable" must be the restrictive one, or an
error, never the default for "absent".

### E. What this means for the context relay

The relay shipped since the chapter's §6 was drafted. In topology B (several
legs inside one process) only the conversation is reset between legs
(`src/main.c:3650-3690`); the codebase index, the language servers and the
MCP connections stay. So F7's stale index lasts the whole relay, a server
lost to Ctrl-C in leg 1 (N4) stays lost, and the relay's memory guard
(M953), which measures growth from leg 1, also counts F4's per-batch index
leak. In topology A (a fresh process per leg) MCP servers are connected again
one after another, each request allowed up to 120 s, and the index is rebuilt
from its disk cache on the first search, unless N6 left no cache. Every MCP
tool description and schema rides every request without a bound, so the
relay's starting-prompt arithmetic must include them.

### F. What this slice does well

- **M938 and M939 are model fixes:** a design note each, a mock that behaves
  like the real server (it stages the empty and the real answer on its own
  monotonic clock), a test that goes red for each guard removed, and the
  review's own measured case kept as a check.
- **Denied MCP tools are hidden and still refused** if a model names one
  anyway (`jc_mcp.c:313-327`).
- **The index says when it is incomplete** instead of letting "no match"
  mean "does not exist" (`jc_index.c:291-312`, M483), and it refuses a cache
  written by another model or byte order (`:741-749`).
- **The converter invents nothing** (§D.4), and `jichi setup --import`
  already writes the config where jichi reads it.
- **`format_file` checks the fence before asking the server**, and tells "no
  changes needed" apart from "could not parse" (`jc_tool_lsp_refactor.c:293-368`).
- **Machine-checked scaffold text is clean:** across 33 packs, every agent a
  command names exists, no command hides a built-in, and every jichi flag,
  subcommand and config key the templates mention exists.

### G. Recommendations, in order of value for effort

1. **Fix the rename position (N1).** Small, and it stops a wrong rename from
   being written and reported as the right one. Add `prepareRename` and the
   text check at the same time.
2. **Close the two fences that fail open (N8, N5).** Make the agent and skill
   loaders refuse an unreadable tool list; translate converted agent files;
   add one fence check where the LSP layer opens a file.
3. **Give MCP and LSP children their own process group, and restart a dead
   one once (N4, F12b).** Then extend the M918 test to send the Ctrl-C byte
   through a pseudo-terminal.
4. **Escape file URIs (N3)** and **locate symbols from the server's outline
   (N2).** Both are small and both remove a confident wrong answer.
5. **Use the document version for diagnostics (F1 residual):** advertise
   `versionSupport`, ignore answers for an older version, and say "pending"
   rather than nothing when the wait ends without one.
6. **Make the index build partial instead of all-or-nothing (N6)** and **key
   the docs URL cache by URL (N7).**
7. **Bound the writes (F3) and the whole-request deadline for MCP (F10)** with
   one shared peer helper for LSP, MCP stdio and ACP.
8. **Converter notes (N9, N10, F13, F15-F17):** one `jc_ir_warn` per dropped
   or narrowed item, and the CONVERT.md sentence made true by a test that
   counts them.
9. **Scaffold texts (F18-F21, N12, the small items):** the
   `test_assets_parse` extension that resolves every tool name, requires a
   readable list and rejects read-only profiles with mutating tools would go
   red on nine items today.

### H. What was not reviewed

The repo map's code beyond its two documentation sentences (Phase 1's D-28,
D-29 and F-01 cover it); the MCP HTTP transport beyond its request path
(Phase 1's network chapter); language servers other than clangd and any real
MCP server (no network was used); a valgrind re-run of F4 and a two-process
run of F5 (their code is unchanged since they were measured).

### I. Something to do

**Without a bench (the read-only twin):**

1. For N1, take the line `Widget make_widget(void) {`. Which token starts at
   column 0? Which column does `make_widget` start at? What would a server
   rename at column 0?
2. For N4, a terminal sends Ctrl-C to "the foreground process group". Draw
   jichi, its MCP server and its language server as boxes, and mark which of
   them are in that group today, and which would be after the fix.
3. For N8, write down three ways a tool list in YAML can be spelled (a block
   list, a list in brackets, a comma-separated string). Which one does jichi's
   loader read as a list? What does it do with the other two?

**With a bench (the source tree only):** find the three places where the
language-server client uses a line number given by the caller. This command
was run on `e5d832a4` from the tree's root:

```sh
/usr/bin/grep -n "line_hint > 0" src/lsp/jc_lsp.c
```

It prints lines 906, 938 and 1018. Read the two lines after each: in which
of them is the symbol's name used, and in which is it not?

### J. Read further

- [LSP.md](../LSP.md), [MCP.md](../MCP.md), [RAG.md](../RAG.md),
  [CONVERT.md](../CONVERT.md), [SCAFFOLDING.md](../SCAFFOLDING.md): the
  references. *Primary for jichi.*
- The Language Server Protocol specification 3.17: "Text Documents"
  (document versions), "PublishDiagnostics" (`versionSupport`), "Rename"
  (`prepareRename`), and "Uri" (escaping). *Primary.*
- The Model Context Protocol specification, "Lifecycle" and "Pagination".
  *Primary.*
- The POSIX definition of process groups and the terminal's foreground group
  (the General Terminal Interface chapter of the Base Definitions). *Primary.*
- RFC 3986, "Uniform Resource Identifier (URI): Generic Syntax", section 2
  (percent-encoding). *Primary.*
- [The 2026-10-17 hardening review](2026-10-17-hardening-00-overview.md):
  Phase 1, whose ids this section cites.
