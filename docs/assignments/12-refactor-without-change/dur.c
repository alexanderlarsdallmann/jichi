/* dur.c - see dur.h. The tests are green; the code still smells. */
#include "dur.h"

int hms_to_seconds(int h, int m, int s)
{
    if (h < 0 || h > 23) {
        return -1;
    }
    if (m < 0 || m > 59) {
        return -1;
    }
    if (s < 0 || s > 59) {
        return -1;
    }
    return h * 3600 + m * 60 + s;
}

int seconds_remaining(int h, int m, int s)
{
    if (h < 0 || h > 23) {
        return -1;
    }
    if (m < 0 || m > 59) {
        return -1;
    }
    if (s < 0 || s > 59) {
        return -1;
    }
    return 86400 - (h * 3600 + m * 60 + s);
}

int days_to_seconds(int d)
{
    if (d < 0) {
        return -1;
    }
    return d * 86400;
}
