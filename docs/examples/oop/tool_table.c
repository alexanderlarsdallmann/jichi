/* tool_table.c -- an interface with two tiers, in the shape of jichi's
 * struct jc_tool (include/jc_tool.h):
 *   - built-in tools are CLASS-level: plain functions, no state, one entry each;
 *   - a dynamic tool (jichi's MCP tools) is an INSTANCE: the same interface plus
 *     a `ctx` pointer to its own state, and functions that receive that ctx.
 * The dispatcher looks a tool up by name in a table and prefers the instance
 * function when one is set. No switch on the tool's kind anywhere.
 *
 * Build and run (from this directory):
 *   cc -std=c89 -pedantic -Wall -Wextra -Werror -o tool_table tool_table.c && ./tool_table
 */
#include <stdio.h>
#include <string.h>

struct tool {
    const char *name;
    int (*run)(const char *arg, char *out, size_t cap);                 /* class tier */
    void *ctx;                                                           /* instance tier */
    int (*run_ctx)(void *ctx, const char *arg, char *out, size_t cap);
};

static int copy_into(char *out, size_t cap, const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    if (la + lb + 1 > cap) {
        return -1;
    }
    memcpy(out, a, la);
    memcpy(out + la, b, lb + 1);
    return 0;
}

/* Built-in, class-level tools: no state of their own. */
static int echo_run(const char *arg, char *out, size_t cap)
{
    return copy_into(out, cap, "echo: ", arg);
}

static int upper_run(const char *arg, char *out, size_t cap)
{
    size_t i, n = strlen(arg);
    if (n + 1 > cap) {
        return -1;
    }
    for (i = 0; i <= n; i++) {
        out[i] = (char)((arg[i] >= 'a' && arg[i] <= 'z') ? arg[i] - 'a' + 'A' : arg[i]);
    }
    return 0;
}

/* A dynamic, instance-bound tool: its state travels in ctx (in jichi: an MCP
 * server connection and the remote tool's name). */
struct remote { const char *server; int calls; };

static int remote_run(void *ctx, const char *arg, char *out, size_t cap)
{
    struct remote *r = (struct remote *)ctx;
    r->calls++;
    return copy_into(out, cap, r->server, arg);
}

static int dispatch(const struct tool *table, size_t n, const char *name,
                    const char *arg, char *out, size_t cap)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (strcmp(table[i].name, name) == 0) {
            if (table[i].run_ctx != NULL) {
                return table[i].run_ctx(table[i].ctx, arg, out, cap);
            }
            return table[i].run(arg, out, cap);
        }
    }
    return copy_into(out, cap, "no such tool: ", name);   /* an error is a value */
}

int main(void)
{
    struct remote notes;
    struct tool table[3];
    char out[128];
    const char *calls[4][2];
    int i;

    notes.server = "notes-server says: ";
    notes.calls = 0;
    table[0].name = "echo";  table[0].run = echo_run;  table[0].ctx = NULL;   table[0].run_ctx = NULL;
    table[1].name = "upper"; table[1].run = upper_run; table[1].ctx = NULL;   table[1].run_ctx = NULL;
    table[2].name = "notes"; table[2].run = NULL;      table[2].ctx = &notes; table[2].run_ctx = remote_run;

    calls[0][0] = "echo";  calls[0][1] = "hi";
    calls[1][0] = "upper"; calls[1][1] = "hi";
    calls[2][0] = "notes"; calls[2][1] = "hi";
    calls[3][0] = "rm";    calls[3][1] = "-rf /";
    for (i = 0; i < 4; i++) {
        if (dispatch(table, 3, calls[i][0], calls[i][1], out, sizeof out) != 0) {
            printf("%-6s -> (output did not fit)\n", calls[i][0]);
        } else {
            printf("%-6s -> %s\n", calls[i][0], out);
        }
    }
    printf("the notes instance was called %d time(s)\n", notes.calls);
    return 0;
}
