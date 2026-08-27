#ifndef CLAMP_HPP
#define CLAMP_HPP

// Clamp x into the inclusive range [lo, hi].
inline long clamp(long x, long lo, long hi)
{
    if (x < lo) return lo;
    if (x > hi) return x; // <-- one of these lines is wrong
    return x;
}

#endif
