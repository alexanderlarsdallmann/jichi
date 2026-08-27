/* track.h - malloc/free with a live-bytes and PEAK gauge.
 *
 * The candidates allocate only through these, so a checker can read what a
 * leak checker cannot: track_live() is what is allocated RIGHT NOW (a leak
 * checker only sees what is still allocated at exit), and track_peak() is
 * the high-water -- the largest live total the process ever reached. The
 * peak is the number that catches a "borrows the whole file, gives it back
 * politely" implementation, which every start-vs-end measurement calls
 * clean.
 */
#ifndef TRACK_H
#define TRACK_H

#include <stddef.h>

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t old_n, size_t new_n);
void  xfree(void *p, size_t n);

size_t track_live(void);   /* bytes allocated and not yet freed */
size_t track_peak(void);   /* the largest track_live() ever seen */

#endif
