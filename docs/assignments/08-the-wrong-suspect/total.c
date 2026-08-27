/* total.c - the summing stage of csvsum. */
#include "csvsum.h"

int total(const int *v, int n)
{
    int s = 0;
    while (n-- > 0) {
        s += v[n];
    }
    return s;
}
