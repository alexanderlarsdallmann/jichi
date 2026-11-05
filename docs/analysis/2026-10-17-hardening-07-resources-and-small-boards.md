# Hardening review 7 — Resources and small boards: where jichi's memory, CPU and disk go

*Part of the 2026-10-17 hardening and platform review of jichi, at commit `e5d832a4`; start with
[the overview](2026-10-17-hardening-00-overview.md). Every `path:line` in
this chapter refers to that commit. The chapter was drafted by reviewer F (Claude Opus 5.5) and edited by
the review's lead, who also re-checked its central claims.*

*Where the measurements come from:*
- *An x86-64 workstation: Ryzen 9 3900X, Ubuntu 24.04, gcc 13.3, libcurl 8.5.*
- *A LicheeRV Nano: one RISC-V C906 core, 229 MB of visible RAM.*
- *The only model was the repository's own mock model (`tests/tools/mockmodel`) on the loopback
  interface. No measurement had a timeout around it.*

---

## Who this is for, and what you will be able to do

**For** a self-learner or junior developer who knows a little C and nothing about jichi's insides, and
who wants to run jichi on a small single-board computer or simply understand where a C program's
resources go. You do not need to have read any jichi source.

**After reading it you will be able to:**

- explain jichi's three memory arenas and why each exists;
- say what one model call costs in memory, CPU, system calls and disk writes, with numbers;
- choose a build mode for a small board and know what it buys you;
- read a heap profile and a syscall count, and know why a heap peak is not the same thing as resident
  memory;
- act on ten concrete findings, ordered by how much they buy for how much work.

**What you need first.** It helps, but is not required, to have read the beginner's page on memory,
[Annai 7](../reading/annai-07-memory-the-jichi-way.md), and the argument for the arenas,
[Fukabori 3](../reading/fukabori-03-the-three-arena-lifetime-model.md). The numbers jichi already
publishes for small machines are in [LOW_MEMORY.md](../LOW_MEMORY.md); this chapter extends them.

---

## 1. Ten terms first

- **RSS (resident set size).** The memory of a process that is actually in RAM right now, counted in
  pages. It is what the out-of-memory killer looks at and what `/usr/bin/time -v` reports as "Maximum
  resident set size".
- **Heap.** Memory a program asks for with `malloc`. A heap profiler such as valgrind's massif counts
  bytes requested. That is not the same as RSS: a large allocation can be reserved and never touched,
  and freed memory may stay resident until the allocator gives it back.
- **Page, page fault.** The kernel hands out memory in pages, usually 4 KB; some boards, such as the
  Pi 5 kernels, use 16 KB. The first touch of a new page is a *minor page fault*: a short trip into the
  kernel to map a zeroed page.
- **mmap threshold.** glibc's `malloc` serves large requests (≥128 KB by default) with a private
  memory mapping rather than from the main heap. Freeing such a block unmaps it and returns it to the
  system at once.
- **Arena (bump allocator).** A block of memory handed out by advancing a pointer. Nothing is freed
  one item at a time; the whole arena is rewound at once. jichi's arenas are in `src/util/jc_mem.c`.
- **System call (syscall).** A request to the kernel: `read`, `write`, `poll`, `close`, `rename`. Each
  costs a little time, and on a battery or thermally limited board each *wake-up* of an idle program
  costs power.
- **`fsync`.** A syscall that waits until a file's data is on the storage device. Without it, written
  data sits in the kernel's page cache for up to about 30 s; a power cut in that window loses it.
- **`-O0`, `-O2`, `SIZE=1`.** Compiler optimisation levels.
  - jichi's default build passes no `-O` flag, which is `-O0`: unoptimised, easiest to debug.
  - `make OPT=-O2` optimises for speed.
  - `make SIZE=1` optimises for size (`-Os`), removes unused sections and strips the binary.
