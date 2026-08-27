/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_image.c - image helpers (see jc_image.h). */

#include "jc_image.h"

#include <string.h>

/* Case-insensitive compare of `a` against lowercase literal `b`. */
static int ext_eq(const char *a, const char *b)
{
    jc_size i;
    for (i = 0; b[i] != '\0'; i++) {
        char c = a[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != b[i]) {
            return 0;
        }
    }
    return a[i] == '\0';
}

/* The lowercase-comparable extension (after the last '.') of a path's
 * basename, or NULL when there is none. A '.' in a parent dir, a trailing
 * slash, a dotfile (".png"), or a trailing dot ("x.") all yield NULL. */
static const char *path_ext(const char *path)
{
    const char *base;
    const char *slash;
    const char *dot;

    if (path == NULL) {
        return NULL;
    }
    slash = strrchr(path, '/');
    base = (slash != NULL) ? slash + 1 : path;
    dot = strrchr(base, '.');
    if (dot == NULL || dot == base || dot[1] == '\0') {
        return NULL;
    }
    return dot + 1;
}

const char *jc_image_media_type(const char *path)
{
    const char *ext = path_ext(path);
    if (ext == NULL) {
        return NULL;
    }
    if (ext_eq(ext, "png"))  return "image/png";
    if (ext_eq(ext, "jpg"))  return "image/jpeg";
    if (ext_eq(ext, "jpeg")) return "image/jpeg";
    if (ext_eq(ext, "gif"))  return "image/gif";
    if (ext_eq(ext, "webp")) return "image/webp";
    return NULL;
}

const char *jc_image_gen_format(const char *path)
{
    const char *ext = path_ext(path);
    if (ext == NULL) {
        return NULL;
    }
    if (ext_eq(ext, "png"))  return "png";
    if (ext_eq(ext, "jpg"))  return "jpeg";
    if (ext_eq(ext, "jpeg")) return "jpeg";
    if (ext_eq(ext, "webp")) return "webp";
    return NULL;
}
