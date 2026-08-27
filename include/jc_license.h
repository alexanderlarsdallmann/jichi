/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_license.h - THE copyright holder and licence of jichi (M497). Single source
 * of truth, in the shape jc_version.h established: `--version`, `describe
 * --output json` and tests/smoke/license_lint.sh all read these, and nothing
 * else defines them.
 *
 * BOTH ARE DECIDED (2026-08-27, the institutional answer): the licence is
 * Apache-2.0, and the copyright is held by Justus-Liebig-Universität Gießen
 * with Alexander-Lars Dallmann named as the author. This supersedes the interim
 * note of 2026-08-20 (holder = the author), and it is the outcome
 * docs/LICENSING.md anticipated: under § 69b UrhG the economic rights in
 * software written in the course of employment are exercised by the employer,
 * while authorship -- and the author's name -- remain with the author. Hence
 * TWO lines below, not one: the holder line says who grants the licence, the
 * author line says who wrote it.
 *
 * The sweep that applied this is scripts/set-license.sh (identifier) plus the
 * M619 holder/author pass; tests/smoke/license_lint.sh pins every file, the
 * --version output and `describe` against these defines, so the three cannot
 * drift apart.
 *
 * A copyright NOTICE is not what creates the copyright -- that exists from
 * authorship. The notice states the holder so a reader knows who to ask, which
 * matters most in the one file a stranger opens first and least in the 310th. */
#ifndef JC_LICENSE_H
#define JC_LICENSE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Final (2026-08-27). Swept across the tree; license_lint pins every file,
 * line 2 of every header, against this. */
#define JC_COPYRIGHT "Copyright (c) 2026 Justus-Liebig-Universität Gießen"

/* Final (2026-08-27). Line 3 of every header; § 69b UrhG keeps authorship with
 * the author while the employer exercises the economic rights. */
#define JC_AUTHOR "Author: Alexander-Lars Dallmann"

/* Final (2026-08-27): applied by scripts/set-license.sh; LICENSE at the root
 * is the verbatim text, NOTICE travels with redistributions (section 4(d)). */
#define JC_LICENSE_SPDX "Apache-2.0"

#ifdef __cplusplus
}
#endif
#endif /* JC_LICENSE_H */
