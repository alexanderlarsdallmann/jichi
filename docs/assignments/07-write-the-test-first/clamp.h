/* clamp.h - bound a value to a closed range.
 *
 * clamp(x, lo, hi) returns:
 *   lo  when x < lo
 *   hi  when x > hi
 *   x   otherwise
 * Requires lo <= hi.
 */
#ifndef CLAMP_H
#define CLAMP_H

int clamp(int x, int lo, int hi);

#endif /* CLAMP_H */
