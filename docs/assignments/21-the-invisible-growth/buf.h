/* buf.h - a growable byte buffer, with a footprint gauge.
 *
 * buf_clear() empties the buffer but KEEPS its capacity -- the right call
 * for a warm buffer refilled to a similar size every time. Whether that is
 * the right call for YOUR buffer depends on the sizes that flow through it;
 * that question is the task.
 *
 * buf_live_bytes() reports the total capacity of every live buffer in the
 * process -- the gauge the selftest reads. It is bookkeeping inside this
 * module, not /proc magic: malloc and free update it.
 */
#ifndef BUF_H
#define BUF_H

#include <stddef.h>

struct buf {
    char  *data;
    size_t len;
    size_t cap;
};

void buf_init(struct buf *b);
void buf_free(struct buf *b);
int  buf_append(struct buf *b, const char *bytes, size_t n);
void buf_clear(struct buf *b);           /* len = 0, capacity kept */

size_t buf_live_bytes(void);             /* sum of live capacities */

#endif
