/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_fcntl.h - <fcntl.h>, plus the one flag jichi adds to every open() of a
 * file (M837).
 *
 * DOS (DJGPP) and Windows' C libraries open a file in TEXT mode unless told
 * otherwise: a write turns "\n" into "\r\n", and a read turns "\r\n" back into
 * "\n" and stops at the first Ctrl-Z byte. POSIX has no text mode and no
 * O_BINARY, which is why no Linux, BSD, illumos or macOS run could ever show the
 * difference. jichi's files are data -- JSON, logs, the index's floats, files a
 * model edits -- and it always wants back the bytes it wrote, so every open() of
 * a file carries JC_O_BINARY and every fopen() mode carries "b".
 * tests/smoke/portability_lint.sh checks 37 and 38 hold both rules. */
#ifndef JC_FCNTL_H
#define JC_FCNTL_H

#include <fcntl.h>

#ifdef O_BINARY
#define JC_O_BINARY O_BINARY
#else
#define JC_O_BINARY 0
#endif

#endif /* JC_FCNTL_H */
