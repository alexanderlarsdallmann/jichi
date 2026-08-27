/* jichi-supervisor.c -- a minimal, dependency-free C89 task-queue supervisor for
 * one or more autonomous `jichi --auto` instances.
 *
 * This is the compiled counterpart to loop.sh, illustrating the same contract
 * in C: claim a task file by atomic rename() (safe across instances), run jichi
 * as a bounded non-interactive turn capturing its --output json result, and
 * route the task to done/ or failed/ purely on the child's EXIT CODE (the
 * authoritative signal; the JSON body is captured for logging). See
 * docs/AUTONOMOUS_LOOPS.md.
 *
 * Build:  make            (in this directory)
 * Run:    JICHI_CONFIG=./config.autonomous.json ./jichi-supervisor ./queue .
 *
 * Deliberately tiny: no JSON parser (jichi's exit code already encodes the
 * outcome -- 0 done / 1 verify-failed|error|budget / 2 usage / 130,143 signal),
 * no threads, no network. C89 / POSIX only. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define PATHMAX 1024

static const char *env_or(const char *name, const char *dflt)
{
    const char *v = getenv(name);
    return (v != NULL && v[0] != '\0') ? v : dflt;
}

static void join(char *out, size_t cap, const char *a, const char *b)
{
    /* out = a "/" b  (snprintf is declared via _POSIX_C_SOURCE). */
    snprintf(out, cap, "%s/%s", a, b);
}

static int ensure_dir(const char *path)
{
    /* mkdir, tolerating an already-existing directory. */
    if (mkdir(path, 0700) == 0) return 0;
    return (errno == EEXIST) ? 0 : -1;
}

static void make_layout(const char *queue)
{
    const char *subs[5];
    char p[PATHMAX];
    int i;
    subs[0] = "pending"; subs[1] = "running"; subs[2] = "done";
    subs[3] = "failed";  subs[4] = "attempts";
    ensure_dir(queue);
    for (i = 0; i < 5; i++) { join(p, sizeof p, queue, subs[i]); ensure_dir(p); }
}

/* Claim one pending task by atomic rename into running/. Returns 1 and fills
 * `running` (full path) and `base` (task file name) on success, else 0. */
static int claim_task(const char *queue, char *running, char *base)
{
    char pend[PATHMAX], from[PATHMAX];
    DIR *d;
    struct dirent *e;
    int got = 0;

    join(pend, sizeof pend, queue, "pending");
    d = opendir(pend);
    if (d == NULL) return 0;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n < 6 || strcmp(e->d_name + n - 5, ".task") != 0) continue;
        join(from, sizeof from, pend, e->d_name);
        snprintf(running, PATHMAX, "%s/running/%s.%ld",
                 queue, e->d_name, (long)getpid());
        if (rename(from, running) == 0) {   /* atomic: one winner per task */
            strcpy(base, e->d_name);
            got = 1;
            break;
        }
    }
    closedir(d);
    return got;
}

/* Run jichi on the task's prompt (the file's contents). Returns the exit code. */
static int run_jlu(const char *bin, const char *config, const char *workspace,
                   const char *running, const char *journal)
{
    char *argv[24];
    int n = 0;
    pid_t pid;
    int status = 0;

    argv[n++] = (char *)bin;
    argv[n++] = (char *)"--config";   argv[n++] = (char *)config;
    argv[n++] = (char *)"--auto";
    argv[n++] = (char *)"--output";   argv[n++] = (char *)"json";
    argv[n++] = (char *)"-q";
    argv[n++] = (char *)"--no-session";
    argv[n++] = (char *)"--budget-tokens";
    argv[n++] = (char *)env_or("BUDGET_TOKENS", "400k");
    argv[n++] = (char *)"--deadline";
    argv[n++] = (char *)env_or("DEADLINE", "30m");
    argv[n++] = (char *)"--journal";  argv[n++] = (char *)journal;
    argv[n++] = (char *)"-p";
    argv[n++] = (char *)running;      /* placeholder; replaced below */
    argv[n] = NULL;

    /* Read the prompt from the task file into a heap buffer. */
    {
        FILE *f = fopen(running, "rb");
        long sz;
        char *buf;
        if (f == NULL) return 2;
        fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
        if (sz < 0) { fclose(f); return 2; }
        buf = (char *)malloc((size_t)sz + 1);
        if (buf == NULL) { fclose(f); return 2; }
        if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return 2; }
        buf[sz] = '\0';
        fclose(f);
        argv[n - 1] = buf;   /* the -p argument is the prompt text */
    }

    pid = fork();
    if (pid < 0) { free(argv[n - 1]); return 1; }
    if (pid == 0) {
        /* Child: run in the workspace so the path fence + repo map scope there. */
        if (workspace != NULL && workspace[0] != '\0') {
            if (chdir(workspace) != 0) _exit(2);
        }
        execvp(bin, argv);
        _exit(127);            /* exec failed */
    }
    free(argv[n - 1]);
    while (waitpid(pid, &status, 0) < 0) { /* retry on EINTR */ }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

