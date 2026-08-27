/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_audio.c - audio media-type detection (see jc_audio.h). */

#include "jc_audio.h"

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

const char *jc_audio_media_type(const char *path)
{
    const char *base;
    const char *slash;
    const char *dot;
    const char *ext;

    if (path == NULL) {
        return NULL;
    }
    slash = strrchr(path, '/');
    base = (slash != NULL) ? slash + 1 : path;
    dot = strrchr(base, '.');
    if (dot == NULL || dot == base || dot[1] == '\0') {
        return NULL;
    }
    ext = dot + 1;
    if (ext_eq(ext, "mp3"))  return "audio/mpeg";
    if (ext_eq(ext, "mpga")) return "audio/mpeg";
    if (ext_eq(ext, "mpeg")) return "audio/mpeg";
    if (ext_eq(ext, "wav"))  return "audio/wav";
    if (ext_eq(ext, "m4a"))  return "audio/mp4";
    if (ext_eq(ext, "mp4"))  return "audio/mp4";
    if (ext_eq(ext, "flac")) return "audio/flac";
    if (ext_eq(ext, "ogg"))  return "audio/ogg";
    if (ext_eq(ext, "oga"))  return "audio/ogg";
    if (ext_eq(ext, "webm")) return "audio/webm";
    return NULL;
}
