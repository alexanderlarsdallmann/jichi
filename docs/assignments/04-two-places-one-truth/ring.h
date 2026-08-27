/* ring.h - a fixed-capacity ring buffer of ints. */
#ifndef RING_H
#define RING_H

#define RING_CAP 64

struct ring {
    int items[RING_CAP];
    int head;
    int tail;
};

void ring_init(struct ring *r);
int ring_push(struct ring *r, int v);
int ring_pop(struct ring *r, int *out);

#endif /* RING_H */
