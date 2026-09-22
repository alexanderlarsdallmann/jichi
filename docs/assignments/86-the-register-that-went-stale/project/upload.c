/* upload.c -- send one record to the collector. */
#include <stdio.h>
#include <string.h>

#define MAX_ATTEMPTS 3

/* The sampler hands us records at a fixed sample rate; this is the rate the
 * device was configured with, not anything we control or limit here. */
static int sample_rate_hz = 50;

int send_once(const char *rec);

int upload_record(const char *rec)
{
    int attempt;
    for (attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        if (send_once(rec) == 0) {
            return 0;
        }
        fprintf(stderr, "upload: attempt %d failed, retrying\n", attempt + 1);
    }
    return -1;
}

int sampler_rate(void)
{
    return sample_rate_hz;
}
