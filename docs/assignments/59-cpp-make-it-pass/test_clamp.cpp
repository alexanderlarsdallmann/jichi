// The truth for this task -- do NOT edit it; fix clamp.hpp until it is green.
// No test framework is assumed (gtest/Catch2 need a package manager); this is a
// tiny base-only harness: <cassert> aborts on a false check, which the grader
// reads as a nonzero exit. The whole suite is compiled with AddressSanitizer.
#include "clamp.hpp"
#include <cassert>

int main()
{
    assert(clamp(9, 0, 5) == 5);    // above hi clamps to hi
    assert(clamp(-3, 0, 5) == 0);   // below lo clamps to lo
    assert(clamp(2, 0, 5) == 2);    // in range passes through
    return 0;
}
