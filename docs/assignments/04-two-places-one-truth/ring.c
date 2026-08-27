/* ring.c - see ring.h. The capacity comes from the header; nothing here
 * hardcodes it. */
#include "ring.h"

void ring_init(struct ring *r)
{
    r->head = 0;
    r->tail = 0;
}

int ring_push(struct ring *r, int v)
{
    int next = (r->head + 1) % RING_CAP;
    if (next == r->tail) {
        return -1; /* full */
    }
    r->items[r->head] = v;
    r->head = next;
    return 0;
}

int ring_pop(struct ring *r, int *out)
{
    if (r->tail == r->head) {
        return -1; /* empty */
    }
    *out = r->items[r->tail];
    r->tail = (r->tail + 1) % RING_CAP;
    return 0;
}
