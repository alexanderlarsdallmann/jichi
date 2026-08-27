/* beta.c - widget lifecycle. */

struct widget {
    int ready;
};

int widget_init(struct widget *w)
{
    if (w == 0) {
        return -1;
    }
    w->ready = 1;
    return 0;
}
