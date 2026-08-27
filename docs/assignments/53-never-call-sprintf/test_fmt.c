/* The suite -- do NOT edit it. Keep it green while changing HOW, not WHAT. */
#include "fmt.h"
#include <string.h>
#include <assert.h>

int main(void)
{
    char big[64];
    char small[8];

    /* When it fits, the greeting is exact. */
    greet(big, sizeof big, "world");
    assert(strcmp(big, "Hello, world!") == 0);

    /* When it does not fit, it must TRUNCATE, never overflow `small`.
       "Hello, Alexandra!" is 17 bytes; small holds 8. Under ASan the pristine
       sprintf overflows here; a bounded write stays inside small. */
    greet(small, sizeof small, "Alexandra");
    assert(strlen(small) < sizeof small);

    return 0;
}
