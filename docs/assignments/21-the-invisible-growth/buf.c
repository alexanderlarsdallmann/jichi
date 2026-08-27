/* buf.c - see buf.h. */
#include "buf.h"

#include <stdlib.h>
#include <string.h>

static size_t live_bytes = 0;

void buf_init(struct buf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void buf_free(struct buf *b)
{
    live_bytes -= b->cap;
    free(b->data);
    buf_init(b);
}

int buf_append(struct buf *b, const char *bytes, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t want = (b->cap == 0) ? 64 : b->cap;
        char *nd;
        while (want < b->len + n + 1) {
            want *= 2;
        }
        nd = (char *)realloc(b->data, want);
        if (nd == NULL) {
            return -1;
        }
        live_bytes += want - b->cap;
        b->data = nd;
        b->cap = want;
    }
    memcpy(b->data + b->len, bytes, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

void buf_clear(struct buf *b)
{
    b->len = 0;
    if (b->data != NULL) {
        b->data[0] = '\0';
    }
}

size_t buf_live_bytes(void)
{
    return live_bytes;
}
