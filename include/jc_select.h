/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_select.h - select() and fd_set, wherever this libc declares them (M834).
 *
 * POSIX 2001 gave select() its own header, <sys/select.h>. Older systems declare
 * it elsewhere, and DJGPP is one: select() is in <time.h> and fd_set in
 * <sys/types.h>, with no <sys/select.h> at all -- the DJGPP step 0 found ten files
 * failing on the missing header (DEFERRED def-266). The Makefile asks
 * (HAVE_SYS_SELECT_H) and defines JC_NO_SYS_SELECT_H where the header is absent.
 *
 * <sys/time.h> is included unconditionally, for struct timeval: MiNTLib's
 * <sys/select.h> only forward-declares it (M723), so a caller that includes this
 * header can declare a timeval on every row. Include this, never <sys/select.h>
 * directly -- tests/smoke/posix_utils_lint.sh fails the build on a direct include
 * under src/ or in the unit tests, so a new file cannot bring the gap back. */
#ifndef JC_SELECT_H
#define JC_SELECT_H

#include <sys/types.h>
#include <sys/time.h>
#ifdef JC_NO_SYS_SELECT_H
#include <time.h>
#else
#include <sys/select.h>
#endif

#endif /* JC_SELECT_H */
