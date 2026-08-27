#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Return a freshly malloc'd copy of s with a-z uppercased.
   The CALLER owns the result and frees it (see main). */
static char *shout(const char *s)
{
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    size_t i;
    for (i = 0; i < n; i++)
        out[i] = (s[i] >= 'a' && s[i] <= 'z') ? (char)(s[i] - 32) : s[i];
    out[n] = '\0';
    free(out);              /* <-- one line here is the bug */
    return out;
}

int main(void)
{
    char *r = shout("hello");
    printf("%s\n", r);      /* should print HELLO */
    free(r);
    return 0;
}
