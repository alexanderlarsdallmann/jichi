/* stats.c - see stats.h. */
#include "stats.h"

int stats_max(const int *v, int n)
{
    int best = 0;
    int i;
    for (i = 0; i < n; i++) {
        if (v[i] > best) {
            best = v[i];
        }
    }
    return best;
}

int stats_sum(const int *v, int n)
{
    int s = 0;
    int i;
    for (i = 0; i < n; i++) {
        s += v[i];
    }
    return s;
}
