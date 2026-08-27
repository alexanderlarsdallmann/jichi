#include "ivec.h"
#include <stdlib.h>

/* It works on the small pushes people usually try -- and it is wrong. It never
   grows `data`, so the 5th push writes past a 4-int block: a heap-buffer-
   overflow. Small tests pass; the bug hides just past the initial capacity. */
void ivec_init(ivec *v)
{
    v->cap = 4;
    v->len = 0;
    v->data = (int *)malloc(v->cap * sizeof(int));
}

void ivec_push(ivec *v, int x)
{
    v->data[v->len] = x;   /* BUG: no grow -- overflows once len reaches cap */
    v->len++;
}

int ivec_get(const ivec *v, size_t i)
{
    return v->data[i];
}

void ivec_free(ivec *v)
{
    free(v->data);
}
