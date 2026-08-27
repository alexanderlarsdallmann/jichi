#ifndef FMT_H
#define FMT_H
#include <stddef.h>

/* Write "Hello, <name>!" into out, which holds cap bytes (including the '\0').
   It must NEVER write past cap -- if the greeting does not fit, truncate. */
void greet(char *out, size_t cap, const char *name);

#endif
