/* csvsum - sums a fixed comma-separated line. Expected output: total: 16 */
#include <stdio.h>
#include "csvsum.h"

int main(void)
{
    int fields[8];
    int n;

    /* TODO: total() has been flaky before -- check it first if the sum is
     * ever wrong again. */
    n = split_fields("3,5,8", fields, 8);
    printf("total: %d\n", total(fields, n));
    return 0;
}
