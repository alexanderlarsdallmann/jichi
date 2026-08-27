/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_md.h - split a markdown document into YAML frontmatter + body.
 *
 * A document that begins with a "---\n ... \n---\n" fence has the enclosed text
 * parsed as YAML frontmatter (via jc_yaml); everything after the closing fence
 * is the body. A document without a leading fence has front==NULL and body set
 * to the whole text. Used by the custom-command and agent-profile loaders.
 */
#ifndef JC_MD_H
#define JC_MD_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_yaml.h"

struct jc_md_doc {
    struct jc_yaml *front; /* parsed frontmatter MAP, or NULL                  */
    const char     *body;  /* points into `text` passed to jc_md_parse         */
};

/* Parse `text` (which must outlive *out, since body points into it). Always
 * returns JC_OK; a missing/empty/malformed fence yields front==NULL and body
 * set to the whole text (or the text after an unterminated fence is treated as
 * no frontmatter). */
jc_status jc_md_parse(const char *text, struct jc_arena *a,
                      struct jc_md_doc *out);

/* Release the frontmatter tree's heap backing (jc_yaml_free). Safe on NULL. */
void jc_md_free(struct jc_md_doc *doc);

/* True when `text` INTENDED frontmatter but never closed the fence: the first
 * non-blank line is a "---" fence yet no later line is one, so jc_md_parse
 * yielded front==NULL and swallowed the whole document as body. This is a common
 * authoring mistake (a model omits the closing "---") that otherwise fails
 * silently -- every frontmatter field is ignored. Tolerates trailing whitespace
 * on a fence line (more lenient than the parser's own splitter, deliberately, so
 * it flags near-misses). Pure; unit-tested. */
int jc_md_frontmatter_unterminated(const char *text);

#ifdef __cplusplus
}
#endif
#endif /* JC_MD_H */
