/* candidate A */
#include "median3.h"

int median3(int a, int b, int c)
{
    if ((a >= b && a <= c) || (a <= b && a >= c)) {
        return a;
    }
    if ((b >= a && b <= c) || (b <= a && b >= c)) {
        return b;
    }
    return c;
}
