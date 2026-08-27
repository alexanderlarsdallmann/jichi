/* dur.h - day-time duration helpers.
 *
 * Times of day are h (0-23), m (0-59), s (0-59). Invalid components make
 * every function return -1.
 */
#ifndef DUR_H
#define DUR_H

/* Seconds since midnight for h:m:s, or -1 if any component is invalid. */
int hms_to_seconds(int h, int m, int s);

/* Seconds from h:m:s until the following midnight, or -1 if invalid. */
int seconds_remaining(int h, int m, int s);

/* Seconds in d whole days; d must be >= 0, else -1. */
int days_to_seconds(int d);

#endif /* DUR_H */
