/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_path.c - workspace path canonicalization + containment (M24).
 *
 * jc_path_under_root is pure (no syscalls) and unit-tested. jc_path_resolve and
 * jc_path_in_root wrap realpath(); they live in this POSIX-compiled TU.
 *
 * realpath() is an XSI extension; glibc only declares it when an XOPEN/2K8
 * feature-test macro is set (the build-wide -D_POSIX_C_SOURCE=200112L is not
 * enough). Request it here, before any system header is pulled in. */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "jc_path.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#if PATH_MAX > JC_PATH_MAX
#define JC_RESOLVE_BUF PATH_MAX
#else
#define JC_RESOLVE_BUF JC_PATH_MAX
#endif

int jc_path_under_root(const char *root, const char *path)
{
    size_t rlen;

    if (root == NULL || path == NULL || root[0] == '\0') {
        return 0;
    }
    rlen = strlen(root);
    /* Treat a trailing slash on root as not significant ("/work/" == "/work"). */
    while (rlen > 1 && root[rlen - 1] == '/') {
        rlen--;
    }
    if (strncmp(path, root, rlen) != 0) {
        return 0;
    }
    /* Exact match, or the next char is a separator (so "/work" does not match
     * "/workother" but does match "/work" and "/work/sub"). */
    if (path[rlen] == '\0') {
        return 1;
    }
    if (path[rlen] == '/') {
        return 1;
    }
    /* root was "/" (rlen collapses to 1): anything absolute is contained. */
    if (rlen == 1 && root[0] == '/') {
        return 1;
    }
    return 0;
}

jc_status jc_path_normalize(const char *cwd, const char *path,
                            char *out, jc_size cap)
{
    jc_size o = 0;
    const char *p;

    if (path == NULL || path[0] == '\0' || out == NULL || cap == 0) {
        return JC_ERR_INVALID;
    }
    /* Refuse ".." rather than collapse it -- see the header. A false identity
     * match makes the dedup elide a read that was NOT superseded, which loses
     * information; refusing costs only a missed dedup. */
    for (p = path; *p != '\0'; p++) {
        if (p[0] == '.' && p[1] == '.' &&
            (p == path || p[-1] == '/') &&
            (p[2] == '\0' || p[2] == '/')) {
            return JC_ERR_INVALID;
        }
    }

    if (path[0] == '/') {
        out[o++] = '/';
    } else {
        /* A relative path is interpreted against `cwd`, as the tools do. `cwd`
         * is assumed absolute (jc_app canonicalizes it at startup). */
        jc_size clen;
        if (cwd == NULL || cwd[0] != '/') {
            return JC_ERR_INVALID;
        }
        clen = (jc_size)strlen(cwd);
        while (clen > 1 && cwd[clen - 1] == '/') {
            clen--;
        }
        if (clen + 2 > cap) {
            return JC_ERR_TOOBIG;
        }
        memcpy(out, cwd, clen);
        o = clen;
        if (out[o - 1] != '/') {
            out[o++] = '/';
        }
    }

    /* Copy the remaining components, dropping empties ("//") and "." segments. */
    p = path;
    while (*p != '\0') {
        const char *seg;
        jc_size slen;
        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        seg = p;
        while (*p != '\0' && *p != '/') {
            p++;
        }
        slen = (jc_size)(p - seg);
        if (slen == 1 && seg[0] == '.') {
            continue;
        }
        if (o > 0 && out[o - 1] != '/') {
            if (o + 1 >= cap) {
                return JC_ERR_TOOBIG;
            }
            out[o++] = '/';
        }
        if (o + slen + 1 > cap) {
            return JC_ERR_TOOBIG;
        }
        memcpy(out + o, seg, slen);
        o += slen;
    }
    /* Drop the trailing separator left by a path that reduced to a directory
     * ("/foo/" -> "/foo"), but keep a bare root ("/"). */
    while (o > 1 && out[o - 1] == '/') {
        o--;
    }
    out[o] = '\0';
    return JC_OK;
}

/* M607: the most symlink hops a not-yet-existing leaf may take before the
 * resolver gives up (fail closed). Linux's own MAXSYMLINKS is 40; a legitimate
 * write target is one or two hops away, so the bound is generous and the
 * pathological case (a link to itself) stops in bounded time. */
#define JC_RESOLVE_MAX_HOPS 40