- **Fence and cap (jichi's vocabulary).** A *fence* bounds what a run may touch: a path, a disk floor.
  It stays on even in measurement runs. A *cap* bounds how long or how much: a deadline, a token
  budget. It stays off when measuring, because a cap that fires produces a different result
  (CLAUDE.md, "Caps versus fences").
- **Lite mode.** jichi's low-resource profile. It is chosen automatically on machines with less than
  1 GB of visible RAM (`jc_resource_tier`, `src/platform/jc_platform_posix.c`), or with `--lite`. It
  turns off the repository map, `@` references, markdown rendering and snapshots
  (`src/config/jc_config.c:1060-1090`).

---

## 2. How jichi spends memory, CPU and disk

jichi is single-threaded. Its memory comes in two kinds.

**Arenas, for data whose lifetime is known in advance.** There are three:

| Arena | Lives for | Holds |
|---|---|---|
| `app->arena` | the whole session | configuration, rules, the repository map |
| `app->scratch` | one top-level turn | the system prompt and `@`-reference expansions |
| `app->tool_scratch` | one tool call | a file's bytes while a tool works on them |

Rewinding an arena keeps its first block and frees the rest:

```c
/* src/util/jc_mem.c:156-167 */
void jc_arena_reset(struct jc_arena *a)
{
    struct jc_block *b;
    /* Free all but the last (oldest) block, then rewind that one. */
    while (a->head->next != NULL) {
        b = a->head;
        a->head = b->next;
        free(b->data);
        free(b);
    }
    a->head->used = 0;
}
```

The project learned the cost of a wrong choice twice. Per-call reads placed on the session arena grew
one user's process to gigabytes (M197, [ANECDOTES #23](../ANECDOTES.md)). Reads kept for a whole
200-call turn left a 55 MB peak (M199, [ANECDOTES #24](../ANECDOTES.md)). No leak checker reports either,
because the memory is still reachable when the program exits.

**`malloc`, for data whose size or lifetime is dynamic.** That covers the conversation history (one
`jc_message` per message, each with its own `content` string), JSON trees and growable strings.

**One model call goes through these steps:**
1. The provider (OpenAI or Anthropic dialect, `src/provider/`) builds a JSON tree from the whole
   history. That is a second copy of every message.
2. It prints the tree into one string, a third copy, and frees the tree.
3. It checks the whole string for valid UTF-8 (`src/provider/jc_provider.c:195-208`).
4. libcurl uploads the body through a read callback that frees it the moment the last byte has gone
   out (M20e, [ROADMAP](../ROADMAP.md) "M20e — Stretch"):

```c
/* src/net/jc_http.c:697-705 */
    /* Reclaim the body as soon as it is fully sent (before the response). */
    if (br->own && !br->freed && br->off >= br->len) {
        free(br->data);
        br->data = NULL;
        br->freed = 1;
    }
```

Because the body is gone, a retry has to rebuild it (`src/chat/jc_agent.c:257-262`).

5. The reply streams back as Server-Sent Events. `src/net/jc_sse.c` frames the lines and the provider
   parses each event.
6. The text is collected, then copied into the history when the message ends.

**Two settings for glibc's allocator (M218, `src/util/jc_memtrim.c:11-26`).**
- At startup, the mmap threshold is *pinned* at 128 KB. Otherwise glibc raises it after the first large
  free, and the large request bodies then stay in the main heap for good.
- At the start of every top-level turn, `malloc_trim(0)` returns freed heap pages to the system.

Both compile to nothing where the build probe finds no `malloc_trim`, for example on musl.

**Disk.**
- The session file is rewritten whole, as a temporary file followed by `rename`, once per top-level
  turn, and only if something changed.
- Telemetry is on by default since M599 (`metrics` level). It appends one JSON line per event under
  `~/.jichi.d/telemetry/` and flushes each line.
- jichi never calls `fsync`; `docs/FILE_HANDLING.md` §"The honest part" records that decision.

**The interactive front end (TUI).** At the prompt it blocks in `read(0, …)` and uses no CPU. During a
model call it is woken by libcurl's progress callback, which it uses to collect keys typed ahead and to
redraw the "working…" spinner.

---

## 3. What was measured

### 3.1 Memory over one long headless turn [M]

**The workload:**
- One headless (`-p`) run in `--auto` mode with telemetry at `metrics`.
- The mock model asks for N `read_file` calls on distinct files of a fixed size, then answers.
- The default `-O0` build, on the x86 workstation.

| Workload | Peak RSS | Final request body |
|---|---|---|
| 1 call | 12.7 MB | 17 KB |
| 50 reads × 20 KB | 15.5 MB | 1.2 MB |
| 200 reads × 8 KB | 17.5 MB | — |
| 200 reads × 20 KB | 21.1 MB | 3.3 MB |

**What the peak is made of.** valgrind's massif tool, run on the 50 × 20 KB turn, shows that the peak
heap (4.9 MB) falls inside the request build:

| At the peak | Bytes |
|---|---|
| The history's message strings | 1.16 MB |
| The JSON tree's copies of those strings | 1.16 MB |
| The print buffer's capacity, which doubles as it grows | 2.0 MB |

Only the touched part of a mapped buffer is resident. That is why RSS rose by about 2.5× the request
size, not 4×. It is the same lesson as M725's story, where a fix cut a heap peak by 88% and RSS by
4% ([the massif write-up](2026-09-22-index-embedding-allocation-massif.md)).

**After a turn.** A TUI turn that read 30 files of 100 KB ended with these `/context` readings:
- 16.2 MB resident, with a 21.0 MB peak;
- the session arena at 1 KB used;
- the tool arena holding the last file, 100 KB.

### 3.2 System calls and disk writes per turn [M]

strace counted every syscall of the 50-call headless turn.

| Per tool round | Count |
|---|---|
| telemetry `write` (one `model_call`, one `tool_call` event, 360–430 bytes each) | 2 |
| `statfs` (the telemetry disk-space sample, F-10) | 1 |
| `sendto` / `poll` / `rt_sigaction` (libcurl) | about 11 / 23 / 37 |

| Per turn | Count |
|---|---|
| session save: temp file + `rename` (1.24 MB after 50 rounds) | 1 |
| `mkdir` on every save, all of them "already exists" (F-09) | 13 |
| `fsync` / `fdatasync` | **0** |

One TCP connection carried all 51 requests (M227's connection reuse). One `git` child was spawned at
startup and one `rm` child at exit, and each closed 4,093 file descriptors before running (F-09).

### 3.3 Wake-ups in the TUI [M]

strace followed a TUI session through four phases:

| Phase | Syscalls |
|---|---|
| 15 s idle at the prompt (also 30 s in a second run) | 0: the last call is a blocking `read` |
| Waiting 6 s for a model that sends nothing | 8 per second, one wake-up per second (F-08) |
| Streaming 5,000 small text deltas | 5,017 `pselect6` (one key check per delta) and 391 `write`s, one per rendered line |
| Back at the prompt | 0 |

This confirms M181's earlier result of "0.0 s CPU idle" ([footprint comparison](2026-07-28-footprint-comparison.md)).

### 3.4 Where the CPU goes [M]

callgrind counted instructions in the 50-call turn on the `-O2` build. Building the request was
**91.7%** of all instructions. Within that:

| Part of the request build | Share of all instructions |
|---|---|
| printing the JSON (one helper call per byte) | 60.9% |
| re-checking the whole printed body for valid UTF-8 | 30.7% |
| building the tree | about 3% |

Everything else together was about 8%: running the tools 1.6%, the tool-schema print for the token
estimate 1.2%, copying results into the history 1.2%.

Streaming the reply is cheap by comparison: 5,000 deltas cost about 19,000 instructions each, about
0.1 s in total on this machine.

### 3.5 Build modes on x86 [M]

The 200 × 20 KB turn, with user CPU time from `/usr/bin/time -v`:

| Build | User CPU | Peak RSS | Binary (lead, L-02) |
|---|---|---|---|
| default (`-O0`) | 4.54 s | 20.7 MB | 2,230,056 B |
| `OPT=-O2` | 2.54 s | 21.4 MB | 1,881,040 B (−16%) |
| `SIZE=1` | 2.92 s | 21.0 MB | 1,552,072 B (−30%) |

The lead measured, from the same tree:
- every build mode compiles the product with zero warnings;
- the unit suite passes in every mode (15,398 checks, 0 failures), once the test sources are compiled
  without `-Werror`;
- the binary installed by `make install` is whatever the last plain `make` produced, which is the
  `-O0` build, so `_FORTIFY_SOURCE` is not live in it (L-01).

### 3.6 On the LicheeRV Nano [M, lead]

The lead's benchmark ran on the board itself (riscv64, one C906 core at 1 GHz, 229 MB visible, Debian
with gcc 13.2 and libcurl 8.8.0, 4 KB pages): for each build, a serial build of `jichi`, one
`--version`, and five runs of one 4-request mock turn that makes three `read_file` calls on two
generated files (260,000 and 264,000 bytes). Every turn made its 4 requests and printed the final
answer. The script is kept with the project's internal records.

| On the Nano | `-O0` (default) | `OPT=-O2` | `SIZE=1` |
|---|---|---|---|
| Serial build of `jichi` | 221 s | 731 s | 597 s |
| Binary on disk | 2,177,896 B | 1,755,216 B (−19%) | 1,494,480 B (−31%) |
| fortify-checked functions imported | 0 | 10 | 11 |
| `--version`: peak RSS, wall (one run) | 7,060 KB, 0.15 s | 6,828 KB, 0.07 s | 7,084 KB, 0.14 s |
| Turn wall time (5 runs) | 0.63–0.69 s | 0.43–0.61 s | 0.48–0.51 s |
| Turn user CPU (5 runs) | 0.40–0.44 s | 0.20–0.23 s | 0.25–0.29 s |
| Turn peak RSS (5 runs) | 12.4–12.8 MB | 12.2–12.5 MB | 12.1–12.3 MB |
| Request 4 | 613,644 B | 613,644 B | 613,646 B |
| Unit suite (`make test`, build included) | 15,398 checks, 0 failures, 203 s | 15,398 checks, 0 failures, 583 s (fortify live) | not run |

For comparison, the same turn at `-O0` on the x86 workstation (reviewer F): wall 0.120–0.122 s, user
CPU 0.023–0.027 s, peak RSS 13.5–13.9 MB, request 4 613,658 B (the sizes differ only by the
workspace paths). The `--version` row is a single run each, so its time is noise-level.

**What this shows.**
- **The board needs about 15–19× the workstation's CPU for the same work.** Reviewer F's report had
  guessed 8–10× for a Cortex-A53; the C906 is the weaker core.
- **On the board, `-O2` halves jichi's own CPU per turn** (0.20–0.23 s against 0.40–0.44 s), and
  `SIZE=1` cuts it by about a third while also making the smallest binary. Memory is the same in all
  three builds: the turn's peak is the request, not the code.
- **The price is build time:** an optimised build takes 2.7–3.3× as long on the board (597–731 s
  against 221 s). For a board, build once and install, or cross-build.
- **Scaling the 200-round x86 turn by the measured ratio,** an `-O0` build on the Nano would spend
  about 75 s of CPU re-serialising history, about 0.4 s per model call. That is an extrapolation, not
  a measurement.

---

## 4. Design decisions, their reasons and their lessons

**Decision 1 — three arenas chosen by lifetime.**
- *Why:* C89 has no destructors and no garbage collector. If every piece of data has a known end of
  life, one rewind cleans everything up. ([ROADMAP](../ROADMAP.md) M20a for the per-turn arena, M199 for
  the per-call one; [tool-arena analysis](2026-07-29-tool-arena.md).)
- *Implications:*
  - Good: it works. The measured turn above left the session arena at 1 KB.
  - It is easy to get wrong, and a leak checker cannot see it. The project now enforces the rule with a
    lint (`tests/smoke/arena_lint.sh`) and prints every arena's size in `/context`.
  - A short-lived arena bounds how *long* memory is held, not how *much* it holds at once. F-01 is
    exactly that gap.
- *Lesson:* pick the shortest lifetime that outlives the data, then ask a second question: what is the
  peak inside that lifetime?

**Decision 2 — rebuild the request on every attempt, and free the body as soon as it is sent (M20e).**
- *Why:* the large body never coexists with the reply that streams back. On a small board that is the
  difference between holding one copy of the history and holding two.
- *Implications:*
  - Good: memory stays low.
  - The rebuild is the CPU hot spot: §3.4's 92%. A retry pays it again.
- *Lesson:* when you trade CPU for memory, measure the CPU you traded. Here the trade is right; the
  per-byte work inside the rebuild is what is wrong (F-06).

**Decision 3 — pin glibc's mmap threshold and trim at turn boundaries (M218,
[LOW_MEMORY.md "The memory model"](../LOW_MEMORY.md)).**
- *Why:* glibc raises its mmap threshold after the first large free. Once it has, the big request
  bodies come from the main heap, which glibc rarely shrinks. RSS then rises while every leak checker
  reports nothing.
- *Implications:*
  - Good: RSS stays level on glibc.
  - Every body of 128 KB or more is now a fresh mapping that must be faulted in: 117,826 minor page
    faults in the 200 × 20 KB turn, about 0.1 s of kernel time [M + estimate].
  - It does nothing on musl, whose allocator has no such ratchet [GK].
  - I also tried trimming again at the *end* of each turn. Idle RSS was 16,192 KB without it and
    16,380 KB with it [M]: no difference, so the existing design stands.
- *Lesson:* the allocator is part of your program's behaviour. Measure RSS, not only the heap.

**Decision 4 — atomic file replacement without `fsync` ([FILE_HANDLING.md](../FILE_HANDLING.md),
M146).**
- *Why:*
  - Writing a temporary file and renaming it over the old one means a concurrent reader always sees
    either the old file or the new one, never half of each.
  - `fsync` after every write was judged too costly for files jichi can recreate.
- *Implications:*
  - Measured: zero `fsync` per turn.
  - ext4 usually flushes a file that replaces another by rename [GK].
  - Other filesystems, or an SD card after a power cut, can leave an empty session file. The portability
    analysis already records that risk.
- *Lesson:* atomicity (no torn file) and durability (the file survives a power cut) are two different
  promises. Write down which one you give.

**Decision 5 — save the session once per top-level turn, whole, and skip unchanged saves (M218, M232).**
- *Why:* it is simple, the result can always be resumed, and M232's streamed serializer never builds a
  tree of the whole history.
- *Implications:*
  - Good: low memory.
  - A 200-round headless turn is one turn, so it is saved once, at its end. The 50-round run wrote the
    session exactly once [M]. A crash or the OOM killer mid-turn loses that turn (2026-10-08 review,
    R05-F6, still open).
  - Every per-turn save ignores its own error result (F-02).
- *Lesson:* a persistence point is only as good as the error path around it.

**Decision 6 — telemetry on by default, one flush per event (M599: "a learner forgets otherwise").**
- *Why:* a learner, or `learn analyze`, needs history that nobody had to remember to switch on.
  Flushing every line makes events visible to a reader at once.
- *Implications:*
  - Two small writes per tool round [M].
  - The log is never rotated (DEFERRED def-198), and past 64 MiB jichi's own readers cannot load it
    (F-04).
- *Lesson:* a sink that is on by default needs a retention plan from the day it is switched on.

**Decision 7 — idle means blocked in `read`; liveness comes from libcurl's callback (M66, M254).**
- *Why:* no timer and no thread means no wake-ups while you think.
- *Implications:*
  - Good: zero syscalls at the prompt [M].
  - The spinner's "~11 fps" limit (`src/tui/jc_tui.c:588-628`) only applies while bytes arrive. During a
    silent wait libcurl calls back once per second, so the spinner moves once per second. That is good
    for a battery and slightly misleading in the comment (F-08).
- *Lesson:* drive liveness from events you already have, and know how often the event source fires.

**Decision 8 — the default build is unoptimised (Makefile:755-768).**
- *Why:* debuggability, and `make info` says so plainly.
- *Implications:*
  - The default build takes 1.8× the CPU of `-O2` on the request path [M].
  - `_FORTIFY_SOURCE` is inactive, a hardening point the lead covers (L-01).
  - `make install` installs that build.
  - [LOW_MEMORY.md](../LOW_MEMORY.md) already recommends `SIZE=1` for size. These measurements add that
    it also cuts CPU: 2.92 s against 4.54 s.
- *Lesson:* a default chosen for developers is not automatically right for users. Say which audience
  each build serves.

---

## 5. Findings

The 2026-10-08 severity scale:
- **high:** data loss, a crash, a broken security boundary, or a fence that does not hold.
- **medium:** a wrong result or a misleading report in a plausible case.
- **low:** an edge case or a maintenance cost.
- **note:** worth knowing, not a defect.

Evidence marks: [V] verified in the tree, [M] measured, [R] read but not traced to the end, [GK]
general knowledge. None of these findings is a security defect, so all of them are described in full.

### F-01 — The repository map can need hundreds of megabytes at startup

- **Severity:** medium.
- **Marks:** [M] [V].
- **Status:** new. M140 moved the scanned texts off the session arena, which fixed how long they are
  held but not how much memory they need at once.
- **What happens:** to build the repository map (`src/index/jc_repomap.c`), jichi works like this:
  - It walks the workspace and sorts the candidate source files.
  - It reads them one by one into **one** arena (`:699`, `:727`) and scans each for top-level symbols.
  - It frees that arena only after the listing is finished (`:753`).
  - The listing stops when it passes 12 KB (`:742-747`).

  Files with many symbols fill 12 KB after a few files. On jichi's own tree the startup heap peak is
  about 0.8 MB, and RSS with and without the map is 12.9 MB against 12.7 MB [M].

  Files with *few* symbols produce one short line each, so many of them are read and **all are kept**.
  Typical cases are minified JavaScript, generated code, or test fixtures with one function.
  - Measured with 300 files of 400 KB, each holding one function: 138 MB peak RSS with the map on,
    12 MB with it off, for a 3.8 KB map. Startup took 0.46 s instead of 0.09 s [M].
  - Worst case by arithmetic: about 1,100 lines fit in 12 KB, and each file may be up to 512 KB
    (`:20`), so about 560 MB.
  - Lite mode turns the map off, which protects boards under 1 GB. Boards of 1–8 GB keep the map: the
    Pi 4/400/5, VisionFive 2, BPI-F3.
  - The same function serves `@folder:` references during a session (`:798-804`).

  The walk (`:647-675`) also counts only matching source files toward its 4,000-file limit. It stats
  every other entry it meets, so starting jichi in a large home directory on an SD card is slow [R].
- **Recommendation:** read each file into a second arena and rewind it right after the scan. The peak
  becomes one file (at most 512 KB) and the output is unchanged. Optionally, also bound the walk by the
  number of entries visited.
- **Red-first test:**
  - A unit test that builds a map over 40 symbol-sparse 64 KB files.
  - It asserts that the scan's high-water mark is at most one file plus the listing.
  - It needs a small test counter, like the one `cJSON_jc_string_bytes` provides for the JSON parser.
- **Effort:** S.

### F-02 — A failed session save is silent

- **Severity:** medium.
- **Mark:** [V].
- **Status:** new.
- **What happens:** `jc_session_save` returns a real error.
  - Out of memory while serialising gives `src/session/jc_session.c:466-468`.
  - A failed write or rename gives `src/platform/jc_platform_posix.c:388-399`, which covers a full
    disk, a read-only home directory or a permission problem.

  But every per-turn caller ignores it:
  - the TUI after each turn and at exit (`src/tui/jc_tui.c:5381`, `:5390`);
  - the headless path (`src/main.c:3548`, `:3617`);
  - the editor protocol (`src/acp/jc_acp.c:695`);
  - the TUI's undo, rewind and resume notes (`jc_tui.c:2785`, `:3742`, `:3803`, `:4837`, `:4910`,
    `:4934`, `:4952`).

  Only the command-line `rewind` checks (`src/main.c:10022`). Because the write is atomic, the old file
  survives, which is good. But the user is never told that the turn was not saved, and expects
  `--resume` to bring it back.

  The disk fence does not cover this. It stops a run at 10 MB free on the *workspace* filesystem
  (`src/chat/jc_agent.c:2242-2244`), while the session lives under `~/.jichi.d/` and needs its own size
  free there.
- **Recommendation:** check the result at the per-turn call sites. Print one line, once per session,
  naming the file and the reason. Add a telemetry event.
- **Red-first test:**
  - A smoke driver for a `FAULT=1` build that arms `JC_FAULT_WRITE`, which `jc_write_file_atomic`
    already honours (`jc_platform_posix.c:370`).
  - It runs one headless turn with sessions on and expects a message on stderr. Today there is none.
- **Effort:** S.

### F-03 — Out of memory empties a tool result silently; one request builder dereferences NULL

- **Severity:** low.
- **Mark:** [V].
- **Status:** new. The 2026-10-08 review's R05-F15c covered only session load and fork.
- **What happens:**
  - `jc_history_add` stores `dup_content(content)` without checking it (`src/chat/jc_message.c:97`) and
    reports success.
  - `jc_msg_add_tool_call` keeps unchecked copies of the id, name and arguments (`:145-148`).
  - The tool loop ignores the result of adding a tool result (`src/chat/jc_agent.c:4786`).
  - The request builders then send an empty string or `{}` in place of the lost text.
  - In the OpenAI builder, two objects are used without a NULL check: the system message and the
    message array (`src/provider/jc_provider_openai.c:184-195`).

  So a failed allocation of a large tool result shows the model an empty result, as if the tool printed
  nothing, and a failed small allocation can crash.

  On Linux with default settings `malloc` almost never fails; the kernel kills the process instead.
  `malloc` does fail under `ulimit -v`, strict overcommit, when a 32-bit address space runs out, and on
  the systems without overcommit that jichi supports: FreeDOS, FreeMiNT and Haiku [GK].
- **Recommendation:**
  - Let `jc_history_add` return NULL when the copy fails.
  - Check the result at the tool boundary and end the turn with an out-of-memory error.
  - Guard the two OpenAI dereferences.
- **Red-first test:** add the existing allocation-fault hook (today only in the arena,
  `src/util/jc_mem.c:36`) to `jc_strdup`, and assert that `jc_history_add` reports the failure.
- **Effort:** M.

### F-04 — Telemetry grows without limit, but jichi's own readers stop at 64 MiB

- **Severity:** low.
- **Mark:** [V].
- **Status:** new detail on DEFERRED def-198.
- **What happens:**
  - Telemetry is on by default (`src/config/jc_config.c:1139-1145`) and is never rotated.
  - `/context tools` loads the whole log with `jc_read_file` (`src/chat/jc_app.c:517-522`). That function
    refuses files over 64 MiB (`JC_READ_FILE_MAX`, `include/jc_platform.h:128`).
  - The caller treats the refusal as "no telemetry here" (`src/tui/jc_tui.c:5029-5032`).
  - `jichi telemetry` says only "could not read" (`src/main.c:5195-5196`).
  - def-198 plans to revisit retention when a log passes "~100 MB", which is above the point where the
    readers have already stopped working.
  - Below 64 MiB, each `/context tools` loads the entire log into memory.
- **Recommendation:**
  - Read the log line by line; `jc_telemetry_feed` already processes one line at a time.
  - At least report "telemetry log too large to read (N MB)".
  - Lower def-198's trigger below 64 MiB.
- **Red-first test:** a sparse 65 MiB log with one valid last line; `/context tools` must not report
  that no telemetry exists.
- **Effort:** S for the message, M for the streaming read.

### F-05 — Building a JSON array takes time proportional to the square of its length

- **Severity:** low.
- **Marks:** [M] [V].
- **Status:** new.
- **What happens:** `cJSON_AddItemToArray` walks the whole list to find its end on every append
  (`src/json/cJSON.c:688-705`). The parser keeps a tail pointer (`:370-416`); the builder does not.

  The large array jichi builds is the index manifest, one element per indexed chunk
  (`src/index/jc_index.c:497-505`). A small program linked against `cJSON.c`, compiled at `-O2` on x86:

  | Elements | Time |
  |---|---|
  | 1,000 | 0.001 s |
  | 16,557 (the size of jichi's own index) | 0.28 s |
  | 50,000 | 2.6 s |

  Every manifest write pays this, and a board pays it several times over.
- **Recommendation:** keep the tail in the first child's `prev` pointer, which is the convention of the
  library jichi's cJSON imitates. Keep it correct in delete and replace.
- **Red-first test:** count the steps through a test hook, and assert that 10,000 appends take fewer
  than 20,000 steps. Today they take about 50 million.
- **Effort:** S–M.

### F-06 — Re-serialising the request is almost all of jichi's CPU, and most of that work is avoidable

- **Severity:** low (performance; there is no wrong result).
- **Marks:** [M] [V].
- **Status:** new as a measurement. Decision 2 records the design.
- **What happens:** §3.4. Two causes:
  - The JSON printer calls a helper, which checks the buffer's capacity, once for every byte it copies
    (`src/json/cJSON.c:835-913`).
  - The whole printed body is then checked for valid UTF-8 one sequence at a time
    (`src/provider/jc_provider.c:199`; `src/util/jc_utf8.c:115-150`, `:210-235`), although every message
    was already checked when it entered the history (`src/chat/jc_message.c:32-42`).
- **Measured cost:** 4.54 s of CPU for a 200-round turn on `-O0`, and 2.54 s on `-O2` (x86). By §3.6's
  ratio, roughly 75 s on the Nano at `-O0`; that last figure is an extrapolation.
- **Recommendation:**
  1. In the printer, copy runs of bytes that need no escaping with one capacity check and one `memcpy`.
  2. In the UTF-8 check, skip ASCII bytes in a tight loop.
  3. Later, and larger: write the `messages` array straight into the body without building a tree, as
     M232 already does for session files. This also removes one copy of the history from the memory
     peak (§3.1).
- **Prototype (reviewer F, unvetted).** Steps 1 and 2 were tried as an 18-line change in a private copy.
  It has **not** been through the project's gate: not the C89 `-pedantic -Werror` build matrix, and not
  a parity test with non-ASCII and control bytes. Its results:
  - 51 of 51 request bodies were byte-identical to the unchanged `-O2` build, once the workspace path
    and the per-session cache key were normalised;
  - user CPU fell from 2.54 s to 0.96 s (−62%), and wall time from 3.35 s to 1.75 s [M].
- **Red-first test:** none is needed for an optimisation that changes no byte. The guard is a parity
  test that prints a corpus covering every escape class, control bytes and non-ASCII through the old and
  new paths and compares them.
- **Effort:** S for the two fast paths, L for the streamed writer.

### F-07 — Two TUI commands still allocate on the session arena every time they run

- **Severity:** note.
- **Mark:** [V].
- **Status:** new; the same class as M197.
- **What happens:** `/assignment` with a file argument reads, parses and renders the assignment onto the session arena
  on every use (`src/tui/jc_tui.c:4036-4056`). `/language` copies its argument there each time
  (`:4740`). The amounts are small and user-driven.
- **Recommendation:** use a local arena and copy out only what is kept, as `/assignments` already does
  (`:3977-3990`).
- **Effort:** S.

### F-08 — During a silent model wait the spinner moves once per second

- **Severity:** note.
- **Mark:** [M].
- **Status:** new.
- **What happens:** see Decision 7 and §3.3. Nothing breaks, and the cadence is the right default for a
  battery-powered board. Only the comment's "~11 fps" describes a case that occurs while bytes are
  arriving.
- **Recommendation:** correct the comment; keep the behaviour.
- **Effort:** S.

### F-09 — Small, repeated system-call costs at startup, exit and every save

- **Severity:** note.
- **Marks:** [M] [V].
- **Status:** new as measurements. The descriptor loop is a recorded decision (M472).
- **What happens:**
  - Startup runs `git rev-parse --is-inside-work-tree` even with snapshots off. I identified the command
    with a logging stand-in for `git` on `PATH`.
  - Exit runs `rm -rf` on a per-process directory under `~/.jichi.d/tool-output/` even when it does not
    exist (`src/util/jc_toolout.c:180-187`).
  - Each child closes descriptors 3 to 4095 one by one: 4,101 `close` calls per spawn
    (`src/util/jc_proc.c:465-482`).
  - Each session save walks `mkdir -p` (13 `mkdir` calls) even after `stat` showed the directory exists
    (`src/platform/jc_platform_posix.c:481-497`).
  - Each costs milliseconds on x86. On a board a `fork` + `exec` costs tens of milliseconds [estimate].
- **Recommendation:**
  - Use `close_range` or `closefrom` where the platform has it (§6), keeping the loop as a fallback.
  - Skip the exit `rm` when the directory is absent.
  - Return early from `jc_mkdir_p_private` when the directory exists.
- **Effort:** S.

### F-10 — One `statfs` call per tool call while telemetry is on

- **Severity:** note.
- **Mark:** [M].
- **Status:** new as a measurement; M805 designed the sample.
- **What happens:** each `tool_call` event samples free disk space (`src/chat/jc_agent.c:4303-4314`): 50
  samples in the 50-call turn, 51 `statfs` calls in all with the fence's one. That is about a microsecond on a local disk, and a network round trip on
  an NFS or SMB workspace [GK].
- **Recommendation:** reuse the disk fence's reading, which is already limited to once every 10 s
  (`src/util/jc_diskguard.c:36-45`).
- **Effort:** S.

---

## 6. Platform notes

| Platform | What it offers | What jichi does | Recommendation |
|---|---|---|---|
| Linux, glibc | `mallopt`, `malloc_trim` (also returns free pages inside the heap [GK]) | M218 settings; RSS stays level [M] | Nothing beyond F-06 |
| musl (static builds, the Nano's minimal build) | An allocator without glibc's ratchet [GK]; no `malloc_trim` | The settings compile to no-ops (`jc_memtrim.c:28-38`) [V] | Run the long-session soak (M180) once on musl; no long musl run is recorded |
| 16 KB pages (Pi 5, Pi 500+) | Larger pages: RSS counts in 16 KB units; a mapped 128 KB body rounds up by up to 12.5%; a quarter of the page faults [GK] | Nothing page-size specific: arena blocks are 8 KB heap chunks (`jc_mem.c:11`) | No change; re-run §3.1 on the Pi 500+ for a measured comparison with the Pi 400 |
| Linux ≥ 5.9 `close_range`; FreeBSD 13 `close_range`; OpenBSD, NetBSD, illumos `closefrom` [GK] | Close all descriptors above N in one call | A loop of 4,093 `close` calls per spawn [M] | Probe for them as the Makefile probes `malloc_trim`; keep the loop as fallback |
| libcurl without `CURLOPT_NOSIGNAL` | — | About 37 `rt_sigaction` calls per request [M]; a libcurl with the synchronous resolver uses `SIGALRM` for DNS timeouts [GK] | Consider `NOSIGNAL` with SIGPIPE ignored process-wide; jichi already sends with `MSG_NOSIGNAL` [M] |
| SD / eMMC storage | Power cuts are common on boards | One session rewrite per turn, no `fsync` [M] | One `fdatasync` on the session's temporary file before `rename`, at turn end only (already proposed in the portability analysis); telemetry stays unsynced |
| Boards under 1 GB | — | Lite mode turns off the repository map, references, markdown and snapshots (`jc_config.c:1060-1090`) [V] | Keep; it is what keeps F-01 off the Pi Zero 2 W and the Nano |
| FreeDOS, FreeMiNT, Haiku | No memory overcommit [GK] | — | F-03's paths are reachable here first |
| Any board | — | Default build is `-O0` | Build with `SIZE=1`: −30% binary and −36% CPU on x86 [M] |

---

## 7. Recommendations, ordered by value for effort

1. **Fix the repository map's peak (F-01).** Rewind a per-file arena after each scan. One small change
   removes the only measured path from a normal startup to over 100 MB. *S.*
2. **Report failed session saves (F-02).** Check the result at the per-turn call sites and say so once.
   Add the `FAULT=1` smoke driver that proves it. *S.*
3. **Build boards with `OPT=-O2` or `SIZE=1`, and say so where users look.** Measured on the Nano,
   `-O2` halves jichi's own CPU per turn and `SIZE=1` cuts it by a third with the smallest binary
   (§3.6); on x86 the same order holds (L-02, §3.5). State in [LOW_MEMORY.md](../LOW_MEMORY.md) that
   `SIZE=1` is a CPU choice as well as a size choice, that `make install` installs the last build made,
   and that an optimised build takes about three times as long to compile on a board. *S.*
4. **Add the two fast paths to the printer and the UTF-8 check (F-06).** Write the parity test first,
   then put the prototype through the C89 gate. Expect most of the 62% CPU cut measured on x86. *S.*
5. **Make cJSON's array append constant-time (F-05).** It removes quadratic time from every index
   manifest write. *S–M.*
6. **Fix telemetry reading and retention (F-04).** Read line by line, or at least report the size
   limit. Bring def-198's trigger below 64 MiB. *S–M.*
7. **Save the session at tool-round boundaries in long turns (R05-F6, still open).** Throttle it with
   the existing "changed since last save" check. Consider one `fdatasync` per save on board
   deployments. *M.*
8. **Make out-of-memory on the history path an error, not an empty string (F-03).** *M.*
9. **Trim small syscall costs (F-09, F-10).** Skip the exit `rm` and the redundant `mkdir` walk, use
   `close_range`/`closefrom`, and reuse the fence's disk reading. *S each.*
10. **Stream the request's message array into the body (F-06, step 3).** It removes a full copy of the
    history from every model call's peak, and more CPU. *L.*

---

## 8. Earlier findings in this area, checked at `e5d832a4`

| Earlier finding | Status now | Evidence |
|---|---|---|
| M218: mmap threshold pin, trim at turn start | fixed [V] | `src/util/jc_memtrim.c:11-26`; `src/chat/jc_agent.c:5656`, `:5856` |
| M218: provider stream buffers keep their largest size | fixed [V] | shrink on reset, `src/provider/jc_provider.c:75`, `:90` |
| M218: session re-serialised at every boundary | fixed [V] | "changed" check, `src/session/jc_session.c:307-311`, `:455-457` |
| M232: whole-history tree on session save | fixed [V] | streamed serializer, `jc_session.c:313-445` |
| M197: session listing on the session arena | fixed [V] | local arenas, `jc_session.c:948-984`, `:1009-1045`, `:630-661` |
| M199: a turn's reads accumulate | fixed [V] [M] | reset before each tool call, `jc_agent.c:4145-4147`; tool arena 100 KB after 30 reads |
| M140: repository-map texts on the session arena | changed shape [V] [M] | lifetime fixed; peak is F-01 |
| M227: one TLS connection per process | fixed [V] [M] | `src/net/jc_http.c:807-846`; one `connect` for 51 requests |
| M725: JSON string buffers sized to the rest of the input | fixed [V] | `src/json/cJSON.c:164-200` |
| M362: fast line echo only in accessible mode | changed [V] | M558 made it unconditional, `src/tui/jc_term.c:1056-1059` |
| M181: idle TUI uses no CPU | holds [M] | 0 syscalls in 15 s and 30 s |
| M143: remembered notes bounded | fixed [V] | `src/chat/jc_memory.c:164-193` |
| `fsync` policy: never | unchanged by decision [V] [M] | none in `src/`; 0 per turn |
| R05-F6: session saved only at the end of a turn | open [M] | one save for a 50-round turn |
| R05-F15b: redaction failure writes the unredacted session | open [V] | `jc_session.c:475-481` |
| R05-F15c: NULL dereference on load/fork after OOM | open [V] | `jc_session.c:612`; fork `:127-138` |
| R05-F15a: glossary keeps whole files on the session arena | open [V] | `src/chat/jc_glossary.c:21` |
| R02-F14: a resume note's save ignores `--no-session` | open [V] | `src/main.c:3280-3286` |
| R06-8: no overflow guard in arena/vector growth | open [V] | `src/util/jc_mem.c:80-86`; `src/util/jc_vec.c:160-164` |
| R06-11: telemetry writes unchecked (silent when the disk is full) | open [V] | `src/util/jc_eventlog.c:245-247` |
| R06-12: disk fence measures the workspace, not `~/.jichi.d` | open [V] | `jc_agent.c:2242-2244` |
| R07-F15a: the SSE parser's overflow flag is never read | open [V] | set at `src/net/jc_sse.c:72`, `:114`, read nowhere |
| R07-F14: streamed tool calls past 32 dropped silently | open [V] | `jc_provider.c:94-100`; `jc_provider_openai.c:268-271` |
| R01-F17b: trims recompute the whole-history estimate | open [V], not exercised | `src/chat/jc_compact.c:1050-1053` |
| R09-F12a: walks follow symlinked directories | open [V] | `jc_is_dir` uses `stat`, `jc_platform_posix.c:643-647` |
| def-198: telemetry retention | open [V]; F-04 sharpens it | no rotation in `src/` |
| def-87: what an index build's ~130 MB is | not reviewed | — |
| Long-session RSS steps (332–608 MB) | not reproduced | `@folder:` (F-01) is one more candidate; no evidence it caused them |

---

## 9. What is done well

- **Lifetimes are designed, linted and visible.**
  - The three-arena rule is written down (CLAUDE.md) and enforced (`arena_lint`).
  - `/context` prints each arena's use and the process's RSS (`src/chat/jc_context.c:217-240`). After a
    measured turn I could read the arenas' state without a profiler.
- **The request body never coexists with the reply.** The upload frees it at its last byte, and every
  early-error exit honours the transfer of ownership (`src/net/jc_http.c:1124-1140`).
- **Session saves are streamed and skipped when nothing changed** (M232, M218). Session listings read
  metadata without building a JSON tree (M202).
- **The idle front end is truly idle:** a blocking read, no timers, zero syscalls.
- **Memory fixes come with their measurements.** M218's mechanism, M725's account of why heap and RSS
  disagreed, and M197/M199's numbers each took minutes to check against the code.
- **The disk fence is pure logic plus a thin wrapper.** "Unknown never stops" is explicit
  (`src/util/jc_diskguard.c:23-45`), and its 10 s limit held in a fast turn: one fence `statfs` in 50
  calls [M].
- **The JSON primitives fail safe on allocation failure.**
  - The mutators accept NULL (`src/json/cJSON.c:688-768`).
  - The string builder guards against overflow (M472, `src/util/jc_str.c:50-87`).
  - The printer returns NULL rather than a truncated document (`cJSON.c:997-1017`).
  - The session serializer refuses to return a partial document (`jc_session.c:438-443`).
- **Lite mode turns off the right things** for the smallest boards, including the one feature F-01 is
  about.

---

## 10. Not reviewed, and open questions

**Not reviewed:**
- the index build's memory (def-87);
- compaction's summarisation path (`src/chat/jc_compact.c:417-750`);
- most of `src/tui/jc_tui.c` outside input, render, the turn hold and the session commands;
- most of `src/main.c` outside the startup and save paths;
- the MCP, LSP and editor-protocol buffers, which other chapters cover.

**Open questions:**
- **musl and 16 KB pages:** the effects described in §6 are general knowledge until someone runs §3.1 on
  the Nano's musl build and on the Pi 500+.
- **The repository map's walk:** its cost in a large home directory on an SD card was not measured.
- **What caused four real sessions to step up to 332–608 MB RSS** after 16–20 turns
  ([long-session RSS](2026-09-22-long-session-rss-measured.md)) is still unknown.
- **jichi ends a model call when the HTTP transfer ends, not when the provider sends its final event.**
  - A mock reply shape with neither chunked framing nor a closed connection kept jichi waiting after the
    final event, until the dead-stream fence's 900 s.
  - That is the mock breaking HTTP, not a jichi defect.
  - Whether a misbehaving proxy can cause the same wait was not investigated.

---

## Read further

- [LOW_MEMORY.md](../LOW_MEMORY.md): RAM tiers, board tables, build-time footprint reduction.
- [Annai 7 — memory the jichi way](../reading/annai-07-memory-the-jichi-way.md) and
  [Fukabori 3 — the three-arena lifetime model](../reading/fukabori-03-the-three-arena-lifetime-model.md).
- [Fukabori 8 — streaming and the no-buffering invariants](../reading/fukabori-08-streaming-and-the-no-buffering-invariants.md).
- [FILE_HANDLING.md](../FILE_HANDLING.md): atomic writes, and why there is no `fsync`.
- [The tool-arena analysis (M199)](2026-07-29-tool-arena.md),
  [the telemetry-memory analysis (M218)](2026-08-01-telemetry-memory.md),
  [the memory soak (M180)](2026-07-28-memory-soak.md),
  [the index massif analysis (M700/M725)](2026-09-22-index-embedding-allocation-massif.md),
  [long-session RSS, measured](2026-09-22-long-session-rss-measured.md).
- [The 2026-10-08 review, chapter 2 — when the window fills](2026-10-08-review-02-when-the-window-fills.md).
- [ANECDOTES.md](../ANECDOTES.md) #23 and #24: the arena bugs, told as stories.
- [TELEMETRY.md](../TELEMETRY.md) and [DEFERRED.md](../DEFERRED.md) (def-198, def-87).
