/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_glossary.c - glossary loading (see jc_glossary.h). */

#include "jc_glossary.h"
#include "jc_app.h"
#include "jc_str.h"
#include "jc_utf8.h"
#include "jc_snprintf.h"
#include "jc_path.h"
#include "jc_log.h"

#include <string.h>
#include <sys/stat.h>

/* Append the file at `path` (if present, non-empty, and under root) to `sb`,
 * separated from any prior content by a blank line. Returns 1 if anything was
 * appended. The `root` parameter is the canonical workspace root; if non-NULL,
 * the file's resolved target must be under root (symlinks escaping the root
 * are refused). */
static int append_file(struct jc_app *app, const char *path, struct jc_sb *sb,
                       const char *root)
{
    char *data = NULL;
    jc_size len = 0;
    char resolved[JC_PATH_MAX];
    struct stat st;

    /* M1108 (D-33): a non-regular file here -- a FIFO/device/socket would
     * block jc_read_file's fopen until the run's deadline; a directory is
     * skipped the same way without ever being read (M1108-b: the WARN says
     * so, true for every non-regular shape). This is the single choke
     * point for BOTH
     * glossary routes: the project .jichi/glossary.md and the global
     * ~/.config/jichi/glossary.md. Following stat (not lstat), so a symlink
     * that resolves to a regular file still reads (matching the M992/M1020
     * rules semantics); a missing or broken link gives stat != 0 and falls
     * through to jc_read_file, which returns NOTFOUND and the skip below
     * stays silent -- behaviour unchanged. The root/symlink-escape guard
     * below is untouched. */
    if (stat(path, &st) == 0 && !S_ISREG(st.st_mode)) {
        jc_logf(JC_LOG_WARN, "glossary: skipping %s: not a regular file -- skipped without reading", path);
        return 0;
    }

    /* Guard: fail closed -- refuse if the resolved target is outside the
     * root OR if the path cannot be resolved at all (a >40-hop symlink
     * chain or an unreadable link cannot prove containment, so skip it,
     * as the rules guard does). */
    if (root != NULL) {
        if (jc_path_resolve(path, resolved, sizeof(resolved)) != JC_OK ||
            !jc_path_under_root(root, resolved)) {
            jc_logf(JC_LOG_WARN, "glossary: skipping %s: resolved target outside the workspace (or unresolvable)", path);
            return 0;
        }
    }

    /* M1109 (D-35): read at most JC_GLOSSARY_MAX TRAILING bytes -- the cut is
     * at the combined-buffer level (jc_glossary_load keeps the TAIL of
     * global+project), and the last JC_GLOSSARY_MAX bytes of the combined
     * buffer are the same as from the whole files. Pre-fix the whole file
     * (up to the 64 MB JC_READ_FILE_MAX) sat on the session arena. */
    if (jc_read_file_bounded(path, &data, &len, app->arena,
                             JC_GLOSSARY_MAX, 0) != JC_OK || len == 0) {
        return 0;
    }
    if (sb->len > 0) {
        jc_sb_append(sb, "\n\n");
    }
    jc_sb_append_n(sb, data, len);
    return 1;
}

char *jc_glossary_load(struct jc_app *app)
{
    char path[1200];
    char wsroot[JC_PATH_MAX];
    struct jc_sb sb;
    char *result = NULL;
    const char *text;

    /* Compute the canonical workspace root for the project glossary guard.
     * For the project glossary path, the root is the realpath of app->cwd.
     * Global glossary in $HOME/.config/jichi/ is EXEMPT (user's own config). */
    if (jc_path_resolve(app->cwd, wsroot, sizeof(wsroot)) != JC_OK) {
        wsroot[0] = '\0'; /* fail closed */
    }

    jc_sb_init(&sb);

    /* Global first (house-wide terms), then project (which may override/extend
     * in prose). */
    jc_snprintf(path, sizeof(path), "%s/.config/jichi/glossary.md",
                jc_home_dir());
    /* Global glossary is exempt from the root check (user's own config). */
    append_file(app, path, &sb, NULL);
    jc_snprintf(path, sizeof(path), "%s/.jichi/glossary.md", app->cwd);
    /* Project glossary is guarded. */
    append_file(app, path, &sb, wsroot);

    text = (sb.data != NULL) ? sb.data : "";
    /* Skip leading whitespace so an all-blank file reads as empty. */
    while (*text == ' ' || *text == '\n' || *text == '\r' || *text == '\t') {
        text++;
    }
    if (text[0] != '\0') {
        jc_size len = (jc_size)strlen(text);
        if (len > JC_GLOSSARY_MAX) {
            /* Keep the tail, starting on a character boundary (M191). */
            text = text + jc_utf8_resync(text, len, len - JC_GLOSSARY_MAX);
        }
        result = jc_arena_strdup(app->arena, text);
    }
    jc_sb_free(&sb);
    return result;
}
