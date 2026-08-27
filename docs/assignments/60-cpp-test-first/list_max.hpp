#ifndef LIST_MAX_HPP
#define LIST_MAX_HPP
#include <vector>

// The largest element of a non-empty vector. It looks right and passes on the
// vectors people usually try. It is wrong -- the same bug the C course's
// stats_max had, in a functional coat.
inline long list_max(const std::vector<long>& xs)
{
    long m = 0; // seeds at 0
    for (long x : xs) {
        if (x > m) m = x;
    }
    return m;
}

#endif
