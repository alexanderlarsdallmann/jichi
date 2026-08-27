/* candidate D */
#include "median3.h"

int median3(int a, int b, int c)
{
    unsigned x = (unsigned)a;
    unsigned y = (unsigned)b;
    unsigned z = (unsigned)c;
    unsigned t;
    if (x > y) { t = x; x = y; y = t; }
    if (y > z) { t = y; y = z; z = t; }
    if (x > y) { t = x; x = y; y = t; }
    return (int)y;
}
