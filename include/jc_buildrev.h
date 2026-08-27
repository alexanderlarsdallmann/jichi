/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_buildrev.h -- the commit this binary was built from (M495). See the .c for
 * why this exists and why it is a commit rather than a timestamp. */
#ifndef JC_BUILDREV_H
#define JC_BUILDREV_H

#ifdef __cplusplus
extern "C" {
#endif

/* The short commit hash, suffixed "-dirty" when the tree had uncommitted changes
 * at build time. NULL when the build had no repository to ask (a tarball), so a
 * caller prints nothing rather than inventing a value. */
const char *jc_build_rev(void);

#ifdef __cplusplus
}
#endif
#endif /* JC_BUILDREV_H */
