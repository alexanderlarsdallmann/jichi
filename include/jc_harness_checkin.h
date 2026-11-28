/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_checkin.h - the checkin verb (M1224, H13, the C89 port of checkin.sh). */

#ifndef JC_HARNESS_CHECKIN_H
#define JC_HARNESS_CHECKIN_H

#include "jc_harness_config.h"  /* struct jc_harness_config */
#include "jc_mem.h"             /* struct jc_arena */

#ifdef __cplusplus
extern "C" {
#endif

/* The checkin verb entry point. Parses the command line (with --config required),
 * loads the config, and runs the checkin protocol (37 cells, the C-rows of the
 * driver's B-list).
 *
 * Returns 0 on success, 1 on refusal (e.g. missing loop unit), 2 on usage error. */
int jc_harness_checkin(int argc, char *const *argv, struct jc_harness_config *cfg,
                       struct jc_arena *a);

#ifdef __cplusplus
}
#endif

#endif /* JC_HARNESS_CHECKIN_H */
