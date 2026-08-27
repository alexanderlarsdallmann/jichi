/* rot13.h */
#ifndef ROT13_H
#define ROT13_H

/* 'a'-'z' and 'A'-'Z' rotate by 13 within their own case; all other
 * values are returned unchanged. */
int rot13_char(int c);

#endif /* ROT13_H */
