/* candidate C */
#include "median3.h"

int median3(int a, int b, int c)
{
    if (a == b) { return c; }  /* two agree: the third breaks the tie */
    if (b == c) { return a; }
    if (a == c) { return b; }
    if ((a > b) != (a > c)) { return a; }
    if ((b > a) != (b > c)) { return b; }
    return c;
}
