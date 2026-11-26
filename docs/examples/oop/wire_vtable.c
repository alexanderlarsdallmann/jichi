/* wire_vtable.c -- objects in C89, in the shape jichi's providers use.
 *
 * An "object" here is a struct whose first member points at a table of
 * functions (the vtable), plus state the caller never looks inside.
 * The caller holds `struct wire *` and calls through the table: it never asks
 * which kind of wire it has. That is the whole of dynamic dispatch.
 *
 * Build and run (from this directory), with the flags jichi itself uses:
 *   cc -std=c89 -pedantic -Wall -Wextra -Werror -o wire_vtable wire_vtable.c && ./wire_vtable
 * It also compiles as C++, as jichi's tree does (docs/CPP_BUILD.md):
 *   g++ -x c++ -pedantic -Wall -Wextra -Werror -o wire_vtable_cpp wire_vtable.c && ./wire_vtable_cpp
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct wire;                       /* the object; its layout is this file's */

struct wire_vtable {               /* the interface: what every wire can do */
    int  (*build)(struct wire *self, const char *prompt, char *out, size_t cap);
    const char *(*name)(const struct wire *self);
    void (*destroy)(struct wire *self);
};

struct wire {
    const struct wire_vtable *vt;  /* the dispatch table */
    char model[32];                /* state every wire shares */
    int  calls;                    /* state the implementations update */
};

/* Append `s` at out[*len], never past cap-1; 0 on success, -1 if it would
 * not fit. No sprintf, strcpy or strcat: jichi forbids the unbounded family
 * (tests/smoke/sprintf_lint.sh), so the example does too. */
static int put(char *out, size_t cap, size_t *len, const char *s)
{
    size_t n = strlen(s);
    if (*len + n + 1 > cap) {
        return -1;
    }
    memcpy(out + *len, s, n);
    *len += n;
    out[*len] = '\0';
    return 0;
}

/* ---- implementation 1: an "openai-like" body ------------------------- */

static int openai_build(struct wire *self, const char *prompt, char *out,
                        size_t cap)
{
    size_t len = 0;
    self->calls++;
    if (put(out, cap, &len, "{\"model\":\"") || put(out, cap, &len, self->model)
        || put(out, cap, &len, "\",\"messages\":[{\"role\":\"user\",\"content\":\"")
        || put(out, cap, &len, prompt) || put(out, cap, &len, "\"}]}")) {
        return -1;
    }
    return (int)len;
}

static const char *openai_name(const struct wire *self)
{
    (void)self;
    return "openai";
}

static void common_destroy(struct wire *self)
{
    free(self);
}

static const struct wire_vtable OPENAI_VT = {
    openai_build, openai_name, common_destroy
};

/* ---- implementation 2: an "anthropic-like" body ---------------------- */

static int anthropic_build(struct wire *self, const char *prompt, char *out,
                           size_t cap)
{
    size_t len = 0;
    self->calls++;
    if (put(out, cap, &len, "{\"model\":\"") || put(out, cap, &len, self->model)
        || put(out, cap, &len, "\",\"max_tokens\":64,\"messages\":[{\"role\":\"user\",\"content\":\"")
        || put(out, cap, &len, prompt) || put(out, cap, &len, "\"}]}")) {
        return -1;
    }
    return (int)len;
}

static const char *anthropic_name(const struct wire *self)
{
    (void)self;
    return "anthropic";
}

static const struct wire_vtable ANTHROPIC_VT = {
    anthropic_build, anthropic_name, common_destroy
};

/* ---- the factory: the ONE place that knows the kinds ----------------- */

static struct wire *wire_create(const char *kind, const char *model)
{
    const struct wire_vtable *vt;
    struct wire *w;
    size_t n = strlen(model);
    if (strcmp(kind, "openai") == 0) {
        vt = &OPENAI_VT;
    } else if (strcmp(kind, "anthropic") == 0) {
        vt = &ANTHROPIC_VT;
    } else {
        return NULL;               /* an unknown kind is a value, not a crash */
    }
    w = (struct wire *)malloc(sizeof *w);
    if (w == NULL || n + 1 > sizeof w->model) {
        free(w);
        return NULL;
    }
    w->vt = vt;
    memcpy(w->model, model, n + 1);
    w->calls = 0;
    return w;
}

/* ---- the caller: it never branches on the kind ----------------------- */

static void send_one(struct wire *w, const char *prompt)
{
    char body[256];
    int n = w->vt->build(w, prompt, body, sizeof body);
    if (n < 0) {
        printf("%-9s  (the body would not fit)\n", w->vt->name(w));
        return;
    }
    printf("%-9s  %s\n", w->vt->name(w), body);
}

int main(void)
{
    const char *kinds[3];
    int i;
    kinds[0] = "openai";
    kinds[1] = "anthropic";
    kinds[2] = "gopher";
    for (i = 0; i < 3; i++) {
        struct wire *w = wire_create(kinds[i], "jlu/qwen3");
        if (w == NULL) {
            printf("%-9s  refused by the factory: unknown kind\n", kinds[i]);
            continue;
        }
        send_one(w, "hello");
        printf("%-9s  calls so far: %d\n", w->vt->name(w), w->calls);
        w->vt->destroy(w);
    }
    return 0;
}
