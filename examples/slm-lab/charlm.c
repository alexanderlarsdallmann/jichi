/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* charlm.c - a character-level language model, trained from scratch on a CPU.
 *
 * The program of the slm-lab bench (examples/slm-lab). One file, C89, the C
 * library and libm only; nothing to install.
 *
 *   cc -std=c89 -pedantic -O2 -o charlm charlm.c -lm
 *   ./charlm corpus.txt
 *   ./charlm --steps 6000 --hidden 128 --seed 7 corpus.txt
 *
 * WHAT IT DOES. It reads a text and holds out the LAST part of it (10% by
 * default) as validation data, which training never sees. It trains a small
 * neural network to predict each character from the ones before it. And it
 * reports the model's loss beside three baselines that need no training at all,
 * because a model is worth what it beats:
 *   uniform  every character equally likely;
 *   unigram  how often each character occurs in the training part;
 *   bigram   which character follows which, counted in the training part.
 * The loss is the average negative log-probability of the true next character,
 * in nats: lower is better, and ln(vocabulary size) is knowing nothing.
 *
 * THE MODEL (Bengio et al., 2003). Each of the last CTX characters is looked up
 * in a table of EMB numbers (its embedding); the CTX embeddings, side by side,
 * feed one hidden layer of HID tanh units; a softmax over the vocabulary turns
 * the hidden layer into next-character probabilities. Training is minibatch
 * stochastic gradient descent on the cross-entropy loss. The gradients are
 * written out by hand -- there is no automatic differentiation here, so every
 * line of the backward pass is one a reader can check against the forward pass
 * above it.
 *
 * WHAT IT DOES NOT DO, said plainly: a "character" is a byte, so a UTF-8 letter
 * such as u-umlaut is two; there is no model file (a run trains and samples in
 * one go); nothing is tuned for speed. Each is an exercise, not an oversight.
 * The vocabulary is taken from the whole text, validation part included: a
 * model must at least be able to name every character it will be scored on.
 * That is the only thing the validation part contributes to training, and it
 * is printed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------------------------------------------------------------- options -- */

struct opts {
    const char *path;
    int ctx;        /* characters of context */
    int emb;        /* numbers per character embedding */
    int hid;        /* hidden units */
    long steps;     /* training steps (minibatches) */
    int batch;      /* examples per step */
    double lr;      /* learning rate; a tenth of it for the last quarter */
    unsigned long seed;
    double val;     /* the fraction held out, from the end */
    long every;     /* evaluate every this many steps */
    int sample;     /* characters to sample at the end */
};

static void usage(void)
{
    fprintf(stderr,
        "usage: charlm [--ctx N] [--emb N] [--hidden N] [--steps N] [--batch N]\n"
        "              [--lr X] [--seed N] [--val FRAC] [--every N] [--sample N]\n"
        "              corpus.txt\n");
}

