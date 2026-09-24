# Where the hundreds of megabytes go: one cJSON node per float

*2026-09-22, massif. Follow-up to
[the long-session RSS measurement](2026-09-22-long-session-rss-measured.md), which
found four sessions stepping from a ~16 MB floor to a 130–250 MB plateau and
stopped at "telemetry names no allocation site". This names it. Nothing in `src/`
or `tests/` changes on this page.*

## What could not be done, first

**The four stepping sessions are gone.** `jichi ls --all` no longer lists
`0bf75213`, `050a43ba`, `e7df3091` or `de0ba7a6` — pruned from the session store.
A literal replay is impossible, and nothing below is one.

What *is* replayable is the **suspected path**. All four ran in
`~/development/adventure/chrtext` — **5.3 GB, 12,118 files** — and two of
the four used `codebase_search`, which no flat session used. `codebase_search`
builds an index, and `jichi index` walks that same path **with no model in the
loop**, deterministically. That is what was measured.

## The native reproduction

`jichi index --reindex` over the jichi repo, embeddings from a local LM Studio
(`text-embedding-nomic-embed-text-v1.5`, loaded alone on the card):

```
Indexed 3045 file(s), 16557 chunk(s) [16557 embedded, 0 reused]
Elapsed 3:51.85      Maximum resident set size: 170,048 kB  (166 MB)
```

RSS climbed **monotonically** throughout — sampled 102 → 105 → 108 → 110 → 113 →
119 → 121 MB — from a process that starts at ~16 MB. So a single `index` run,
with no long session and no model turns, reaches the same order of magnitude as
the steps seen in the session corpus.

## The site, from massif

Massif on a controlled **200-file, 5.4 MB corpus** (small enough to finish under
valgrind; 199 files, 1,966 chunks). Peak heap **204 MB**, and 94% of it is in one
function:

```
99.76% (207,665,541B) (heap allocation functions)
->94.20% (196,102,864B) parse_string
  ->68.87% (143,363,684B) ... cJSON_Parse
  |   <- jc_json_parse <- jc_embed_parse <- jc_embed_texts
  |   <- jc_index_build <- run_index <- run_index_subcommand <- main
  ->23.19% ( 48,282,332B) ... cJSON_Parse   (same call chain)
```

**92% of peak heap is `cJSON_Parse` of the embeddings response**, reached through
`jc_embed_parse ← jc_embed_texts ← jc_index_build`. Not the corpus, not the
chunks, not the vectors as stored — the **JSON reply being turned into a tree**.

## The mechanism, confirmed by arithmetic

cJSON allocates **one heap node per JSON value**, and an embeddings response is
one number per dimension per chunk:

| | |
|---|---:|
| chunks in the massif corpus | 1,966 |
| dimensions per vector | 768 |
| **JSON numbers in the response** | **1,509,888** |
| at ~128 B per cJSON node | **184 MB** |
| **massif observed in the embed path** | **183 MB** |

Within 1%. The 768-float vector arrives as ~9 KB of text and becomes ~98 KB of
cJSON nodes — **a tenfold expansion, structural, before a single float is
stored.**

## It is not a leak

The heap curve sawtooths across the run:

```
204MB  12MB  176MB  180MB  203MB  203MB  195MB … 12MB  192MB … 22MB  202MB  28MB  203MB …
```

Each batch parses its ~200 MB tree and frees it back to ~12 MB before the next.
Nothing is retained. **The peak is real and transient — exactly the shape the
session corpus showed**, where RSS spiked to 608 MB and fell back to a plateau
rather than to the original floor.

## Why the documented mitigation does not cover this

`LOW_MEMORY.md` describes jichi pinning `M_MMAP_THRESHOLD` at **128 KB** at
startup so *"large transient bodies always mmap/munmap"*, plus `malloc_trim` at
turn boundaries.

That targets **one big allocation**. This is the opposite shape: **1.5 million
allocations of about 128 bytes each.** The mmap threshold never applies to them,
and `malloc_trim` can only return free pages the allocator happens to hold at the
top of the heap — which is why RSS falls back *part way* and not to the floor.
The mitigation is not wrong; its allocation shape and this one are different, and
the soak that validated it (25 turns, **~0.4 MB history**) never allocated a
tree like this.

## What is established, and what is not

**Established.** The dominant allocation in an index build is `cJSON_Parse` of
the embeddings response (92% of peak heap); one node per float explains the
magnitude to within 1%; it is transient per batch, not retained; and a plain
`jichi index` on a 3,045-file repo peaks at 166 MB RSS with monotone growth.

**Not established.** That this is *the* cause of the four session steps. The same
workspace, the same tool in two of the four, and the same shape — but the
sessions are pruned, `de0ba7a6` stepped with **neither** `codebase_search` nor
`spawn_subagent`, and no run here reproduced a *session*. This is a named
mechanism that is sufficient to produce the observed magnitudes, not a proven
attribution.

**Also not established.** That fragmentation is why the floor stays raised. It is
consistent with 1.5M small allocations and with `malloc_trim`'s top-of-heap
limit, but nothing here measured arena fragmentation.

## The direction a fix would take

Not proposed as work, only recorded so the next reader does not re-derive it: the
expansion is in **materialising a cJSON node per float**. A numeric fast path for
the vector arrays — reading the floats straight out of the response text into the
`f32` buffer the index already keeps, without building the tree — would remove
the tenfold expansion at its source. It is confined to `jc_embed_parse`, and
`vectors.f32` shows the destination format is already flat.

## Afterwards: it was the strings, and the heap was mostly address space (M725, 2026-09-24)

Kept above as written, because both of its conclusions were wrong in a way worth reading.

**The allocation site was `parse_string`**, as the massif tree above says in its second line; the
arithmetic on float nodes matched by coincidence. `parse_string` sized every string's buffer to
the *rest of the input* and kept it, so a reply of ~1.5 MB with ~260 strings held ~190 MB. Sized
to each string's own span, the same 199-file corpus peaks at **25,596,080 B** of heap, from
**219,112,560 B**. The fast path this page sketched was then built on top and measured: **zero
bytes** off the peak, so it was not kept (ROADMAP M725).

**Resident memory barely moved: 136,080 kB to 130,692 kB** over this tree. jichi pins glibc's mmap
threshold at 128 KB (`src/util/jc_memtrim.c`), so each oversized buffer was a fresh mapping of
which a page or two was ever touched. The requested bytes are real only where nothing pages them
in lazily, such as FreeMiNT, a strict commit limit or a `ulimit -v`, and there the fix matters. The
170 MB this page measured is something else, and DEFERRED has the row for finding out what.
