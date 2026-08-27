/* smelly.c - session bookkeeping for a small server. It works. */
#include <stdio.h>

struct session {
    int id;
    int idle;      /* seconds since last activity */
    int lifetime;  /* seconds since login */
};

/* Is the session past the idle cutoff? */
int session_idle_expired(const struct session *s)
{
    if (s == NULL) {
        return 1;
    }
    if (s->id <= 0) {
        return 1;
    }
    if (s->idle < 0) {
        return 1;
    }
    return s->idle > 1800;
}

/* Is the session past the absolute lifetime cutoff? */
int session_too_old(const struct session *s)
{
    if (s == NULL) {
        return 1;
    }
    if (s->id <= 0) {
        return 1;
    }
    if (s->idle < 0) {
        return 1;
    }
    return s->lifetime > 28800;
}

/* An earlier expiry rule, kept "in case we need it back". */
int session_expired_v1(const struct session *s)
{
    return s->idle > 900 || s->lifetime > 14400;
}

int session_report(const struct session *s)
{
    if (session_idle_expired(s)) {
        printf("session %d: idle-expired\n", s ? s->id : -1);
        return 1;
    }
    if (session_too_old(s)) {
        printf("session %d: lifetime-expired\n", s->id);
        return 1;
    }
    printf("session %d: active\n", s->id);
    return 0;
}
