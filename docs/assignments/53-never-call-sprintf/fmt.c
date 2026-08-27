#include "fmt.h"
#include <stdio.h>

/* It works when `out` is big enough -- and it is a classic buffer overflow.
   sprintf takes no size, so it happily writes past a small `out`. `cap` is
   even passed in and ignored. This is exactly the write jichi's house rule
   forbids ("never call sprintf", CLAUDE.md). Fix it to honour `cap`. */
void greet(char *out, size_t cap, const char *name)
{
    (void)cap;
    sprintf(out, "Hello, %s!", name);
}
