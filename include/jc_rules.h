/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_rules.h - discover and concatenate instruction files (AGENTS.md etc.).
 *
 * Builds the "project rules" block that jc_sysmsg_build injects into the system
 * prompt: a global file, every AGENTS.md (CLAUDE.md fallback) from the git root
 * down to the working directory, and any explicit config "instructions" paths.
 */
#ifndef JC_RULES_H
#define JC_RULES_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

struct jc_app; /* jc_app.h */

/* Returns an arena-owned concatenation of the discovered instruction files, or
 * NULL if none were found. Bounded in size. */
char *jc_rules_load(struct jc_app *app);

#ifdef __cplusplus
}
#endif
#endif /* JC_RULES_H */
