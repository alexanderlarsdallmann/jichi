/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_pdf.h - PDF text extraction (M42).
 *
 * jichi reads source/text directly, but PDFs (specs, papers, assignments) are
 * binary. Rather than vendor a PDF parser (PDF is far too gnarly for a
 * maintainable C89 one), we shell out to an external extractor -- `pdftotext`
 * (poppler-utils) by default -- via jc_proc_capture. The read_file tool and the
 * @path reference detect a `.pdf` and route through here; a missing extractor
 * is reported as an actionable error rather than dumping binary.
 */
#ifndef JC_PDF_H
#define JC_PDF_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

struct jc_sb; /* jc_str.h */

/* True if `path` ends in ".pdf" (case-insensitive). */
int jc_pdf_is_pdf(const char *path);

/* The effective extractor command: `configured` (config `pdfCommand`) when set,
 * else "pdftotext". Never NULL — handy for passing as jc_index_build's pdf_cmd
 * (where non-NULL also *enables* PDF indexing). Pure. */
const char *jc_pdf_command(const char *configured);

/* Extract `path`'s text by running `command` (NULL/empty => "pdftotext") as
 * `<command> <path> -` (text to stdout), appending it to `out` bounded by `cap`
 * (jc_proc_capture adds a truncation note when the cap is hit). `abort` (may be
 * NULL) cancels. Returns JC_OK; JC_ERR_NOTFOUND when the extractor isn't
 * installed / can't be spawned; JC_ERR_TIMEOUT on timeout/abort; JC_ERR_PROVIDER
 * on any other non-zero exit (e.g. a corrupt PDF). */
jc_status jc_pdf_extract(const char *path, const char *command, jc_size cap,
                         struct jc_sb *out, volatile int *abort);

#ifdef __cplusplus
}
#endif
#endif /* JC_PDF_H */
