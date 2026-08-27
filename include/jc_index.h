/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_index.h - codebase chunk index with a persisted, incremental cache.
 *
 * The index walks a workspace, splits text files into line-bounded chunks,
 * embeds each chunk (via jc_embed), and stores the resulting float32 vectors.
 * A disk cache under ~/.jichi.d/index/<key>/ lets a rebuild re-embed
 * only the files whose mtime changed.
 *
 * Search returns the chunks most cosine-similar to a query vector. The
 * `codebase_search` tool layers reranking on top of these results.
 *
 * Index memory is malloc-backed (not the per-turn arena); release it with
 * jc_index_free.
 */
#ifndef JC_INDEX_H
#define JC_INDEX_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_config.h"
#include "jc_vec.h"

struct jc_index; /* opaque */

/* ---- pure helpers (no I/O; unit-testable offline) ----------------------- */

/* A 1-based inclusive range of source lines covered by one chunk. */
struct jc_line_range { int start; int end; };

/* Group `text` (of byte length `len`) into line-bounded chunks of roughly
 * CHUNK_CHARS each, appending a struct jc_line_range per chunk to `out` (an
 * initialised jc_vec of struct jc_line_range). Every line lands in exactly one
 * chunk; the ranges partition lines 1..N contiguously. */
jc_status jc_chunk_ranges(const char *text, jc_size len, struct jc_vec *out);

/* Rank `count` row vectors (a contiguous count*dim float array) by cosine
 * similarity to `qvec` (length dim). Writes up to `top_n` results, best first,
 * into out_idx/out_score. Returns the number written. Network-free. */
int jc_cosine_topn(const float *vectors, int count, int dim,
                   const float *qvec, int top_n,
                   int *out_idx, double *out_score);

/* The host's byte order as a manifest tag: "le" or "be" (M136). The vectors.f32
 * blob is raw host-endian float32, so the manifest records the writer's order
 * and a cache written on a foreign-endian machine (e.g. a shared $HOME mounted
 * across architectures) is treated as stale and rebuilt instead of silently
 * misread. Pure; unit-tested. */
const char *jc_index_endian_tag(void);

/* Build (or incrementally update) the index for workspace `root` using the
 * embedding model `m`, then load it into memory. When `reindex` is non-zero the
 * on-disk cache is ignored and everything is re-embedded. On success returns
 * JC_OK and *out (free with jc_index_free). `abort` may be NULL.
 *
 * If `stats` is non-NULL it receives counts describing the build. */
struct jc_index_stats {
    int files;          /* text files indexed                     */
    int chunks;         /* total chunks                           */
    int embedded;       /* chunks (re)embedded this build         */
    int reused;         /* chunks served from the cache           */
    /* M483: directories under the root that could not be READ, and whose
     * contents are therefore absent from the index. The walk used to return
     * JC_OK for them under a comment reading "unreadable dir: skip quietly"
     * (which cannot be quoted verbatim here -- C has no nested comments, and
     * trying to cost this file a build), so an unreadable subtree produced a
     * smaller, entirely plausible file count and nothing anywhere said a word.
     * A later codebase_search then answers "no matching code found" for that
     * subtree, which a model reads as "the code does not contain this".
     * Zero on a healthy build. */
    int unreadable_dirs;
};

/* `pdf_cmd` enables PDF text indexing when non-NULL: `.pdf` files (normally
 * skipped) are included and their text extracted via that command (e.g.
 * "pdftotext"). NULL keeps the prior behaviour (PDFs skipped) — the codebase
 * index passes NULL; docs sources pass the configured extractor (M42/M44). */
jc_status jc_index_build(const char *root, const struct jc_model_cfg *m,
                         int reindex, const char *pdf_cmd, struct jc_index **out,
                         struct jc_index_stats *stats, volatile int *abort,
                         const struct jc_vec *ignore_dirs);

/* M612: the on-disk cache directory for `root` (~/.jichi.d/index/<key>). Public
 * so `prune` can compute -- and protect -- the CURRENT workspace's own index
 * when it sweeps the cache; the cache had no retention and grew one directory
 * per distinct workspace forever. */
void jc_index_cache_dir(const char *root, char *buf, jc_size cap);

int jc_index_count(const struct jc_index *idx);
/* Directories skipped because they could not be read (M483). Carried ON THE
 * INDEX, not only in the build stats, because the index is built once and then
 * searched many times: the caller that must warn (codebase_search's result, read
 * by a model) is not the caller that built it. */
int jc_index_unreadable_dirs(const struct jc_index *idx);
int jc_index_dim(const struct jc_index *idx);

const char *jc_index_chunk_path(const struct jc_index *idx, int i);
int         jc_index_chunk_start(const struct jc_index *idx, int i);
int         jc_index_chunk_end(const struct jc_index *idx, int i);
const char *jc_index_chunk_text(const struct jc_index *idx, int i);

/* Rank chunks by cosine similarity to `qvec` (length must equal the index
 * dimension). Writes up to `top_n` results, best first, into out_idx/out_score
 * (each at least `top_n` long). Returns the number written. */
int jc_index_search(const struct jc_index *idx, const float *qvec, int top_n,
                    int *out_idx, double *out_score);

void jc_index_free(struct jc_index *idx);

#ifdef __cplusplus
}
#endif
#endif /* JC_INDEX_H */
