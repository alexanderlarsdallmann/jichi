/* alpha.c - startup wiring. */

struct widget;
extern int widget_init(struct widget *w); /* declared here, defined elsewhere */

int start_all(struct widget *w)
{
    return widget_init(w);
}
