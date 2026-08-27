/* clamp.c - see clamp.h. */
#include "clamp.h"

int clamp(int x, int lo, int hi)
{
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return lo;
    }
    return x;
}
