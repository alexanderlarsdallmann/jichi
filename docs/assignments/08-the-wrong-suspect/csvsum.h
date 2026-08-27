/* csvsum.h - split a comma-separated line of ints and total them. */
#ifndef CSVSUM_H
#define CSVSUM_H

/* Parse up to max ints out of line (comma-separated, no spaces). Returns the
 * number of fields stored into out. */
int split_fields(const char *line, int *out, int max);

/* Sum of v[0..n-1]. */
int total(const int *v, int n);

#endif /* CSVSUM_H */