static int parse_opts(int argc, char **argv, struct opts *o)
{
    int i;

    o->path = NULL;
    o->ctx = 6;
    o->emb = 16;
    o->hid = 96;
    o->steps = 4000;
    o->batch = 32;
    o->lr = 0.1;
    o->seed = 1;
    o->val = 0.1;
    o->every = 500;
    o->sample = 240;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (a[0] != '-') {
            o->path = a;
            continue;
        }
        if (v == NULL) {
            usage();
            return 0;
        }
        if (strcmp(a, "--ctx") == 0) {
            o->ctx = atoi(v);
        } else if (strcmp(a, "--emb") == 0) {
            o->emb = atoi(v);
        } else if (strcmp(a, "--hidden") == 0) {
            o->hid = atoi(v);
        } else if (strcmp(a, "--steps") == 0) {
            o->steps = atol(v);
        } else if (strcmp(a, "--batch") == 0) {
            o->batch = atoi(v);
        } else if (strcmp(a, "--lr") == 0) {
            o->lr = atof(v);
        } else if (strcmp(a, "--seed") == 0) {
            o->seed = (unsigned long)atol(v);
        } else if (strcmp(a, "--val") == 0) {
            o->val = atof(v);
        } else if (strcmp(a, "--every") == 0) {
            o->every = atol(v);
        } else if (strcmp(a, "--sample") == 0) {
            o->sample = atoi(v);
        } else {
            usage();
            return 0;
        }
        i++;
    }
    if (o->path == NULL || o->ctx < 1 || o->ctx > 32 || o->emb < 1 ||
        o->hid < 1 || o->steps < 0 || o->batch < 1 || o->lr <= 0.0 ||
        o->val <= 0.0 || o->val >= 0.5 || o->every < 1 || o->sample < 0) {
        usage();
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------ random ------ */

/* xorshift32: small, fast, and the same numbers on every machine for a seed,
 * which is what makes a run reproducible. Kept to 32 bits in an unsigned long
 * (at least 32 bits in C89). */
static unsigned long rng_state = 1;

static unsigned long rng_next(void)
{
    unsigned long x = rng_state;
    x ^= (x << 13) & 0xFFFFFFFFUL;
    x ^= x >> 17;
    x ^= (x << 5) & 0xFFFFFFFFUL;
    rng_state = x & 0xFFFFFFFFUL;
    return rng_state;
}

/* Uniform in [0, 1). */
static double rng_uniform(void)
{
    return (double)(rng_next() & 0xFFFFFFUL) / 16777216.0;
}

/* ------------------------------------------------------------- model ------ */

struct model {
    int V, ctx, emb, hid, in;   /* in = ctx * emb */
    double *C;    /* V x emb   character embeddings */
    double *W1;   /* in x hid  */
    double *b1;   /* hid       */
    double *W2;   /* hid x V   */
    double *b2;   /* V         */
};

static double *alloc_zero(long n)
{
    double *p = (double *)calloc((size_t)n, sizeof(double));
    if (p == NULL) {
        fprintf(stderr, "charlm: out of memory\n");
        exit(1);
    }
    return p;
}

static void model_alloc(struct model *m, int V, int ctx, int emb, int hid)
{
    m->V = V;
    m->ctx = ctx;
    m->emb = emb;
    m->hid = hid;
    m->in = ctx * emb;
    m->C = alloc_zero((long)V * emb);
    m->W1 = alloc_zero((long)m->in * hid);
    m->b1 = alloc_zero(hid);
    m->W2 = alloc_zero((long)hid * V);
    m->b2 = alloc_zero(V);
}

static void model_free(struct model *m)
{
    free(m->C);
    free(m->W1);
    free(m->b1);
    free(m->W2);
    free(m->b2);
}

static long model_params(const struct model *m)
{
    return (long)m->V * m->emb + (long)m->in * m->hid + m->hid +
           (long)m->hid * m->V + m->V;
}

/* Uniform in [-a, a]. */
static void fill_uniform(double *p, long n, double a)
{
    long i;
    for (i = 0; i < n; i++) {
        p[i] = (2.0 * rng_uniform() - 1.0) * a;
    }
}

static void model_init(struct model *m)
{
    /* Scaled by 1/sqrt(fan-in), so the hidden layer starts in tanh's steep
     * middle rather than its flat ends, where gradients vanish. */
    fill_uniform(m->C, (long)m->V * m->emb, 1.0);
    fill_uniform(m->W1, (long)m->in * m->hid, 1.0 / sqrt((double)m->in));
    fill_uniform(m->W2, (long)m->hid * m->V, 1.0 / sqrt((double)m->hid));
}

/* Scratch for one example's forward and backward pass. */
struct work {
    double *x;       /* in: the concatenated embeddings */
    double *h;       /* hid: tanh activations */
    double *p;       /* V: probabilities */
    double *dh;      /* hid */
    double *dx;      /* in */
};

static void work_alloc(struct work *w, const struct model *m)
{
    w->x = alloc_zero(m->in);
    w->h = alloc_zero(m->hid);
    w->p = alloc_zero(m->V);
    w->dh = alloc_zero(m->hid);
    w->dx = alloc_zero(m->in);
}

static void work_free(struct work *w)
{
    free(w->x);
    free(w->h);
    free(w->p);
    free(w->dh);
    free(w->dx);
}

/* Forward: the probabilities of the next character after text[pos-ctx..pos-1],
 * into w->p. Returns -ln p(text[pos]), the loss at this position. */
static double forward(const struct model *m, struct work *w,
                      const int *text, long pos)
{
    int j, k, v;
    double mx, sum;

    for (j = 0; j < m->ctx; j++) {
        memcpy(w->x + j * m->emb, m->C + (long)text[pos - m->ctx + j] * m->emb,
               (size_t)m->emb * sizeof(double));
    }
    for (k = 0; k < m->hid; k++) {
        double a = m->b1[k];
        int i;
        for (i = 0; i < m->in; i++) {
            a += w->x[i] * m->W1[(long)i * m->hid + k];
        }
        w->h[k] = tanh(a);
    }
    mx = -1e300;
    for (v = 0; v < m->V; v++) {
        double z = m->b2[v];
        for (k = 0; k < m->hid; k++) {
            z += w->h[k] * m->W2[(long)k * m->V + v];
        }
        w->p[v] = z;
        if (z > mx) {
            mx = z;
        }
    }
    /* softmax, shifted by the largest logit so exp() cannot overflow */
    sum = 0.0;
    for (v = 0; v < m->V; v++) {
        w->p[v] = exp(w->p[v] - mx);
        sum += w->p[v];
    }
    for (v = 0; v < m->V; v++) {
        w->p[v] /= sum;
    }
    return -log(w->p[text[pos]] > 1e-300 ? w->p[text[pos]] : 1e-300);
}

/* Backward: add this example's gradient, scaled by `scale`, into g. Uses the
 * activations forward() left in w. */
static void backward(const struct model *m, struct model *g, struct work *w,
                     const int *text, long pos, double scale)
{
    int i, j, k, v;
    int target = text[pos];

    /* d loss / d logit = p - onehot(target) */
    for (k = 0; k < m->hid; k++) {
        w->dh[k] = 0.0;
    }
    for (v = 0; v < m->V; v++) {
        double d = (w->p[v] - (v == target ? 1.0 : 0.0)) * scale;
        g->b2[v] += d;
        for (k = 0; k < m->hid; k++) {
            g->W2[(long)k * m->V + v] += w->h[k] * d;
            w->dh[k] += m->W2[(long)k * m->V + v] * d;
        }
    }
    /* through tanh: d tanh(a) / da = 1 - tanh(a)^2 */
    for (k = 0; k < m->hid; k++) {
        w->dh[k] *= 1.0 - w->h[k] * w->h[k];
        g->b1[k] += w->dh[k];
    }
    for (i = 0; i < m->in; i++) {
        double dx = 0.0;
        for (k = 0; k < m->hid; k++) {
            g->W1[(long)i * m->hid + k] += w->x[i] * w->dh[k];
            dx += m->W1[(long)i * m->hid + k] * w->dh[k];
        }
        w->dx[i] = dx;
    }
    /* into the embeddings of the context characters */
    for (j = 0; j < m->ctx; j++) {
        double *row = g->C + (long)text[pos - m->ctx + j] * m->emb;
        for (i = 0; i < m->emb; i++) {
            row[i] += w->dx[j * m->emb + i];
        }
    }
}

/* One parameter array: p -= lr * grad, and clear the gradient for the next step. */
static void sgd_one(double *p, double *grad, long n, double lr)
{
    long i;
    for (i = 0; i < n; i++) {
        p[i] -= lr * grad[i];
        grad[i] = 0.0;
    }
}

static void sgd_step(struct model *m, struct model *g, double lr)
{
    sgd_one(m->C, g->C, (long)m->V * m->emb, lr);
    sgd_one(m->W1, g->W1, (long)m->in * m->hid, lr);
    sgd_one(m->b1, g->b1, m->hid, lr);
    sgd_one(m->W2, g->W2, (long)m->hid * m->V, lr);
    sgd_one(m->b2, g->b2, m->V, lr);
}

/* The mean loss over positions [from, to), at most `cap` of them, spread evenly
 * so a capped evaluation still sees the whole range. */
static double mean_loss(const struct model *m, struct work *w, const int *text,
                        long from, long to, long cap)
{
    long n = to - from, stride, i, count = 0;
    double sum = 0.0;
    if (n <= 0) {
        return 0.0;
    }
    stride = (n > cap) ? n / cap : 1;
    for (i = from; i < to; i += stride) {
        sum += forward(m, w, text, i);
        count++;
    }
    return sum / (double)count;
}

/* ------------------------------------------------------------ main -------- */

int main(int argc, char **argv)
{
    struct opts o;
    FILE *f;
    unsigned char *raw;
    long n, cap, i, ntrain, val_from, step;
    int map[256], V = 0, *text, *chars;
    long *uni, *bi, *bi_row;
    double b_uniform, b_unigram, b_bigram, v_loss = 0.0, t_loss;
    struct model m, g;
    struct work w;

    if (!parse_opts(argc, argv, &o)) {
        return 2;
    }
    f = fopen(o.path, "rb");
    if (f == NULL) {
        fprintf(stderr, "charlm: cannot open %s\n", o.path);
        return 1;
    }
    cap = 1L << 20;   /* 1 MiB of text is plenty for a CPU lab */
    raw = (unsigned char *)malloc((size_t)cap);
    if (raw == NULL) {
        fclose(f);
        fprintf(stderr, "charlm: out of memory\n");
        return 1;
    }
    n = (long)fread(raw, 1, (size_t)cap, f);
    fclose(f);
    if (n < 20L * o.ctx + 200L) {
        fprintf(stderr, "charlm: %s has %ld characters; give it at least %ld\n",
                o.path, n, 20L * o.ctx + 200L);
        free(raw);
        return 1;
    }

    /* The vocabulary: the distinct bytes of the whole text (see the header). */
    for (i = 0; i < 256; i++) {
        map[i] = -1;
    }
    chars = (int *)malloc(256 * sizeof(int));
    text = (int *)malloc((size_t)n * sizeof(int));
    if (chars == NULL || text == NULL) {
        fprintf(stderr, "charlm: out of memory\n");
        return 1;
    }
    for (i = 0; i < n; i++) {
        if (map[raw[i]] < 0) {
            map[raw[i]] = V;
            chars[V] = raw[i];
            V++;
        }
        text[i] = map[raw[i]];
    }
    ntrain = n - (long)((double)n * o.val);
    val_from = ntrain;
    printf("corpus: %ld characters, vocabulary %d (bytes, from the whole text)\n",
           n, V);
    printf("split: train %ld, validation %ld (the last %.0f%%, never trained on)\n",
           ntrain, n - ntrain, o.val * 100.0);

    /* Baselines, counted on the training part and scored on the validation
     * part, each with one added to every count (Laplace) so an unseen event is
     * unlikely rather than impossible. */
    uni = (long *)calloc((size_t)V, sizeof(long));
    bi = (long *)calloc((size_t)V * (size_t)V, sizeof(long));
    bi_row = (long *)calloc((size_t)V, sizeof(long));
    if (uni == NULL || bi == NULL || bi_row == NULL) {
        fprintf(stderr, "charlm: out of memory\n");
        return 1;
    }
    for (i = 0; i < ntrain; i++) {
        uni[text[i]]++;
        if (i > 0) {
            bi[(long)text[i - 1] * V + text[i]]++;
            bi_row[text[i - 1]]++;
        }
    }
    b_uniform = log((double)V);
    b_unigram = 0.0;
    b_bigram = 0.0;
    for (i = val_from; i < n; i++) {
        b_unigram -= log((double)(uni[text[i]] + 1) / (double)(ntrain + V));
        b_bigram -= log((double)(bi[(long)text[i - 1] * V + text[i]] + 1) /
                        (double)(bi_row[text[i - 1]] + V));
    }
    b_unigram /= (double)(n - val_from);
    b_bigram /= (double)(n - val_from);
    printf("baselines on validation (nats per character; lower is better):\n");
    printf("  uniform  %.3f  (every character equally likely: ln %d)\n", b_uniform, V);
    printf("  unigram  %.3f  (character frequencies of the training part)\n", b_unigram);
    printf("  bigram   %.3f  (the previous character, counted on the training part)\n",
           b_bigram);

    /* The model. */
    rng_state = (o.seed & 0xFFFFFFFFUL) != 0 ? (o.seed & 0xFFFFFFFFUL) : 1UL;
    model_alloc(&m, V, o.ctx, o.emb, o.hid);
    model_alloc(&g, V, o.ctx, o.emb, o.hid);
    model_init(&m);
    work_alloc(&w, &m);
    printf("model: context %d, embedding %d, hidden %d: %ld parameters\n",
           o.ctx, o.emb, o.hid, model_params(&m));

    for (step = 0; step <= o.steps; step++) {
        if (step % o.every == 0 || step == o.steps) {
            t_loss = mean_loss(&m, &w, text, o.ctx, ntrain, 2000);
            v_loss = mean_loss(&m, &w, text, val_from, n, 20000);
            printf("step %-6ld train %.3f  validation %.3f\n", step, t_loss, v_loss);
            fflush(stdout);
        }
        if (step == o.steps) {
            break;
        }
        {
            double lr = (step >= (o.steps * 3) / 4) ? o.lr / 10.0 : o.lr;
            int b;
            for (b = 0; b < o.batch; b++) {
                /* a training position: its context must lie inside the
                 * training part too, so nothing of the validation part is
                 * ever an input or a target during training */
                long pos = o.ctx + (long)(rng_uniform() * (double)(ntrain - o.ctx));
                if (pos >= ntrain) {
                    pos = ntrain - 1;
                }
                (void)forward(&m, &w, text, pos);
                backward(&m, &g, &w, text, pos, 1.0 / (double)o.batch);
            }
            sgd_step(&m, &g, lr);
        }
    }

    if (v_loss < b_bigram) {
        printf("result: validation %.3f -- beats the bigram baseline by %.3f\n",
               v_loss, b_bigram - v_loss);
    } else {
        printf("result: validation %.3f -- does NOT beat the bigram baseline "
               "(%.3f): it has learned less than counting pairs of characters\n",
               v_loss, b_bigram);
    }

    /* Sample: start from the first `ctx` characters of the validation part and
     * draw each next character from the model's distribution. */
    if (o.sample > 0) {
        int *buf = (int *)malloc((size_t)(o.ctx + o.sample + 1) * sizeof(int));
        int s;
        if (buf == NULL) {
            fprintf(stderr, "charlm: out of memory\n");
            return 1;
        }
        for (s = 0; s < o.ctx; s++) {
            buf[s] = text[val_from + s];
        }
        printf("sample (seed %lu, %d characters):\n", o.seed, o.sample);
        for (s = 0; s < o.sample; s++) {
            double r, acc = 0.0;
            int v, pick;
            buf[o.ctx + s] = 0;
            (void)forward(&m, &w, buf, (long)(o.ctx + s));
            r = rng_uniform();
            pick = m.V - 1;
            for (v = 0; v < m.V; v++) {
                acc += w.p[v];
                if (r < acc) {
                    pick = v;
                    break;
                }
            }
            buf[o.ctx + s] = pick;
            putchar(chars[pick]);
        }
        putchar('\n');
        free(buf);
    }

    work_free(&w);
    model_free(&m);
    model_free(&g);
    free(uni);
    free(bi);
    free(bi_row);
    free(text);
    free(chars);
    free(raw);
    return 0;
}
