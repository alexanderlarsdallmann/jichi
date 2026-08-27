#ifndef IVEC_H
#define IVEC_H
#include <stddef.h>

/* A growable array of ints. `len` is how many are stored; `cap` is how many
   the current `data` block can hold. ivec_push must grow `data` when len == cap
   instead of writing past it. */
typedef struct {
    int *data;
    size_t len;
    size_t cap;
} ivec;

void ivec_init(ivec *v);
void ivec_push(ivec *v, int x);
int  ivec_get(const ivec *v, size_t i);
void ivec_free(ivec *v);

#endif
