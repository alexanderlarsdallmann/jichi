/* unify.c -- unification, the operation at the heart of every Prolog, in C89.
 *
 * Terms are variables, atoms, or compounds f(t1, ..., tn). Unifying two terms
 * finds bindings for the variables that make them identical, or fails.
 * A binding is recorded on a TRAIL so it can be undone: that is how a Prolog
 * backtracks (SWI-Prolog's trail lives in src/pl-incl.h and src/pl-wam.c).
 * The OCCURS CHECK refuses X = f(X); standard Prolog skips it for speed, and
 * this file lets you see what that costs.
 *
 * Build and run (from this directory):
 *   cc -std=c89 -pedantic -Wall -Wextra -Werror -o unify unify.c && ./unify
 */
#include <stdio.h>
#include <string.h>

enum kind { VAR, ATOM, COMP };

struct term {
    enum kind kind;
    const char *name;          /* atom or functor name; variable's print name */
    struct term *ref;          /* VAR: what it is bound to, or NULL */
    int arity;
    struct term *args[3];
};

#define TRAIL_MAX 64
static struct term *trail[TRAIL_MAX];
static int trail_top = 0;
static int occurs_check = 1;

static struct term *deref(struct term *t)
{
    while (t->kind == VAR && t->ref != NULL) {
        t = t->ref;
    }
    return t;
}

static int occurs(struct term *v, struct term *t)
{
    int i;
    t = deref(t);
    if (t == v) {
        return 1;
    }
    if (t->kind == COMP) {
        for (i = 0; i < t->arity; i++) {
            if (occurs(v, t->args[i])) {
                return 1;
            }
        }
    }
    return 0;
}

static int bind(struct term *v, struct term *t)
{
    if (occurs_check && occurs(v, t)) {
        return 0;                       /* X = f(X) has no finite solution */
    }
    if (trail_top == TRAIL_MAX) {
        return 0;                       /* refuse rather than overrun */
    }
    v->ref = t;
    trail[trail_top++] = v;             /* remember, so backtracking can undo */
    return 1;
}

static void undo_to(int mark)           /* backtracking: unbind back to a mark */
{
    while (trail_top > mark) {
        trail[--trail_top]->ref = NULL;
    }
}

static int unify(struct term *a, struct term *b)
{
    int i;
    a = deref(a);
    b = deref(b);
    if (a == b) {
        return 1;
    }
    if (a->kind == VAR) {
        return bind(a, b);
    }
    if (b->kind == VAR) {
        return bind(b, a);
    }
    if (a->kind == ATOM && b->kind == ATOM) {
        return strcmp(a->name, b->name) == 0;
    }
    if (a->kind == COMP && b->kind == COMP && a->arity == b->arity
        && strcmp(a->name, b->name) == 0) {
        for (i = 0; i < a->arity; i++) {
            if (!unify(a->args[i], b->args[i])) {
                return 0;
            }
        }
        return 1;
    }
    return 0;
}

static void print(struct term *t, int depth)
{
    int i;
    t = deref(t);
    if (depth > 6) {                    /* a cyclic term would print forever */
        fputs("...", stdout);
        return;
    }
    if (t->kind == COMP) {
        printf("%s(", t->name);
        for (i = 0; i < t->arity; i++) {
            if (i > 0) {
                fputs(", ", stdout);
            }
            print(t->args[i], depth + 1);
        }
        fputs(")", stdout);
    } else {
        fputs(t->name, stdout);         /* an atom, or an unbound variable */
    }
}

static struct term mk(enum kind k, const char *name, int arity,
                      struct term *a0, struct term *a1)
{
    struct term t;
    t.kind = k;
    t.name = name;
    t.ref = NULL;
    t.arity = arity;
    t.args[0] = a0;
    t.args[1] = a1;
    t.args[2] = NULL;
    return t;
}

static void try_it(const char *label, struct term *a, struct term *b)
{
    int mark = trail_top;
    int ok = unify(a, b);
    printf("%-34s ", label);
    if (ok) {
        fputs("yes: ", stdout);
        print(a, 0);
        printf("   (bindings made: %d)\n", trail_top - mark);
    } else {
        puts("no");
    }
    undo_to(mark);                      /* every try starts from a clean slate */
}

int main(void)
{
    struct term X = mk(VAR, "X", 0, NULL, NULL);
    struct term Y = mk(VAR, "Y", 0, NULL, NULL);
    struct term a = mk(ATOM, "a", 0, NULL, NULL);
    struct term b = mk(ATOM, "b", 0, NULL, NULL);
    struct term fXb = mk(COMP, "f", 2, &X, &b);
    struct term faY = mk(COMP, "f", 2, &a, &Y);
    struct term gXX = mk(COMP, "g", 2, &X, &X);
    struct term gab = mk(COMP, "g", 2, &a, &b);
    struct term fX  = mk(COMP, "f", 1, &X, NULL);

    try_it("f(X, b) = f(a, Y)", &fXb, &faY);
    try_it("g(X, X) = g(a, b)", &gXX, &gab);
    try_it("X = f(X), occurs check on", &X, &fX);
    occurs_check = 0;
    try_it("X = f(X), occurs check off", &X, &fX);
    return 0;
}
