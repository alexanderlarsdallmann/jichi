/* stats.h - tiny integer statistics helpers. */
#ifndef STATS_H
#define STATS_H

/* Largest value in v[0..n-1]. n must be >= 1. */
int stats_max(const int *v, int n);

/* Sum of v[0..n-1]. */
int stats_sum(const int *v, int n);

#endif /* STATS_H */
