/* main.c - the wordtool CLI: statistics for one line of text. */
#include "wordtool.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    const char *text = (argc > 1) ? argv[1] : "";
    printf("words=%ld longest=%ld\n",
           wt_count_words(text), wt_longest_word(text));
    return 0;
}
