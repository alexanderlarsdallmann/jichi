/* spooler.c - a message spooler whose memory never leaks and grows anyway.
 *
 * The spooler formats messages through ONE reusable buffer (a warm buffer is
 * the right design for a stream of similar messages). The traffic, though,
 * is not uniform: one early message is ~512 KB, the rest are tiny. After the
 * batch the spooler idles -- and the selftest reads the module's footprint
 * gauge while idle. Every byte is reachable, every free will happen at exit,
 * a leak checker reports ZERO. The footprint is still wrong.
 */
#include "buf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Format message i into b: "msg <i>: <payload>\n", uppercased payload. */
static int spool_one(struct buf *b, int i, const char *payload)
{
    char head[32];
    buf_clear(b);
    sprintf(head, "msg %d: ", i);
    if (buf_append(b, head, strlen(head)) != 0 ||
        buf_append(b, payload, strlen(payload)) != 0 ||
        buf_append(b, "\n", 1) != 0) {
        return -1;
    }
    return 0;
}

int main(void)
{
    struct buf spool;
    char *big;
    unsigned long checksum = 0;
    int i;

    buf_init(&spool);
    big = (char *)malloc(512 * 1024 + 1);
    if (big == NULL) {
        return 1;
    }
    memset(big, 'x', 512 * 1024);
    big[512 * 1024] = '\0';

    for (i = 0; i < 200; i++) {
        const char *payload = (i == 3) ? big : "a small routine message";
        size_t j;
        if (spool_one(&spool, i, payload) != 0) {
            free(big);
            return 1;
        }
        for (j = 0; j < spool.len; j++) {
            checksum = (checksum * 31 + (unsigned char)spool.data[j])
                       & 0xffffffffUL; /* 32-bit portable */
        }
    }
    free(big);

    /* Idle now: the batch is done, the spooler waits for the next one. */
    printf("checksum=%lu\n", checksum);
    printf("idle_live_bytes=%lu\n", (unsigned long)buf_live_bytes());
    buf_free(&spool);
    return 0;
}
