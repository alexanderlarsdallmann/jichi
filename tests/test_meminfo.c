/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_meminfo.c - self-RSS status parsing (M180). */

#include "jc_test.h"
#include "jc_meminfo.h"

static const char *STATUS =
    "Name:\tjichi\n"
    "Umask:\t0022\n"
    "State:\tR (running)\n"
    "VmPeak:\t   20000 kB\n"
    "VmSize:\t   19000 kB\n"
    "VmHWM:\t   12288 kB\n"
    "VmRSS:\t   10240 kB\n"
    "Threads:\t1\n";

static void test_parse(void)
{
    long rss = -1;
    long hwm = -1;

    JC_CHECK(jc_meminfo_parse(STATUS, &rss, &hwm) == 1);
    JC_CHECK(rss == 10240);
    JC_CHECK(hwm == 12288);

    /* Either out pointer may be NULL. */
    rss = -1;
    JC_CHECK(jc_meminfo_parse(STATUS, &rss, NULL) == 1);
    JC_CHECK(rss == 10240);

    /* A key must match at line start: "VmRSS" inside another token or a
     * text without the field reports not-found, values zeroed. */
    rss = -1;
    hwm = -1;
    JC_CHECK(jc_meminfo_parse("Name:\tx\nXVmRSS:\t5 kB\n", &rss, &hwm) == 0);
    JC_CHECK(rss == 0 && hwm == 0);
    JC_CHECK(jc_meminfo_parse(NULL, &rss, &hwm) == 0);
}

static void test_self(void)
{
    long rss = 0;
    long hwm = 0;

    /* On Linux (the only supported platform) /proc/self/status exists and a
     * running test binary is certainly resident. Tolerate absence (rc 0)
     * for exotic build hosts, but when present the numbers must be sane. */
    if (jc_meminfo_self(&rss, &hwm)) {
        JC_CHECK(rss > 0);
        /* hwm == 0 is jc_meminfo_parse's documented "VmHWM absent" sentinel: it
         * zeroes the out-param and fills it only `if (hwm_found)`. Cygwin's
         * /proc/self/status carries no VmHWM line, so a bare `hwm >= rss`
         * asserted something the module never promised, and failed there (M477).
         * Where a high-water mark IS reported it must still be >= current RSS. */
        JC_CHECK(hwm == 0 || hwm >= rss);
    }
}

void test_meminfo(void)
{
    test_parse();
    test_self();
}
