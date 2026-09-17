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

    /* The target does not exist and is not a symlink. Until M638 this branch
     * split ONE component off ("parent/leaf"), canonicalized the parent and
     * re-appended the leaf -- so a write into a directory that did not exist
     * yet returned JC_ERR_NOTFOUND, jc_app_path_denied read the failure as
     * "outside", and write_file answered "refused by safety fence (path
     * outside workspace)" for `tests/bench/refute_ab/refute_ab.py` in a
     * workspace that had no tests/bench/refute_ab yet (the footer-in-anger
     * note, 2026-09-17: the model then built the file by fifty shell appends,
     * which the fence could not see at all). write_file's own ensure_parent()
     * already does mkdir -p, so the fence was refusing what the tool would
     * have done.
     *
     * M638: walk UP to the deepest ancestor that exists (lstat, so a dangling
     * symlink counts as existing and is then resolved THROUGH its target by
     * the recursion above), canonicalize it, and re-append the missing tail.
     * The tail is re-appended VERBATIM, which is only sound if the kernel
     * would walk it the same way: a `.` or `..` component in a path that does
     * not exist yet would be judged here as a name and walked there as a
     * step, so any such tail fails closed. A tail cannot contain a symlink,
     * because none of it exists. */
    {
        char prefix[JC_RESOLVE_BUF];
        const char *tail;
        size_t plen;
        size_t tlen;
        struct stat st;

        memcpy(prefix, work, len + 1);
        for (;;) {
            char *slash = strrchr(prefix, '/');
            if (slash == NULL) {
                /* Relative, nothing existing in it: the ancestor is the cwd. */
                prefix[0] = '.';
                prefix[1] = '\0';
                tail = work;
                break;
            }
            if (slash == prefix) {
                /* Down to "/x": the ancestor is the root directory. */
                prefix[1] = '\0';
                tail = work + 1;
                break;
            }
            *slash = '\0';
            if (lstat(prefix, &st) == 0) {
                tail = work + (size_t)(slash - prefix) + 1;
                break;
            }
        }

        /* Refuse a tail with an empty, `.` or `..` component. */
        {
            const char *c = tail;
            for (;;) {
                const char *e = strchr(c, '/');
                size_t cl = (e != NULL) ? (size_t)(e - c) : strlen(c);
                if (cl == 0 || (cl == 1 && c[0] == '.') ||
                    (cl == 2 && c[0] == '.' && c[1] == '.')) {
                    return JC_ERR_NOTFOUND;
                }
                if (e == NULL) {
                    break;
                }
                c = e + 1;
            }
        }

        /* The ancestor exists: realpath() it, or -- if it is itself a
         * dangling link, or otherwise resists realpath() -- let the resolver
         * name it the way it names any existing path. */
        if (realpath(prefix, resolved) == NULL) {
            jc_status st2 = resolve_depth(prefix, resolved, sizeof(resolved),
                                          depth + 1);
            if (st2 != JC_OK) {
                return st2;
            }
        }
        plen = strlen(resolved);
        tlen = strlen(tail);
        /* ancestor + '/' + tail + '\0' */
        if (plen + 1 + tlen + 1 > cap) {
            return JC_ERR_TOOBIG;
        }
        memcpy(out, resolved, plen);
        if (plen == 0 || out[plen - 1] != '/') {
            out[plen++] = '/';
        }
        memcpy(out + plen, tail, tlen + 1);
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
