/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_glossary.h - project/global glossary of domain terms (M35c).
 *
 * A glossary is plain markdown the user maintains -- a list of domain terms and
 * their definitions -- that is injected into the system prompt so the agent
 * speaks the project's vocabulary. It complements rules (how to behave) and
 * memory (durable notes): the glossary is *reference*, not instruction.
 *
 * Discovered at ~/.config/jichi/glossary.md (global, house-wide terms)
 * and <cwd>/.jichi/glossary.md (project terms); both are concatenated, global
 * first. Read-only -- the user edits the files directly. See docs/GLOSSARY.md.
 */
#ifndef JC_GLOSSARY_H
#define JC_GLOSSARY_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

struct jc_app;

/* Cap on the injected glossary text (tail-trimmed when over). */
#define JC_GLOSSARY_MAX (8 * 1024)

/* Load the global + project glossary files into one arena-owned string (global
 * first, separated by a blank line), bounded to JC_GLOSSARY_MAX. Returns NULL
 * when neither file exists / both are empty. */
char *jc_glossary_load(struct jc_app *app);

#ifdef __cplusplus
}
#endif
#endif /* JC_GLOSSARY_H */