static jc_status resolve_depth(const char *path, char *out, jc_size cap,
                               int depth)
{
    char resolved[JC_RESOLVE_BUF];
    char work[JC_RESOLVE_BUF];
    size_t len;

    if (path == NULL || path[0] == '\0' || out == NULL || cap == 0) {
        return JC_ERR_INVALID;
    }
    if (depth > JC_RESOLVE_MAX_HOPS) {
        return JC_ERR_NOTFOUND; /* a symlink cycle, or a hostile chain */
    }

    /* Fast path: the target already exists. */
    if (realpath(path, resolved) != NULL) {
        len = strlen(resolved);
        if (len + 1 > cap) {
            return JC_ERR_TOOBIG;
        }
        memcpy(out, resolved, len + 1);
        return JC_OK;
    }

    /* The path does not exist yet (typical write target): canonicalize the
     * parent directory, then re-append the final component. This still resolves
     * any symlink in the parent chain. */
    len = strlen(path);
    if (len + 1 > sizeof(work)) {
        return JC_ERR_TOOBIG;
    }
    memcpy(work, path, len + 1);
    /* Strip a single trailing slash so basename is meaningful. */
    while (len > 1 && work[len - 1] == '/') {
        work[--len] = '\0';
    }

    /* M607: a leaf that EXISTS as a symlink to something that does not exist
     * yet is not "a path that does not exist yet" -- it is a redirection, and
     * realpath() failing on it is exactly why the parent branch below used to
     * re-append the leaf verbatim and call the result inside the workspace.
     * fopen("wb") then FOLLOWED the link and created its target wherever it
     * pointed. Measured: a workspace file `notes.md -> /elsewhere/escaped.txt`
     * (target absent) passed the fence under --auto and the file appeared in
     * /elsewhere (tests/smoke/pathfence_dangling.sh). A link to an EXISTING
     * outside target was always caught, because realpath() resolves it -- which
     * is why every existing test planted an existing target.
     *
     * So: if the leaf is a symlink, resolve its TARGET instead (absolute as
     * given; relative against the link's own directory), recursively and
     * bounded. A readlink failure fails closed: the fence denies what the
     * resolver cannot name. */
    {
        struct stat lst;
        if (lstat(work, &lst) == 0 && S_ISLNK(lst.st_mode)) {
            char target[JC_RESOLVE_BUF];
            ssize_t tl = readlink(work, target, sizeof(target) - 1);
            if (tl <= 0) {
                return JC_ERR_NOTFOUND;
            }
            target[tl] = '\0';
            if (target[0] == '/') {
                return resolve_depth(target, out, cap, depth + 1);
            }
            {
                /* Relative: join against the link's directory. */
                char joined[JC_RESOLVE_BUF];
                char *slash = strrchr(work, '/');
                size_t dlen = (slash == NULL) ? 0 : (size_t)(slash - work);
                size_t need = dlen + 1 + (size_t)tl + 1;
                if (need > sizeof(joined)) {
                    return JC_ERR_TOOBIG;
                }
                if (slash == NULL) {
                    memcpy(joined, target, (size_t)tl + 1);
                } else {
                    memcpy(joined, work, dlen);
                    if (dlen == 0) {
                        joined[dlen++] = '/'; /* the link sat in "/" */
                    } else {
                        joined[dlen++] = '/';
                    }
                    memcpy(joined + dlen, target, (size_t)tl + 1);
                }
                return resolve_depth(joined, out, cap, depth + 1);
            }
        }
    }

    {
        char *slash = strrchr(work, '/');
        const char *parent;
        const char *leaf;
        size_t plen;
        size_t llen;

        if (slash == NULL) {
            /* Relative leaf, no directory part: parent is the cwd. */
            parent = ".";
            leaf = work;
        } else if (slash == work) {
            /* Path like "/foo": parent is "/". */
            parent = "/";
            leaf = work + 1;
        } else {
            *slash = '\0';
            parent = work;
            leaf = slash + 1;
        }

        if (realpath(parent, resolved) == NULL) {
            return JC_ERR_NOTFOUND;
        }
        plen = strlen(resolved);
        llen = strlen(leaf);
        /* parent + '/' + leaf + '\0' */
        if (plen + 1 + llen + 1 > cap) {
            return JC_ERR_TOOBIG;
        }
        memcpy(out, resolved, plen);
        if (plen == 0 || out[plen - 1] != '/') {
            out[plen++] = '/';
        }
        memcpy(out + plen, leaf, llen + 1);
        return JC_OK;
    }
}

jc_status jc_path_resolve(const char *path, char *out, jc_size cap)
{
    return resolve_depth(path, out, cap, 0);
}

int jc_path_in_root(const char *root, const char *path)
{
    char resolved[JC_PATH_MAX];

    if (jc_path_resolve(path, resolved, sizeof(resolved)) != JC_OK) {
        return 0; /* fail closed */
    }
    return jc_path_under_root(root, resolved);
}
