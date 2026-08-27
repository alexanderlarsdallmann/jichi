/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_memtrim.c - return freed heap to the OS (M218). See jc_memtrim.h. */
#include "jc_memtrim.h"

#ifdef JC_HAVE_MALLOC_TRIM

#include <malloc.h>

void jc_mem_tune(void)
{
    /* Pin the mmap threshold: >=128 KB allocations (request bodies, big tool
     * reads) go to mmap and are munmap'd on free, instead of teaching malloc
     * an ever-larger threshold and then never shrinking brk. 128 KB is
     * glibc's own default STARTING threshold -- we only disable the ratchet,
     * we don't invent a policy. */
    mallopt(M_MMAP_THRESHOLD, 128 * 1024);
    /* Trim brk back eagerly once 256 KB of top-of-heap is free. */
    mallopt(M_TRIM_THRESHOLD, 256 * 1024);
}

int jc_mem_release_os(void)
{
    return malloc_trim(0) == 1 ? 1 : 0;
}

#else /* !JC_HAVE_MALLOC_TRIM */

void jc_mem_tune(void)
{
}

int jc_mem_release_os(void)
{
    return 0;
}

#endif
