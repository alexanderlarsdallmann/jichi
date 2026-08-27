/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_pdf.c - PDF text extraction via an external tool (see jc_pdf.h). */

#include "jc_pdf.h"
#include "jc_str.h"
#include "jc_proc.h"

#include <string.h>

#define JC_PDF_TIMEOUT 30 /* seconds */

int jc_pdf_is_pdf(const char *path)
{
    jc_size n;
    if (path == NULL) {
        return 0;
    }
    n = (jc_size)strlen(path);
    if (n < 4 || path[n - 4] != '.') {
        return 0;
    }
    return (path[n - 3] == 'p' || path[n - 3] == 'P') &&
           (path[n - 2] == 'd' || path[n - 2] == 'D') &&
           (path[n - 1] == 'f' || path[n - 1] == 'F');
}

const char *jc_pdf_command(const char *configured)
{
    return (configured != NULL && configured[0] != '\0') ? configured
                                                          : "pdftotext";
}

jc_status jc_pdf_extract(const char *path, const char *command, jc_size cap,
                         struct jc_sb *out, volatile int *abort)
{
    char *argv[4];
    const char *cmd = (command != NULL && command[0] != '\0') ? command
                                                              : "pdftotext";
    int rc;

    if (path == NULL || out == NULL) {
        return JC_ERR_INVALID;
    }
    argv[0] = (char *)cmd;
    argv[1] = (char *)path;
    argv[2] = (char *)"-"; /* poppler: write extracted text to stdout */
    argv[3] = NULL;

    rc = jc_proc_capture(argv, NULL, NULL, out, cap, JC_PDF_TIMEOUT, abort);
    if (rc == -1 || rc == 127) {
        return JC_ERR_NOTFOUND; /* fork/exec failed: extractor not installed */
    }
    if (rc == -2) {
        return JC_ERR_TIMEOUT;
    }
    if (rc != 0) {
        return JC_ERR_PROVIDER;
    }
    return JC_OK;
}