static int read_attempts(const char *queue, const char *base)
{
    char p[PATHMAX];
    FILE *f;
    int a = 0;
    snprintf(p, sizeof p, "%s/attempts/%s", queue, base);
    f = fopen(p, "r");
    if (f != NULL) { if (fscanf(f, "%d", &a) != 1) a = 0; fclose(f); }
    return a;
}

static void write_attempts(const char *queue, const char *base, int a)
{
    char p[PATHMAX];
    FILE *f;
    snprintf(p, sizeof p, "%s/attempts/%s", queue, base);
    f = fopen(p, "w");
    if (f != NULL) { fprintf(f, "%d\n", a); fclose(f); }
}

static void move_to(const char *queue, const char *sub, const char *running,
                    const char *base)
{
    char dest[PATHMAX];
    snprintf(dest, sizeof dest, "%s/%s/%s", queue, sub, base);
    rename(running, dest);
}

int main(int argc, char **argv)
{
    const char *bin = env_or("JICHI_BIN", "jichi");
    const char *config = getenv("JICHI_CONFIG");
    const char *queue = (argc > 1) ? argv[1] : env_or("QUEUE", "./queue");
    const char *workspace = (argc > 2) ? argv[2] : env_or("WORKSPACE", ".");
    const char *jdir = env_or("JOURNAL_DIR", "/tmp");
    int max_attempts = atoi(env_or("MAX_ATTEMPTS", "2"));
    int run_once = atoi(env_or("RUN_ONCE", "0"));
    int poll = atoi(env_or("POLL", "10"));

    char running[PATHMAX], base[PATHMAX], journal[PATHMAX];

    if (config == NULL || config[0] == '\0') {
        fprintf(stderr, "jichi-supervisor: set JICHI_CONFIG\n");
        return 2;
    }
    if (max_attempts < 1) max_attempts = 1;
    make_layout(queue);

    for (;;) {
        int rc, att;
        if (!claim_task(queue, running, base)) {
            if (run_once) { fprintf(stderr, "queue drained; exiting\n"); break; }
            sleep((unsigned)(poll > 0 ? poll : 10));
            continue;
        }
        snprintf(journal, sizeof journal, "%s/%s.jsonl", jdir, base);
        fprintf(stderr, "run %s\n", base);
        rc = run_jlu(bin, config, workspace, running, journal);
        fprintf(stderr, "  -> exit %d\n", rc);

        if (rc == 0) {
            move_to(queue, "done", running, base);
        } else if (rc == 2) {
            move_to(queue, "failed", running, base);      /* misconfig */
        } else if (rc == 130 || rc == 143) {
            move_to(queue, "pending", running, base);      /* requeue + stop */
            fprintf(stderr, "  interrupted; stopping\n");
            break;
        } else {
            att = read_attempts(queue, base) + 1;
            write_attempts(queue, base, att);
            if (att >= max_attempts) {
                move_to(queue, "failed", running, base);   /* quarantine */
            } else {
                move_to(queue, "pending", running, base);  /* retry later */
            }
        }
    }
    return 0;
}
