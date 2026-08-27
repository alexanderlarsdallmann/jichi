/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_index.c - codebase chunk index with a persisted, incremental cache.
 *
 * On-disk layout (per workspace, keyed by a hash of the absolute root):
 *   ~/.jichi.d/index/<key>/manifest.json   metadata + chunk map
 *   ~/.jichi.d/index/<key>/vectors.f32      raw host-endian float32 blob
 *
 * The blob is count*dim float32 values, row i belonging to chunk i. It carries
 * no header; dim/count live in the manifest and the blob is validated by size.
 * Because the blob is host-endian and unversioned, a stale or foreign cache is
 * simply rebuilt (model/dim/size mismatch => full re-embed). The manifest also
 * records the writer's byte order (`"endian"`, M136) so a cache written on a
 * foreign-endian machine -- a shared $HOME mounted across architectures -- is
 * rebuilt instead of silently misread.
 */

#include "jc_index.h"
#include "jc_repomap.h"  /* jc_walk_skip_dir: one skip list, both walkers */
#include "jc_embed.h"
#include "jc_json.h"
#include "jc_mem.h"
#include "jc_str.h"
#include "jc_vec.h"
#include "jc_snprintf.h"
#include "jc_log.h"
#include "jc_pdf.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHUNK_CHARS    1500
#define MAX_FILE_BYTES (1024L * 1024L)
/* PDFs index their extracted *text*, not raw bytes, so a larger raw cap (their
 * embedded images inflate file size) — only consulted when PDF indexing is on. */
#define PDF_MAX_FILE_BYTES (32L * 1024L * 1024L)

struct jc_chunk {
    char *path;   /* malloc'd: path as walked (under root) */
    int   start;  /* 1-based first line                    */
    int   end;    /* inclusive last line                   */
    char *text;   /* malloc'd chunk text                   */
};

struct jc_index {
    char           *root;
    int             dim;
    int             count;
    int             unreadable_dirs; /* M483: holes in the walk, see the header */
    struct jc_chunk *chunks; /* count */
    float          *vectors; /* count*dim */
    int             vec_mapped;  /* M141: vectors is an mmap of vectors.f32
                                  * (read-only, file-backed => the OS can
                                  * evict the pages under memory pressure),
                                  * adopted when the cache was fully clean */
    jc_size         vec_maplen;  /* mapping length for munmap               */
};

/* ---- small helpers ------------------------------------------------------ */

/* malloc a NUL-terminated copy of the first `n` bytes of `s`. */
static char *dup_n(const char *s, jc_size n)
{
    char *p = (char *)malloc(n + 1);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static unsigned long djb2(const char *s)
{
    unsigned long h = 5381u;
    while (*s != '\0') {
        h = ((h << 5) + h) + (unsigned char)*s;
        s++;
    }
    return h;
}

static void cache_dir(const char *root, char *buf, jc_size cap)
{
    jc_snprintf(buf, cap, "%s/.jichi.d/index/%lu",
                jc_home_dir(), djb2(root));
}

void jc_index_cache_dir(const char *root, char *buf, jc_size cap)
{
    cache_dir(root, buf, cap);
}

static int has_suffix(const char *s, const char *suf)
{
    jc_size ls = strlen(s);
    jc_size lf = strlen(suf);
    return ls >= lf && strcmp(s + ls - lf, suf) == 0;
}

/* Obvious binary/asset extensions to skip without reading. */
static int skip_file_ext(const char *name)
{
    static const char *exts[] = {
        ".o", ".a", ".so", ".obj", ".exe", ".class", ".jar", ".wasm",
        ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico", ".pdf",
        ".zip", ".gz", ".tar", ".bz2", ".xz", ".7z", ".bin", ".lock", 0
    };
    int i;
    for (i = 0; exts[i] != 0; i++) {
        if (has_suffix(name, exts[i])) {
            return 1;
        }
    }
    return 0;
}

/* True if the first bytes contain a NUL (treat as binary). */
static int looks_binary(const char *data, jc_size len)
{
    jc_size n = len < 4096 ? len : 4096;
    jc_size i;
    for (i = 0; i < n; i++) {
        if (data[i] == '\0') {
            return 1;
        }
    }
    return 0;
}

/* ---- file metadata accumulated during a build --------------------------- */

struct file_meta {
    char  *path;   /* malloc'd path (matches chunk paths) */
    double mtime;
    int    first;  /* index of this file's first chunk */
    int    count;  /* number of chunks */
};

/* ---- chunking ----------------------------------------------------------- */

jc_status jc_chunk_ranges(const char *text, jc_size len, struct jc_vec *out)
{
    const char *p = text;
    const char *textend = text + len;
    int line = 1;
    int start_line = 1;
    jc_size cur_chars = 0;

    while (p < textend && *p != '\0') {
        const char *nl = strchr(p, '\n');
        const char *lineend = (nl != NULL) ? nl + 1 : textend;
        jc_size llen = (jc_size)(lineend - p);

        if (cur_chars > 0 && cur_chars + llen > (jc_size)CHUNK_CHARS) {
            struct jc_line_range r;
            r.start = start_line;
            r.end = line - 1;
            if (jc_vec_push(out, &r) != JC_OK) {
                return JC_ERR_OOM;
            }
            start_line = line;
            cur_chars = 0;
        }
        cur_chars += llen;
        line++;
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    if (cur_chars > 0) {
        struct jc_line_range r;
        r.start = start_line;
        r.end = line - 1;
        if (jc_vec_push(out, &r) != JC_OK) {
            return JC_ERR_OOM;
        }
    }
    return JC_OK;
}

/* Split `text` into chunks via jc_chunk_ranges and append a struct jc_chunk
 * (with a malloc'd text slice) per range to `chunks`. Returns the chunk count
 * produced (>=0), or -1 on allocation failure. */
static int chunk_text(struct jc_vec *chunks, const char *path,
                      const char *text, jc_size len)
{
    struct jc_vec ranges;
    const char *p = text;
    const char *textend = text + len;
    int cur_line = 1;
    jc_size ri;
    int produced;

    jc_vec_init(&ranges, sizeof(struct jc_line_range));
    if (jc_chunk_ranges(text, len, &ranges) != JC_OK) {
        jc_vec_free(&ranges);
        return -1;
    }

    for (ri = 0; ri < ranges.len; ri++) {
        struct jc_line_range *r =
            (struct jc_line_range *)jc_vec_at(&ranges, ri);
        const char *chunk_begin = p;
        struct jc_chunk c;

        while (cur_line <= r->end && p < textend) {
            const char *nl = strchr(p, '\n');
            p = (nl != NULL) ? nl + 1 : textend;
            cur_line++;
        }
        c.path = jc_strdup(path);
        c.start = r->start;
        c.end = r->end;
        c.text = dup_n(chunk_begin, (jc_size)(p - chunk_begin));
        if (c.path == NULL || c.text == NULL) {
            free(c.path);
            free(c.text);
            jc_vec_free(&ranges);
            return -1;
        }
        if (jc_vec_push(chunks, &c) != JC_OK) {
            free(c.path);
            free(c.text);
            jc_vec_free(&ranges);
            return -1;
        }
    }
    produced = (int)ranges.len;
    jc_vec_free(&ranges);
    return produced;
}

/* ---- directory walk ----------------------------------------------------- */

/* `depth` separates the two cases the old code gave one answer to (M483):
 * the ROOT failing to list is not a partial result -- there is no index to
 * speak of -- while a subdirectory failing is a hole, and a hole must be
 * COUNTED rather than swallowed. `unreadable` accumulates the holes. */
static jc_status walk(const char *dir, struct jc_vec *files, struct jc_arena *a,
                      int with_pdf, int depth, int *unreadable,
                      const struct jc_vec *ignore)
{
    struct jc_vec names;
    jc_size i;
    jc_status st;

    jc_vec_init(&names, sizeof(char *));
    st = jc_list_dir(dir, &names, a);
    if (st != JC_OK) {
        jc_vec_free(&names);
        if (depth == 0) {
            return st;   /* the workspace root itself: a hard error */
        }
        (*unreadable)++;
        return JC_OK;    /* a hole, now counted and reported upward */
    }
    for (i = 0; i < names.len; i++) {
        const char *name = *(char **)jc_vec_at(&names, i);
        char *full = (char *)jc_arena_alloc(a, strlen(dir) + strlen(name) + 2);
        if (full == NULL) {
            jc_vec_free(&names);
            return JC_ERR_OOM;
        }
        jc_snprintf(full, strlen(dir) + strlen(name) + 2, "%s/%s", dir, name);

        if (jc_is_dir(full)) {
            if (jc_walk_skip_dir(name, ignore)) {
                continue;
            }
            st = walk(full, files, a, with_pdf, depth + 1, unreadable,
                      ignore);
            if (st != JC_OK) {
                jc_vec_free(&names);
                return st;
            }
        } else {
            /* PDFs are normally skipped; when PDF indexing is on they're kept
             * (their extracted text is indexed) with a larger raw-size cap. */
            int is_pdf = with_pdf && jc_pdf_is_pdf(name);
            long maxsz = is_pdf ? PDF_MAX_FILE_BYTES : MAX_FILE_BYTES;
            if ((skip_file_ext(name) && !is_pdf) ||
                jc_file_size(full) > maxsz) {
                continue;
            }
            if (jc_vec_push(files, &full) != JC_OK) {
                jc_vec_free(&names);
                return JC_ERR_OOM;
            }
        }
    }
    jc_vec_free(&names);
    return JC_OK;
}

/* ---- cache I/O ----------------------------------------------------------- */

static float *read_blob(const char *path, jc_size nfloats)
{
    FILE *f;
    float *buf;
    size_t got;

    if (nfloats == 0) {
        return NULL;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    buf = (float *)malloc(nfloats * sizeof(float));
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    got = fread(buf, sizeof(float), nfloats, f);
    fclose(f);
    if (got != nfloats) {
        free(buf);
        return NULL;
    }
    return buf;
}

/* M141: map the blob read-only instead of malloc+fread -- the pages stay
 * file-backed (evictable under memory pressure, shared COW across forks).
 * Returns NULL on any failure; callers fall back to read_blob. */
static float *map_blob(const char *path, jc_size nfloats, jc_size *maplen)
{
    int fd;
    struct stat sst;
    void *p;
    jc_size len = nfloats * sizeof(float);

    *maplen = 0;
    if (nfloats == 0) {
        return NULL;
    }
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }
    if (fstat(fd, &sst) != 0 || (jc_size)sst.st_size < len) {
        close(fd);
        return NULL;
    }
    p = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd); /* the mapping outlives the descriptor */
    if (p == MAP_FAILED) {
        return NULL;
    }
    *maplen = len;
    return (float *)p;
}

/* Release a blob obtained from map_blob (munmap) or read_blob (free). */
static void release_blob(float *v, int mapped, jc_size maplen)
{
    if (v == NULL) {
        return;
    }
    if (mapped) {
        munmap((void *)v, maplen);
    } else {
        free(v);
    }
}

/* Look up a file's cached record by path in the manifest's "files" array.
 * Returns the cJSON file object or NULL. */
static cJSON *find_cached_file(cJSON *old_files, const char *path)
{
    cJSON *fe;
    if (!cJSON_IsArray(old_files)) {
        return NULL;
    }
    for (fe = old_files->child; fe != NULL; fe = fe->next) {
        const char *p = jc_json_get_str(fe, "path", NULL);
        if (p != NULL && strcmp(p, path) == 0) {
            return fe;
        }
    }
    return NULL;
}

static jc_status write_cache(const char *dir, const char *root,
                             const char *model, int dim,
                             struct jc_chunk *chunks, int count,
                             const float *vectors,
                             struct file_meta *files, int nfiles)
{
    char path[1024];
    cJSON *root_obj;
    cJSON *farr;
    cJSON *carr;
    char *text;
    jc_status st;
    int i;
    FILE *f;

    jc_mkdir_p(dir);

    root_obj = cJSON_CreateObject();
    if (root_obj == NULL) {
        return JC_ERR_OOM;
    }
    cJSON_AddNumberToObject(root_obj, "version", 1);
    cJSON_AddStringToObject(root_obj, "endian", jc_index_endian_tag());
    cJSON_AddStringToObject(root_obj, "root", root);
    cJSON_AddStringToObject(root_obj, "model", model != NULL ? model : "");
    cJSON_AddNumberToObject(root_obj, "dim", (double)dim);
    cJSON_AddNumberToObject(root_obj, "count", (double)count);

    farr = cJSON_AddArrayToObject(root_obj, "files");
    for (i = 0; i < nfiles; i++) {
        cJSON *fe = cJSON_CreateObject();
        cJSON_AddStringToObject(fe, "path", files[i].path);
        cJSON_AddNumberToObject(fe, "mtime", files[i].mtime);
        cJSON_AddNumberToObject(fe, "first", (double)files[i].first);
        cJSON_AddNumberToObject(fe, "count", (double)files[i].count);
        cJSON_AddItemToArray(farr, fe);
    }

    carr = cJSON_AddArrayToObject(root_obj, "chunks");
    for (i = 0; i < count; i++) {
        cJSON *ce = cJSON_CreateObject();
        cJSON_AddStringToObject(ce, "path", chunks[i].path);
        cJSON_AddNumberToObject(ce, "start", (double)chunks[i].start);
        cJSON_AddNumberToObject(ce, "end", (double)chunks[i].end);
        cJSON_AddItemToArray(carr, ce);
    }

    text = jc_json_print(root_obj);
    cJSON_Delete(root_obj);
    if (text == NULL) {
        return JC_ERR_OOM;
    }
    jc_snprintf(path, sizeof(path), "%s/manifest.json", dir);
    st = jc_write_file(path, text, strlen(text));
    free(text);
    if (st != JC_OK) {
        return st;
    }

    jc_snprintf(path, sizeof(path), "%s/vectors.f32", dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        return JC_ERR_IO;
    }
    if (count > 0 && dim > 0) {
        size_t want = (size_t)count * (size_t)dim;
        if (fwrite(vectors, sizeof(float), want, f) != want) {
            fclose(f);
            return JC_ERR_IO;
        }
    }
    fclose(f);
    return JC_OK;
}

const char *jc_index_endian_tag(void)
{
    /* Runtime probe (C89 has no standard endianness macro): inspect the first
     * byte of a known integer. */
    static const unsigned int probe = 1u;
    return (*(const unsigned char *)&probe == 1u) ? "le" : "be";
}

/* ---- search ------------------------------------------------------------- */

int jc_cosine_topn(const float *vectors, int count, int dim,
                   const float *qvec, int top_n,
                   int *out_idx, double *out_score)
{
    int i;
    int d;
    int filled = 0;
    double qnorm = 0.0;

    if (vectors == NULL || count <= 0 || dim <= 0 || top_n <= 0) {
        return 0;
    }
    for (d = 0; d < dim; d++) {
        qnorm += (double)qvec[d] * (double)qvec[d];
    }
    qnorm = sqrt(qnorm);
    if (qnorm == 0.0) {
        return 0;
    }

    for (i = 0; i < count; i++) {
        const float *row = vectors + (jc_size)i * (jc_size)dim;
        double dot = 0.0;
        double rnorm = 0.0;
        double score;
        int j;
        for (d = 0; d < dim; d++) {
            dot += (double)qvec[d] * (double)row[d];
            rnorm += (double)row[d] * (double)row[d];
        }
        rnorm = sqrt(rnorm);
        if (rnorm == 0.0) {
            continue;
        }
        score = dot / (qnorm * rnorm);

        /* Insertion into a descending top-n list. */
        if (filled < top_n) {
            j = filled++;
        } else if (score > out_score[top_n - 1]) {
            j = top_n - 1;
        } else {
            continue;
        }
        while (j > 0 && out_score[j - 1] < score) {
            out_score[j] = out_score[j - 1];
            out_idx[j] = out_idx[j - 1];
            j--;
        }
        out_score[j] = score;
        out_idx[j] = i;
    }
    return filled;
}

int jc_index_search(const struct jc_index *idx, const float *qvec, int top_n,
                    int *out_idx, double *out_score)
{
    if (idx == NULL) {
        return 0;
    }
    return jc_cosine_topn(idx->vectors, idx->count, idx->dim, qvec, top_n,
                          out_idx, out_score);
}

/* ---- accessors ---------------------------------------------------------- */

int jc_index_unreadable_dirs(const struct jc_index *idx)
{
    return idx != NULL ? idx->unreadable_dirs : 0;
}

int jc_index_count(const struct jc_index *idx)
{
    return idx != NULL ? idx->count : 0;
}

int jc_index_dim(const struct jc_index *idx)
{
    return idx != NULL ? idx->dim : 0;
}

const char *jc_index_chunk_path(const struct jc_index *idx, int i)
{
    if (idx == NULL || i < 0 || i >= idx->count) {
        return NULL;
    }
    return idx->chunks[i].path;
}

int jc_index_chunk_start(const struct jc_index *idx, int i)
{
    return (idx != NULL && i >= 0 && i < idx->count) ? idx->chunks[i].start : 0;
}

int jc_index_chunk_end(const struct jc_index *idx, int i)
{
    return (idx != NULL && i >= 0 && i < idx->count) ? idx->chunks[i].end : 0;
}

const char *jc_index_chunk_text(const struct jc_index *idx, int i)
{
    if (idx == NULL || i < 0 || i >= idx->count) {
        return NULL;
    }
    return idx->chunks[i].text;
}

void jc_index_free(struct jc_index *idx)
{
    int i;
    if (idx == NULL) {
        return;
    }
    for (i = 0; i < idx->count; i++) {
        free(idx->chunks[i].path);
        free(idx->chunks[i].text);
    }
    free(idx->chunks);
    if (idx->vec_mapped) {
        munmap((void *)idx->vectors, idx->vec_maplen); /* M141 */
    } else {
        free(idx->vectors);
    }
    free(idx->root);
    free(idx);
}

/* ---- build -------------------------------------------------------------- */

/* Free a partially built chunk vector's owned strings. */
static void free_chunk_vec(struct jc_vec *chunks)
{
    jc_size i;
    for (i = 0; i < chunks->len; i++) {
        struct jc_chunk *c = (struct jc_chunk *)jc_vec_at(chunks, i);
        free(c->path);
        free(c->text);
    }
    jc_vec_free(chunks);
}

jc_status jc_index_build(const char *root, const struct jc_model_cfg *m,
                         int reindex, const char *pdf_cmd, struct jc_index **out,
                         struct jc_index_stats *stats, volatile int *abort,
                         const struct jc_vec *ignore_dirs)
{
    struct jc_arena *a;
    struct jc_vec files;       /* of char* (full paths) */
    struct jc_vec chunks;      /* of struct jc_chunk    */
    struct jc_vec reuse_src;   /* of int per chunk: old chunk index or -1 */
    struct jc_vec fmeta;       /* of struct file_meta   */
    char dir[1024];
    char mpath[1100];
    cJSON *old = NULL;
    cJSON *old_files = NULL;
    float *old_vecs = NULL;
    int old_mapped = 0;      /* old_vecs came from map_blob (M141)       */
    jc_size old_maplen = 0;
    int cache_clean = 0;     /* fully-reused, in-order cache: skip rewrite */
    int old_dim = 0;
    int old_count = 0;
    int dim = 0;
    int count;
    int n_embed = 0;
    int n_reuse = 0;
    int n_unreadable = 0;
    jc_size fi;
    int i;
    jc_status st = JC_OK;
    struct jc_index *idx = NULL;

    *out = NULL;
    if (root == NULL || m == NULL) {
        return JC_ERR_INVALID;
    }

    a = jc_arena_new(0);
    if (a == NULL) {
        return JC_ERR_OOM;
    }
    jc_vec_init(&files, sizeof(char *));
    jc_vec_init(&chunks, sizeof(struct jc_chunk));
    jc_vec_init(&reuse_src, sizeof(int));
    jc_vec_init(&fmeta, sizeof(struct file_meta));

    cache_dir(root, dir, sizeof(dir));

    /* Load an existing manifest unless a full reindex was requested. */
    if (!reindex) {
        char *mtext;
        jc_snprintf(mpath, sizeof(mpath), "%s/manifest.json", dir);
        if (jc_read_file(mpath, &mtext, NULL, a) == JC_OK) {
            old = jc_json_parse(mtext);
            if (old != NULL) {
                const char *omodel = jc_json_get_str(old, "model", "");
                const char *oendian = jc_json_get_str(old, "endian", "");
                old_dim = (int)jc_json_get_num(old, "dim", 0.0);
                old_count = (int)jc_json_get_num(old, "count", 0.0);
                /* Different model, or a blob written with a foreign byte order
                 * (M136; a pre-M136 manifest has no tag and rebuilds once):
                 * ignore the cache. */
                if ((m->model != NULL && strcmp(omodel, m->model) != 0) ||
                    strcmp(oendian, jc_index_endian_tag()) != 0) {
                    cJSON_Delete(old);
                    old = NULL;
                    old_dim = old_count = 0;
                }
            }
        }
    }
    if (old != NULL && old_count > 0 && old_dim > 0) {
        char bpath[1100];
        jc_snprintf(bpath, sizeof(bpath), "%s/vectors.f32", dir);
        old_vecs = map_blob(bpath, (jc_size)old_count * (jc_size)old_dim,
                            &old_maplen);
        old_mapped = (old_vecs != NULL);
        if (old_vecs == NULL) { /* mmap unavailable: fall back to a copy */
            old_vecs = read_blob(bpath,
                                 (jc_size)old_count * (jc_size)old_dim);
        }
        if (old_vecs == NULL) {
            cJSON_Delete(old); /* blob missing/short: ignore cache */
            old = NULL;
        }
    }
    old_files = (old != NULL) ? jc_json_get_obj(old, "files") : NULL;

    /* Walk the workspace. PDF text indexing is opt-in (pdf_cmd != NULL — set by
     * docs sources, not the codebase index). */
    n_unreadable = 0;
    st = walk(root, &files, a, pdf_cmd != NULL, 0, &n_unreadable,
              ignore_dirs);
    if (st != JC_OK) {
        goto done;
    }

    /* Chunk every file; reuse cached vectors for unchanged files. */
    for (fi = 0; fi < files.len; fi++) {
        const char *path = *(char **)jc_vec_at(&files, fi);
        char *text;
        jc_size len = 0;
        double mt = jc_file_mtime(path);
        int first = (int)chunks.len;
        int produced;
        cJSON *cached;
        int reuse_ok = 0;
        int old_first = 0;

        if (abort != NULL && *abort) {
            st = JC_ERR_ABORTED;
            goto done;
        }
        if (pdf_cmd != NULL && jc_pdf_is_pdf(path)) {
            /* Index the PDF's extracted text (chunk_text copies it). A missing
             * extractor or extraction error just skips this file. */
            struct jc_sb px;
            jc_sb_init(&px);
            if (jc_pdf_extract(path, pdf_cmd, MAX_FILE_BYTES, &px, abort)
                != JC_OK) {
                jc_sb_free(&px);
                continue;
            }
            produced = chunk_text(&chunks, path,
                                  px.data != NULL ? px.data : "", px.len);
            jc_sb_free(&px);
        } else {
        /* M198: scanned path -- skip a FIFO/socket/device that would
         * otherwise block the read forever (see jc_is_regular_file). */
            if (!jc_is_regular_file(path)) {
                continue;
            }
            if (jc_read_file(path, &text, &len, a) != JC_OK) {
                continue;
            }
            if (looks_binary(text, len)) {
                continue;
            }
            produced = chunk_text(&chunks, path, text, len);
        }
        if (produced < 0) {
            st = JC_ERR_OOM;
            goto done;
        }
        if (produced == 0) {
            continue;
        }

        cached = (old_files != NULL) ? find_cached_file(old_files, path) : NULL;
        if (cached != NULL) {
            double omt = jc_json_get_num(cached, "mtime", -1.0);
            int ocount = (int)jc_json_get_num(cached, "count", -1.0);
            old_first = (int)jc_json_get_num(cached, "first", -1.0);
            if (omt == mt && ocount == produced && old_first >= 0 &&
                old_first + produced <= old_count) {
                reuse_ok = 1;
            }
        }
        for (i = 0; i < produced; i++) {
            int src = reuse_ok ? (old_first + i) : -1;
            if (jc_vec_push(&reuse_src, &src) != JC_OK) {
                st = JC_ERR_OOM;
                goto done;
            }
            if (reuse_ok) {
                n_reuse++;
            } else {
                n_embed++;
            }
        }
        {
            struct file_meta fm;
            fm.path = jc_strdup(path);
            fm.mtime = mt;
            fm.first = first;
            fm.count = produced;
            if (fm.path == NULL || jc_vec_push(&fmeta, &fm) != JC_OK) {
                free(fm.path);
                st = JC_ERR_OOM;
                goto done;
            }
        }
    }

    count = (int)chunks.len;
    if (count == 0) {
        /* Empty workspace: write an empty cache and return an empty index. */
        dim = (old_dim > 0) ? old_dim : 0;
    }

    /* Determine dim and gather text to embed. */
    if (n_reuse > 0) {
        dim = old_dim;
    }

    /* Embed the chunks that need it. */
    if (n_embed > 0) {
        const char **texts = (const char **)malloc((jc_size)n_embed *
                                                    sizeof(char *));
        int *embed_chunk = (int *)malloc((jc_size)n_embed * sizeof(int));
        float *emb = NULL;
        int emb_dim = 0;
        int k = 0;

        if (texts == NULL || embed_chunk == NULL) {
            free(texts);
            free(embed_chunk);
            st = JC_ERR_OOM;
            goto done;
        }
        for (i = 0; i < count; i++) {
            if (*(int *)jc_vec_at(&reuse_src, (jc_size)i) < 0) {
                struct jc_chunk *c =
                    (struct jc_chunk *)jc_vec_at(&chunks, (jc_size)i);
                texts[k] = c->text;
                embed_chunk[k] = i;
                k++;
            }
        }
        st = jc_embed_texts(m, texts, n_embed, &emb, &emb_dim, abort);
        free(texts);
        if (st != JC_OK) {
            free(embed_chunk);
            goto done;
        }
        if (n_reuse > 0 && emb_dim != old_dim) {
            /* Dimension changed under us: discard cache and rebuild fully. */
            free(emb);
            free(embed_chunk);
            release_blob(old_vecs, old_mapped, old_maplen);
            old_vecs = NULL;
            old_mapped = 0;
            if (old != NULL) {
                cJSON_Delete(old);
                old = NULL;
            }
            free_chunk_vec(&chunks);
            jc_vec_free(&reuse_src);
            {
                jc_size j;
                for (j = 0; j < fmeta.len; j++) {
                    free(((struct file_meta *)jc_vec_at(&fmeta, j))->path);
                }
            }
            jc_vec_free(&fmeta);
            jc_vec_free(&files);
            jc_arena_free(a);
            return jc_index_build(root, m, 1, pdf_cmd, out, stats, abort,
                                  ignore_dirs);
        }
        dim = emb_dim;

        /* Assemble the final vector array. */
        idx = (struct jc_index *)calloc(1, sizeof(struct jc_index));
        if (idx == NULL) {
            free(emb);
            free(embed_chunk);
            st = JC_ERR_OOM;
            goto done;
        }
        idx->vectors = (float *)malloc((jc_size)count * (jc_size)dim *
                                       sizeof(float));
        if (idx->vectors == NULL) {
            free(idx);
            free(emb);
            free(embed_chunk);
            st = JC_ERR_OOM;
            goto done;
        }
        /* Reused rows from the old blob. */
        for (i = 0; i < count; i++) {
            int src = *(int *)jc_vec_at(&reuse_src, (jc_size)i);
            if (src >= 0) {
                memcpy(idx->vectors + (jc_size)i * (jc_size)dim,
                       old_vecs + (jc_size)src * (jc_size)dim,
                       (jc_size)dim * sizeof(float));
            }
        }
        /* Freshly embedded rows. */
        for (k = 0; k < n_embed; k++) {
            memcpy(idx->vectors + (jc_size)embed_chunk[k] * (jc_size)dim,
                   emb + (jc_size)k * (jc_size)dim,
                   (jc_size)dim * sizeof(float));
        }
        free(emb);
        free(embed_chunk);
    } else {
        /* Nothing to embed: either fully cached or empty. */
        idx = (struct jc_index *)calloc(1, sizeof(struct jc_index));
        if (idx == NULL) {
            st = JC_ERR_OOM;
            goto done;
        }
        if (count > 0 && dim > 0) {
            /* M141: identity reuse -- every chunk came from the cache, in
             * order, same count -- means the old blob IS the new vector
             * array. Adopt it (mmap'd: file-backed, evictable; else the
             * already-loaded copy) instead of malloc+memcpy'ing 100% of it,
             * and skip the byte-identical cache rewrite below. */
            int identity = (count == old_count);
            for (i = 0; identity && i < count; i++) {
                if (*(int *)jc_vec_at(&reuse_src, (jc_size)i) != i) {
                    identity = 0;
                }
            }
            if (identity) {
                idx->vectors = old_vecs;
                idx->vec_mapped = old_mapped;
                idx->vec_maplen = old_maplen;
                old_vecs = NULL; /* ownership moved to idx */
                old_mapped = 0;
                cache_clean = 1;
            } else {
                idx->vectors = (float *)malloc((jc_size)count * (jc_size)dim *
                                               sizeof(float));
                if (idx->vectors == NULL) {
                    free(idx);
                    st = JC_ERR_OOM;
                    goto done;
                }
                for (i = 0; i < count; i++) {
                    int src = *(int *)jc_vec_at(&reuse_src, (jc_size)i);
                    if (src >= 0) {
                        memcpy(idx->vectors + (jc_size)i * (jc_size)dim,
                               old_vecs + (jc_size)src * (jc_size)dim,
                               (jc_size)dim * sizeof(float));
                    }
                }
            }
        }
    }

    /* Move chunk metadata into the index. */
    idx->root = jc_strdup(root);
    idx->dim = dim;
    idx->count = count;
    /* Carried on the index so a SEARCH can warn, not only the build (M483). */
    idx->unreadable_dirs = n_unreadable;
    if (count > 0) {
        idx->chunks = (struct jc_chunk *)malloc((jc_size)count *
                                                sizeof(struct jc_chunk));
        if (idx->chunks == NULL) {
            release_blob(idx->vectors, idx->vec_mapped, idx->vec_maplen);
            free(idx->root);
            free(idx);
            st = JC_ERR_OOM;
            goto done;
        }
        for (i = 0; i < count; i++) {
            idx->chunks[i] = *(struct jc_chunk *)jc_vec_at(&chunks, (jc_size)i);
        }
    }
    /* The chunk vec's element structs were copied (pointers moved); free the
     * container only, not the owned strings. */
    jc_vec_free(&chunks);
    chunks.len = 0;

    /* Old rows are fully copied (or adopted) by now: release the old blob
     * BEFORE any cache rewrite -- truncating a file under a live private
     * mapping invites SIGBUS on not-yet-faulted pages (M141). */
    release_blob(old_vecs, old_mapped, old_maplen);
    old_vecs = NULL;
    old_mapped = 0;

    /* Persist the refreshed cache -- unless it is byte-identical to what is
     * already on disk (identity reuse), which also keeps the adopted mapping
     * valid (M141). */
    if (!cache_clean) {
        write_cache(dir, root, m->model, dim, idx->chunks, count,
                    idx->vectors,
                    (struct file_meta *)fmeta.data, (int)fmeta.len);
    }

    if (stats != NULL) {
        stats->files = (int)fmeta.len;
        stats->chunks = count;
        stats->embedded = n_embed;
        stats->reused = n_reuse;
        stats->unreadable_dirs = n_unreadable;
    }
    *out = idx;
    st = JC_OK;

done:
    if (st != JC_OK) {
        free_chunk_vec(&chunks);
    } else {
        jc_vec_free(&chunks); /* already emptied on success */
    }
    jc_vec_free(&reuse_src);
    {
        jc_size j;
        for (j = 0; j < fmeta.len; j++) {
            free(((struct file_meta *)jc_vec_at(&fmeta, j))->path);
        }
    }
    jc_vec_free(&fmeta);
    jc_vec_free(&files);
    release_blob(old_vecs, old_mapped, old_maplen);
    if (old != NULL) {
        cJSON_Delete(old);
    }
    jc_arena_free(a);
    return st;
}
