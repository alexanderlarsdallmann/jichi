/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_config_trust.c - the project-config trust step (see jc_config_trust.h). */
#include "jc_config_trust.h"
#include "jc_config.h"
#include "jc_json.h"
#include "jc_log.h"
#include "jc_path.h"
#include "jc_perm.h"
#include "jc_sha256.h"
#include "jc_snprintf.h"
#include "jc_str.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* How a NARROW key tightens. */
enum narrow_kind {
    NK_NONE = 0,
    NK_ON,          /* a switch whose ON is the strict side (a fence, an audit)  */
    NK_OFF,         /* an opt-in whose OFF is the strict side                     */
    NK_MODE,        /* agent mode: plan > chat > auto                             */
    NK_POLICY,      /* privileged/kinetic policy: deny > ask > allow              */
    NK_FENCE,       /* tri-state fence: on > auto > off                           */
    NK_BOUND,       /* a cap where smaller is tighter and 0 is the tightest       */
    NK_BOUND0,      /* a cap where 0 means OFF (unbounded) and smaller is tighter */
    NK_PERM         /* permissions: deny adds, allow waits for trust              */
};

struct key_class {
    const char *key;
    int cls;
    int nk;
    const char *what;   /* what the key does when it is not applied: the line
                         * the prompt and the notice show for a GATED key */
};

/* EVERY top-level key jc_config.c reads, classified (M851). The lint
 * tests/smoke/config_trust_lint.sh holds this table against the loader in both
 * directions; when in doubt a key is GATED -- over-classifying costs the user a
 * listed, ignored key and one `jichi trust`, under-classifying costs a hole.
 * Kept in alphabetical order. */
static const struct key_class CLASSES[] = {
    { "accessible",           JC_CFGTRUST_SAFE,   NK_NONE,   "set the accessible output mode" },
    { "aliases",              JC_CFGTRUST_GATED,  NK_NONE,   "define model aliases, which can route requests and run commands" },
    { "assignments",          JC_CFGTRUST_NARROW, NK_OFF,    "turn on the assignment tools (hint, ask_for_help and its helper)" },
    { "audioGenMaxBytes",     JC_CFGTRUST_SAFE,   NK_NONE,   "set an output cap" },
    { "autoCompact",          JC_CFGTRUST_SAFE,   NK_NONE,   "set auto-compaction" },
    { "autoContext",          JC_CFGTRUST_SAFE,   NK_NONE,   "set automatic context" },
    { "autoContextMaxTokens", JC_CFGTRUST_SAFE,   NK_NONE,   "set an automatic-context budget" },
    { "autoContextSources",   JC_CFGTRUST_SAFE,   NK_NONE,   "choose automatic-context sources" },
    { "autoContextTopK",      JC_CFGTRUST_SAFE,   NK_NONE,   "set automatic-context breadth" },
    { "board",                JC_CFGTRUST_NARROW, NK_OFF,    "turn on the board tool" },
    { "budgetPanel",          JC_CFGTRUST_SAFE,   NK_NONE,   "set the budget panel" },
    { "budgetPanelEvery",     JC_CFGTRUST_SAFE,   NK_NONE,   "set the budget panel's cadence" },
    { "caBundle",             JC_CFGTRUST_GATED,  NK_NONE,   "name the CA-bundle file TLS trusts -- a path of the config's choosing that repoints the trust anchors (PEER/HOST verification stay on, but the file decides what is trusted)" },
    { "configEditable",       JC_CFGTRUST_NARROW, NK_OFF,    "let the agent edit the configuration" },
    { "constraintsAutoAdopt", JC_CFGTRUST_NARROW, NK_ON,     "stop reading rules out of an --auto request" },
    { "contextLimit",         JC_CFGTRUST_SAFE,   NK_NONE,   "set the context limit" },
    { "continueCutReply",     JC_CFGTRUST_GATED,  NK_NONE,   "continue a reply cut at the output ceiling automatically (one more model call)" },
    { "control",              JC_CFGTRUST_NARROW, NK_OFF,    "open the mid-run control socket" },
    { "costModel",            JC_CFGTRUST_SAFE,   NK_NONE,   "set cost accounting" },
    { "craft",                JC_CFGTRUST_SAFE,   NK_NONE,   "set craft guidance" },
    { "daemonWorkers",        JC_CFGTRUST_GATED,  NK_NONE,   "set how many daemon worker processes run" },
    { "daemonWorkerTimeout",  JC_CFGTRUST_GATED,  NK_NONE,   "set the daemon workers' timeout" },
    { "design",               JC_CFGTRUST_GATED,  NK_NONE,   "read a design file into the prompt, from any path" },
    { "diskFloorFiles",       JC_CFGTRUST_SAFE,   NK_NONE,   "set the disk-space fence" },
    { "diskFloorMb",          JC_CFGTRUST_SAFE,   NK_NONE,   "set the disk-space fence" },
    { "docs",                 JC_CFGTRUST_GATED,  NK_NONE,   "index documentation directories, from any path" },
    { "editScope",            JC_CFGTRUST_GATED,  NK_NONE,   "arm the autonomy envelope, which switches a headless -p run to auto" },
    { "fetchMaxBytes",        JC_CFGTRUST_SAFE,   NK_NONE,   "set an output cap" },
    { "formatCommand",        JC_CFGTRUST_GATED,  NK_NONE,   "run a formatter command" },
    { "fuzzyEdit",            JC_CFGTRUST_SAFE,   NK_NONE,   "set fuzzy edit matching" },
    { "gitMaxBytes",          JC_CFGTRUST_SAFE,   NK_NONE,   "set an output cap" },
    { "hooks",                JC_CFGTRUST_GATED,  NK_NONE,   "run hook commands" },
    { "hooksEnabled",         JC_CFGTRUST_NARROW, NK_OFF,    "turn hooks on" },
    { "ignoreDirs",           JC_CFGTRUST_SAFE,   NK_NONE,   "set the directories the index skips" },
    { "imageGenMaxBytes",     JC_CFGTRUST_SAFE,   NK_NONE,   "set an output cap" },
    { "inputHistory",         JC_CFGTRUST_NARROW, NK_OFF,    "keep typed TUI input across runs" },
    { "instructions",         JC_CFGTRUST_GATED,  NK_NONE,   "read instruction files into the prompt, from any path" },
    { "kineticAudit",         JC_CFGTRUST_NARROW, NK_ON,     "turn the kinetic audit off" },
    { "kineticCommands",      JC_CFGTRUST_NARROW, NK_POLICY, "loosen the kinetic-command policy" },
    { "kineticCommandsAllow", JC_CFGTRUST_GATED,  NK_NONE,   "pre-approve kinetic commands" },
    { "kineticShellPrefixes", JC_CFGTRUST_SAFE,   NK_NONE,   "mark more shell prefixes as kinetic (stricter)" },
    { "language",             JC_CFGTRUST_SAFE,   NK_NONE,   "set the answer language" },
    { "learnOnStop",          JC_CFGTRUST_NARROW, NK_OFF,    "run the mentor after every --auto run" },
    { "logging",              JC_CFGTRUST_GATED,  NK_NONE,   "write the event log to a path of its choosing" },
    { "lowResource",          JC_CFGTRUST_SAFE,   NK_NONE,   "set the lite profile" },
    { "lspServers",           JC_CFGTRUST_GATED,  NK_NONE,   "start language-server programs" },
    { "markdown",             JC_CFGTRUST_SAFE,   NK_NONE,   "set markdown rendering" },
    { "maxParallelAgents",    JC_CFGTRUST_NARROW, NK_BOUND0, "raise the parallel-agent cap" },
    { "maxRetries",           JC_CFGTRUST_SAFE,   NK_NONE,   "set request retries" },
    { "maxSubagentDepth",     JC_CFGTRUST_NARROW, NK_BOUND,  "raise the subagent nesting cap" },
    { "maxSubagentIters",     JC_CFGTRUST_NARROW, NK_BOUND,  "raise the subagent iteration cap" },
    { "maxToolIters",         JC_CFGTRUST_NARROW, NK_BOUND,  "raise the tool-iteration cap" },
    { "mcpServers",           JC_CFGTRUST_GATED,  NK_NONE,   "start MCP servers or connect to them" },
    { "memBudgetMb",          JC_CFGTRUST_NARROW, NK_BOUND0, "raise or remove the memory budget" },
    { "mode",                 JC_CFGTRUST_NARROW, NK_MODE,   "loosen the session mode" },
    { "model",                JC_CFGTRUST_GATED,  NK_NONE,   "choose the model and where its requests go" },
    { "models",               JC_CFGTRUST_GATED,  NK_NONE,   "add models, and send requests (and the API key they name) to their endpoints" },
    { "notify",               JC_CFGTRUST_GATED,  NK_NONE,   "run a notification command" },
    { "notifyBell",           JC_CFGTRUST_SAFE,   NK_NONE,   "ring the terminal bell" },
    { "numberFormat",         JC_CFGTRUST_SAFE,   NK_NONE,   "set number formatting" },
    { "outputStyle",          JC_CFGTRUST_SAFE,   NK_NONE,   "set the output style" },
    { "parallelMetric",       JC_CFGTRUST_GATED,  NK_NONE,   "run a metric command in the parallel worktrees" },
    { "parallelTaskTimeout",  JC_CFGTRUST_GATED,  NK_NONE,   "set the parallel children's watchdog" },
    { "parallelTestEdits",    JC_CFGTRUST_GATED,  NK_NONE,   "merge a moved test assertion in a pin file when the declared metric did not fall" },
    { "parallelVerify",       JC_CFGTRUST_NARROW, NK_OFF,    "run the verifier in every parallel child" },
    { "pathFence",            JC_CFGTRUST_NARROW, NK_FENCE,  "turn the path fence off" },
    { "pdfCommand",           JC_CFGTRUST_GATED,  NK_NONE,   "run a PDF command" },
    { "permissions",          JC_CFGTRUST_NARROW, NK_PERM,   "pre-approve tools" },
    { "preserveDiscarded",    JC_CFGTRUST_SAFE,   NK_NONE,   "keep discarded parallel work" },
    { "privilegedAudit",      JC_CFGTRUST_NARROW, NK_ON,     "turn the privileged-command audit off" },
    { "privilegedCommands",   JC_CFGTRUST_NARROW, NK_POLICY, "loosen the privileged-command policy" },
    { "privilegedCommandsAllow", JC_CFGTRUST_GATED, NK_NONE, "pre-approve privileged commands (sudo, doas, ...)" },
    { "promptCache",          JC_CFGTRUST_SAFE,   NK_NONE,   "set prompt caching" },
    { "promptCacheTtl",       JC_CFGTRUST_SAFE,   NK_NONE,   "set the prompt-cache lifetime" },
    { "readMaxBytes",         JC_CFGTRUST_SAFE,   NK_NONE,   "set an output cap" },
    { "referenceRoots",       JC_CFGTRUST_GATED,  NK_NONE,   "let the file tools read directories outside the project" },
    { "references",           JC_CFGTRUST_SAFE,   NK_NONE,   "set @-references" },
    { "relayNotesBytes",      JC_CFGTRUST_SAFE,   NK_NONE,   "set the relay notes' byte budget" },
    { "relay",                JC_CFGTRUST_SAFE,   NK_NONE,   "let an exhausted window end the run as an explained handover" },
    { "relayInProcess",       JC_CFGTRUST_SAFE,   NK_NONE,   "continue the relay's next legs in the same process (M952, topology B)" },
    { "relayMaxLegs",         JC_CFGTRUST_SAFE,   NK_NONE,   "cap the in-process relay's legs (M952, topology B)" },
    { "repoMap",              JC_CFGTRUST_SAFE,   NK_NONE,   "set the repository map" },
    { "repoMapLimit",         JC_CFGTRUST_SAFE,   NK_NONE,   "set the repository map's size" },
    { "retrieval",            JC_CFGTRUST_GATED,  NK_NONE,   "tune retrieval, which can route query rewriting to a model" },
    { "revertOutOfScope",     JC_CFGTRUST_NARROW, NK_ON,     "stop reverting out-of-scope changes" },
    { "routing",              JC_CFGTRUST_GATED,  NK_NONE,   "change which model each turn is routed to" },
    { "runMaxBytes",          JC_CFGTRUST_SAFE,   NK_NONE,   "set an output cap" },
    { "runTimeout",           JC_CFGTRUST_NARROW, NK_BOUND0, "raise or remove the shell timeout" },
    { "secretEnv",            JC_CFGTRUST_SAFE,   NK_NONE,   "name more environment variables to remove from every child (removal-only: can DoS a child's lookups, like the output caps, never exfiltrate)" },
    { "search",               JC_CFGTRUST_GATED,  NK_NONE,   "send web searches (and the key it names) to an endpoint" },
    { "searchMaxBytes",       JC_CFGTRUST_SAFE,   NK_NONE,   "set an output cap" },
    { "scopedEditMax",        JC_CFGTRUST_NARROW, NK_BOUND0, "turn off or loosen the scoped edit check" },
    { "selfReview",           JC_CFGTRUST_SAFE,   NK_NONE,   "set self-review" },
    { "shellFence",           JC_CFGTRUST_NARROW, NK_FENCE,  "turn the shell fence off" },
    { "shellFenceAllow",      JC_CFGTRUST_GATED,  NK_NONE,   "widen the shell fence" },
    { "snapshotLimit",        JC_CFGTRUST_SAFE,   NK_NONE,   "set how many checkpoints are kept" },
    { "snapshots",            JC_CFGTRUST_NARROW, NK_ON,     "turn checkpoints (and /undo) off" },
    { "sound",                JC_CFGTRUST_GATED,  NK_NONE,   "run sound commands" },
    { "strictGreen",          JC_CFGTRUST_NARROW, NK_ON,     "accept a verify that passed for the wrong reason" },
    { "strictness",           JC_CFGTRUST_GATED,  NK_NONE,   "set the strictness dials, including the dirty-tree dispatch guard" },
    { "systemPrompt",         JC_CFGTRUST_GATED,  NK_NONE,   "add text to the system prompt of every turn and subagent" },
    { "testCommand",          JC_CFGTRUST_GATED,  NK_NONE,   "run a test command" },
    { "timeFormat",           JC_CFGTRUST_SAFE,   NK_NONE,   "set time formatting" },
    { "timeouts",             JC_CFGTRUST_GATED,  NK_NONE,   "change the request timeouts, including the dead-stream fence" },
    { "toolProfile",          JC_CFGTRUST_SAFE,   NK_NONE,   "set the tool profile" },
    { "tools",                JC_CFGTRUST_GATED,  NK_NONE,   "add tools that run commands" },
    { "transcribeMaxBytes",   JC_CFGTRUST_SAFE,   NK_NONE,   "set an output cap" },
    { "typeAhead",            JC_CFGTRUST_NARROW, NK_OFF,    "turn on type-ahead" },
    { "verify",               JC_CFGTRUST_GATED,  NK_NONE,   "run a verifier command, and arm the autonomy envelope (a headless -p run switches to auto)" },
    { "verifyKind",           JC_CFGTRUST_SAFE,   NK_NONE,   "label the verifier" },
    { "voice",                JC_CFGTRUST_NARROW, NK_OFF,    "speak replies through a speech model" },
    { "wisdom",               JC_CFGTRUST_SAFE,   NK_NONE,   "set the wisdom notes" }
};

#define NCLASSES ((int)(sizeof(CLASSES) / sizeof(CLASSES[0])))

int jc_cfgtrust_count(void)
{
    return NCLASSES;
}

const char *jc_cfgtrust_key_at(int i)
{
    return (i >= 0 && i < NCLASSES) ? CLASSES[i].key : NULL;
}

int jc_cfgtrust_class_at(int i)
{
    return (i >= 0 && i < NCLASSES) ? CLASSES[i].cls : -1;
}

static const struct key_class *class_entry(const char *key)
{
    int i;
    if (key == NULL) {
        return NULL;
    }
    for (i = 0; i < NCLASSES; i++) {
        if (strcmp(CLASSES[i].key, key) == 0) {
            return &CLASSES[i];
        }
    }
    return NULL;
}

int jc_cfgtrust_class_of(const char *key)
{
    const struct key_class *k = class_entry(key);
    return (k != NULL) ? k->cls : -1;
}

/* --- small value readers ------------------------------------------------------ */

/* A bool the way a config file is read (CLAUDE.md: leniently): JSON bool,
 * number (nonzero true), or a bool word. Returns 0 and leaves *out alone when
 * the value is none of those. */
static int item_bool(const cJSON *it, int *out)
{
    int b = 0;
    if (it == NULL) {
        return 0;
    }
    if (cJSON_IsBool(it)) {
        *out = cJSON_IsTrue(it) ? 1 : 0;
        return 1;
    }
    if (cJSON_IsNumber(it)) {
        *out = (it->valuedouble != 0.0) ? 1 : 0;
        return 1;
    }
    if (cJSON_IsString(it) && it->valuestring != NULL &&
        jc_bool_from_word(it->valuestring, &b)) {
        *out = b ? 1 : 0;
        return 1;
    }
    return 0;
}

static int item_num(const cJSON *it, double *out)
{
    if (it != NULL && cJSON_IsNumber(it)) {
        *out = it->valuedouble;
        return 1;
    }
    return 0;
}

/* privileged/kinetic policy word -> JC_PRIVPOL_*, or -1 */
static int policy_of(const cJSON *it)
{
    const char *s;
    if (it == NULL || !cJSON_IsString(it) || it->valuestring == NULL) {
        return -1;
    }
    s = it->valuestring;
    if (strcmp(s, "deny") == 0) {
        return JC_PRIVPOL_DENY;
    }
    if (strcmp(s, "ask") == 0) {
        return JC_PRIVPOL_ASK;
    }
    if (strcmp(s, "allow") == 0) {
        return JC_PRIVPOL_ALLOW;
    }
    return -1;
}

/* strictness ranks: higher is stricter */
static int policy_rank(int p)
{
    return p == JC_PRIVPOL_DENY ? 2 : (p == JC_PRIVPOL_ASK ? 1 : 0);
}

static int fence_rank(int f)
{
    return f == 1 ? 2 : (f == -1 ? 1 : 0);
}

/* A list of strings, joined (truncated) into `out`. */
static void join_strings(const cJSON *arr, char *out, jc_size cap)
{
    const cJSON *it;
    jc_size n = 0;
    out[0] = '\0';
    if (arr == NULL) {
        return;
    }
    for (it = arr->child; it != NULL; it = it->next) {
        const char *s = NULL;
        if (cJSON_IsString(it)) {
            s = it->valuestring;
        } else if (cJSON_IsObject(it)) {
            s = jc_json_get_str(it, "name", NULL);
            if (s == NULL) {
                s = jc_json_get_str(it, "command", NULL);
            }
        }
        if (s == NULL) {
            continue;
        }
        if (n + strlen(s) + 4 >= cap) {
            jc_snprintf(out + n, cap - n, "%s...", n > 0 ? ", " : "");
            return;
        }
        n += (jc_size)jc_snprintf(out + n, cap - n, "%s%s", n > 0 ? ", " : "",
                                  s);
    }
}

/* the members of an object, by key */
static void join_keys(const cJSON *obj, char *out, jc_size cap)
{
    const cJSON *it;
    jc_size n = 0;
    out[0] = '\0';
    if (obj == NULL) {
        return;
    }
    for (it = obj->child; it != NULL; it = it->next) {
        if (it->string == NULL) {
            continue;
        }
        if (n + strlen(it->string) + 4 >= cap) {
            jc_snprintf(out + n, cap - n, "%s...", n > 0 ? ", " : "");
            return;
        }
        n += (jc_size)jc_snprintf(out + n, cap - n, "%s%s", n > 0 ? ", " : "",
                                  it->string);
    }
}

/* where a models list sends its requests */
static void model_endpoints(const cJSON *v, char *out, jc_size cap)
{
    const cJSON *it;
    jc_size n = 0;
    out[0] = '\0';
    if (v == NULL) {
        return;
    }
    if (cJSON_IsObject(v)) {
        const char *b = jc_json_get_str(v, "apiBase", NULL);
        jc_snprintf(out, cap, "%s", b != NULL ? b : "(the provider's default)");
        return;
    }
    for (it = v->child; it != NULL; it = it->next) {
        const char *b = cJSON_IsObject(it) ? jc_json_get_str(it, "apiBase", NULL)
                                           : NULL;
        const char *nm = cJSON_IsObject(it) ? jc_json_get_str(it, "name", NULL)
                                            : NULL;
        char one[300];
        jc_snprintf(one, sizeof one, "%s%s%s", nm != NULL ? nm : "?",
                    b != NULL ? " at " : "", b != NULL ? b : "");
        if (n + strlen(one) + 4 >= cap) {
            jc_snprintf(out + n, cap - n, "%s...", n > 0 ? "; " : "");
            return;
        }
        n += (jc_size)jc_snprintf(out + n, cap - n, "%s%s", n > 0 ? "; " : "",
                                  one);
    }
}

void jc_cfgtrust_describe(const char *key, const cJSON *val, char *out,
                          jc_size cap)
{
    const struct key_class *k = class_entry(key);
    char d[700];

    d[0] = '\0';
    if (key == NULL) {
        jc_snprintf(out, cap, "?");
        return;
    }
    if ((strcmp(key, "models") == 0 || strcmp(key, "model") == 0) &&
        val != NULL) {
        model_endpoints(val, d, sizeof d);
        jc_snprintf(out, cap, "%s: send model requests (and the API key each "
                    "entry names) to %s", key, d[0] != '\0' ? d : "its entries");
    } else if (strcmp(key, "mcpServers") == 0 && val != NULL) {
        if (cJSON_IsObject(val)) {
            join_keys(val, d, sizeof d);
        } else {
            join_strings(val, d, sizeof d);
        }
        jc_snprintf(out, cap, "mcpServers: start or connect to MCP servers: %s",
                    d[0] != '\0' ? d : "(unnamed)");
    } else if ((strcmp(key, "verify") == 0 || strcmp(key, "testCommand") == 0 ||
                strcmp(key, "formatCommand") == 0 ||
                strcmp(key, "pdfCommand") == 0) &&
               val != NULL && cJSON_IsString(val)) {
        jc_snprintf(out, cap, "%s: run `%s`%s", key, val->valuestring,
                    strcmp(key, "verify") == 0
                        ? ", and arm the autonomy envelope (a headless -p run "
                          "switches to auto)" : "");
    } else if ((strcmp(key, "lspServers") == 0 || strcmp(key, "tools") == 0 ||
                strcmp(key, "hooks") == 0 || strcmp(key, "sound") == 0) &&
               val != NULL) {
        if (cJSON_IsObject(val)) {
            join_keys(val, d, sizeof d);
        } else {
            join_strings(val, d, sizeof d);
        }
        jc_snprintf(out, cap, "%s: %s: %s", key, k != NULL ? k->what : "set it",
                    d[0] != '\0' ? d : "(see the file)");
    } else if ((strcmp(key, "instructions") == 0 ||
                strcmp(key, "referenceRoots") == 0 ||
                strcmp(key, "editScope") == 0 ||
                strcmp(key, "privilegedCommandsAllow") == 0 ||
                strcmp(key, "kineticCommandsAllow") == 0 ||
                strcmp(key, "shellFenceAllow") == 0) &&
               val != NULL && cJSON_IsArray(val)) {
        join_strings(val, d, sizeof d);
        jc_snprintf(out, cap, "%s: %s: %s", key, k != NULL ? k->what : "set it",
                    d);
    } else if (strcmp(key, "notify") == 0 && val != NULL &&
               cJSON_IsString(val)) {
        jc_snprintf(out, cap, "notify: run `%s` when a run ends",
                    val->valuestring);
    } else if (strcmp(key, "logging") == 0 && val != NULL) {
        const char *p = jc_json_get_str(val, "path", NULL);
        jc_snprintf(out, cap, "logging: %s%s%s",
                    k != NULL ? k->what : "set logging",
                    p != NULL ? ": " : "", p != NULL ? p : "");
    } else if (strcmp(key, "search") == 0 && val != NULL) {
        const char *u = jc_json_get_str(val, "url", NULL);
        jc_snprintf(out, cap, "search: send web searches to %s",
                    u != NULL ? u : "the endpoint it names");
    } else if (k != NULL) {
        jc_snprintf(out, cap, "%s: %s", key, k->what);
    } else {
        jc_snprintf(out, cap, "%s: (not a key jichi knows how to classify, so it "
                    "waits for trust)", key);
    }
}

static void push_line(struct jc_vec *v, struct jc_arena *a, const char *s)
{
    char *c;
    if (v == NULL || a == NULL || s == NULL) {
        return;
    }
    c = jc_arena_strdup(a, s);
    if (c != NULL) {
        jc_vec_push(v, &c);
    }
}

/* Unlink `it` from `obj` without trusting `prev` links: our cJSON's first
 * child's prev is not maintained, so the predecessor is found by walking. */
static void detach(cJSON *obj, cJSON *it)
{
    cJSON *p = obj->child;
    cJSON *before = NULL;
    while (p != NULL && p != it) {
        before = p;
        p = p->next;
    }
    if (p == NULL) {
        return;
    }
    if (before == NULL) {
        obj->child = it->next;
    } else {
        before->next = it->next;
    }
    if (it->next != NULL) {
        it->next->prev = before;
    }
    it->next = NULL;
    it->prev = NULL;
}

void jc_cfgtrust_filter(cJSON *proj, cJSON *narrow, struct jc_vec *ignored,
                        struct jc_arena *a)
{
    cJSON *it;
    if (proj == NULL || !cJSON_IsObject(proj)) {
        return;
    }
    it = proj->child;
    while (it != NULL) {
        cJSON *next = it->next;
        const struct key_class *k = class_entry(it->string);
        if (k != NULL && k->cls == JC_CFGTRUST_SAFE) {
            it = next;
            continue;
        }
        detach(proj, it);
        if (k != NULL && k->cls == JC_CFGTRUST_NARROW && narrow != NULL) {
            /* Copy the key first: our cJSON_AddItemToObject frees the item's
             * own key before copying the new one, so passing it->string would
             * read freed memory. */
            char keybuf[96];
            jc_snprintf(keybuf, sizeof keybuf, "%s",
                        it->string != NULL ? it->string : "");
            cJSON_AddItemToObject(narrow, keybuf, it);
        } else {
            char line[900];
            jc_cfgtrust_describe(it->string, it, line, sizeof line);
            push_line(ignored, a, line);
            cJSON_Delete(it);
        }
        it = next;
    }
}

/* --- post-parse narrowing ------------------------------------------------------ */

static int *bool_field(struct jc_config *c, const char *key)
{
    if (strcmp(key, "assignments") == 0)      return &c->assignments;
    if (strcmp(key, "board") == 0)            return &c->board;
    if (strcmp(key, "configEditable") == 0)   return &c->config_editable;
    if (strcmp(key, "constraintsAutoAdopt") == 0) return &c->constraints_auto_adopt;
    if (strcmp(key, "control") == 0)          return &c->control_on;
    if (strcmp(key, "hooksEnabled") == 0)     return &c->hooks_enabled;
    if (strcmp(key, "inputHistory") == 0)     return &c->input_history;
    if (strcmp(key, "kineticAudit") == 0)     return &c->kinetic_audit;
    if (strcmp(key, "learnOnStop") == 0)      return &c->learn_on_stop;
    if (strcmp(key, "parallelVerify") == 0)   return &c->parallel_verify;
    if (strcmp(key, "privilegedAudit") == 0)  return &c->privileged_audit;
    if (strcmp(key, "revertOutOfScope") == 0) return &c->revert_out_of_scope;
    if (strcmp(key, "snapshots") == 0)        return &c->snapshots;
    if (strcmp(key, "strictGreen") == 0)      return &c->strict_green;
    if (strcmp(key, "typeAhead") == 0)        return &c->type_ahead;
    if (strcmp(key, "voice") == 0)            return &c->voice;
    return NULL;
}

static long *long_field(struct jc_config *c, const char *key, int **ifield)
{
    *ifield = NULL;
    if (strcmp(key, "maxToolIters") == 0)      { *ifield = &c->max_tool_iters; return NULL; }
    if (strcmp(key, "maxSubagentDepth") == 0)  { *ifield = &c->max_subagent_depth; return NULL; }
    if (strcmp(key, "maxSubagentIters") == 0)  { *ifield = &c->max_subagent_iters; return NULL; }
    if (strcmp(key, "maxParallelAgents") == 0) { *ifield = &c->max_parallel_agents; return NULL; }
    if (strcmp(key, "runTimeout") == 0)        return &c->run_timeout;
    if (strcmp(key, "memBudgetMb") == 0)       return &c->mem_budget_mb;
    if (strcmp(key, "scopedEditMax") == 0)     { *ifield = &c->scoped_edit_max; return NULL; }
    return NULL;
}

static void ignore(struct jc_vec *ignored, struct jc_arena *a, const char *key,
                   const cJSON *val)
{
    char line[900];
    jc_cfgtrust_describe(key, val, line, sizeof line);
    push_line(ignored, a, line);
}

static void apply_perm(struct jc_config *cfg, const cJSON *val,
                       struct jc_vec *ignored, struct jc_arena *a)
{
    const cJSON *deny;
    const cJSON *allow;
    if (val == NULL || !cJSON_IsObject(val)) {
        ignore(ignored, a, "permissions", val);
        return;
    }
    deny = jc_json_get_obj(val, "deny");
    allow = jc_json_get_obj(val, "allow");
    if (deny != NULL) {
        int b = 0;
        if (cJSON_IsArray(deny)) {
            const cJSON *it;
            for (it = deny->child; it != NULL; it = it->next) {
                if (cJSON_IsString(it) && it->valuestring != NULL) {
                    char *c = jc_arena_strdup(a, it->valuestring);
                    if (c != NULL) {
                        jc_vec_push(&cfg->permissions.deny, &c);
                    }
                }
            }
        } else if ((cJSON_IsString(deny) && deny->valuestring != NULL &&
                    strcmp(deny->valuestring, "*") == 0) ||
                   (item_bool(deny, &b) && b)) {
            cfg->permissions.deny_all = 1;
        }
    }
    if (allow != NULL) {
        char d[600];
        char line[800];
        int b = 0;
        if (cJSON_IsArray(allow)) {
            join_strings(allow, d, sizeof d);
            jc_snprintf(line, sizeof line, "permissions.allow: pre-approve %s, "
                        "so they run without asking", d[0] != '\0' ? d : "tools");
        } else if ((cJSON_IsString(allow) && allow->valuestring != NULL &&
                    strcmp(allow->valuestring, "*") == 0) ||
                   (item_bool(allow, &b) && b)) {
            jc_snprintf(line, sizeof line, "permissions.allow: pre-approve EVERY "
                        "tool, so none asks");
        } else {
            line[0] = '\0';
        }
        if (line[0] != '\0') {
            push_line(ignored, a, line);
        }
    }
}

void jc_cfgtrust_apply_narrowing(struct jc_config *cfg, const cJSON *narrow,
                                 struct jc_vec *ignored, struct jc_arena *a)
{
    const cJSON *it;
    if (cfg == NULL || narrow == NULL) {
        return;
    }
    for (it = narrow->child; it != NULL; it = it->next) {
        const struct key_class *k = class_entry(it->string);
        if (k == NULL) {
            continue;
        }
        switch (k->nk) {
        case NK_ON:
        case NK_OFF: {
            int *f = bool_field(cfg, k->key);
            int v = 0;
            int strict = (k->nk == NK_ON) ? 1 : 0;
            if (f == NULL || !item_bool(it, &v)) {
                ignore(ignored, a, k->key, it);
            } else if (v == strict) {
                *f = strict;          /* stricter, or equal: applied */
            } else if (*f != v) {
                ignore(ignored, a, k->key, it);   /* would loosen */
            }
            break;
        }
        case NK_MODE: {
            enum jc_agent_mode m;
            if (!cJSON_IsString(it) || it->valuestring == NULL ||
                !jc_agent_mode_parse(it->valuestring, &m)) {
                ignore(ignored, a, k->key, it);
            } else if (jc_perm_mode_rank(m) >=
                       jc_perm_mode_rank((enum jc_agent_mode)cfg->default_mode)) {
                cfg->default_mode = (int)m;
            } else {
                char line[200];
                jc_snprintf(line, sizeof line, "mode: switch the session to %s",
                            it->valuestring);
                push_line(ignored, a, line);
            }
            break;
        }
        case NK_POLICY: {
            int *f = (strcmp(k->key, "privilegedCommands") == 0)
                         ? &cfg->privileged_commands : &cfg->kinetic_commands;
            int p = policy_of(it);
            if (p < 0) {
                ignore(ignored, a, k->key, it);
            } else if (policy_rank(p) >= policy_rank(*f)) {
                *f = p;
            } else {
                char line[200];
                jc_snprintf(line, sizeof line, "%s: set the policy to \"%s\"",
                            k->key, it->valuestring);
                push_line(ignored, a, line);
            }
            break;
        }
        case NK_FENCE: {
            int *f = (strcmp(k->key, "pathFence") == 0) ? &cfg->path_fence
                                                        : &cfg->shell_fence;
            int v = 0;
            if (!item_bool(it, &v)) {
                ignore(ignored, a, k->key, it);
            } else if (fence_rank(v ? 1 : 0) >= fence_rank(*f)) {
                *f = v ? 1 : 0;
            } else {
                ignore(ignored, a, k->key, it);
            }
            break;
        }
        case NK_BOUND:
        case NK_BOUND0: {
            int *ifield = NULL;
            long *lfield = long_field(cfg, k->key, &ifield);
            double p = 0.0;
            long cur;
            long nv;
            int tighter;
            if ((ifield == NULL && lfield == NULL) || !item_num(it, &p) ||
                p < 0.0) {
                ignore(ignored, a, k->key, it);
                break;
            }
            nv = (long)p;
            cur = (ifield != NULL) ? (long)*ifield : *lfield;
            if (k->nk == NK_BOUND) {
                tighter = (nv <= cur);
            } else {
                /* 0 is OFF: no bound at all, the loosest value */
                tighter = (nv > 0) && (cur == 0 || nv <= cur);
                if (nv == 0 && cur == 0) {
                    tighter = 1;   /* equal: nothing changes */
                }
            }
            if (tighter) {
                if (ifield != NULL) {
                    *ifield = (int)nv;
                } else {
                    *lfield = nv;
                }
            } else {
                char line[200];
                if (nv == 0) {
                    jc_snprintf(line, sizeof line, "%s: remove the bound (0 is "
                                "off)", k->key);
                } else {
                    jc_snprintf(line, sizeof line, "%s: raise it to %ld",
                                k->key, nv);
                }
                push_line(ignored, a, line);
            }
            break;
        }
        case NK_PERM:
            apply_perm(cfg, it, ignored, a);
            break;
        default:
            ignore(ignored, a, k->key, it);
            break;
        }
    }
}

/* --- the store ------------------------------------------------------------------- */

void jc_cfgtrust_store_path(char *out, jc_size cap)
{
    jc_snprintf(out, cap, "%s/.jichi.d/trusted-configs.json", jc_home_dir());
}

static cJSON *store_read(void)
{
    char path[1100];
    char *data = NULL;
    jc_size len = 0;
    struct jc_arena *a = jc_arena_new(0);
    cJSON *root = NULL;

    jc_cfgtrust_store_path(path, sizeof path);
    if (a != NULL && jc_file_exists(path) &&
        jc_read_file(path, &data, &len, a) == JC_OK && data != NULL) {
        root = jc_json_parse(data);
        if (root != NULL && !cJSON_IsObject(root)) {
            cJSON_Delete(root);
            root = NULL;
        }
        if (root == NULL) {
            /* Fail closed: an unreadable store trusts nothing. */
            jc_logf(JC_LOG_WARN, "trust store %s is not a JSON object; no "
                    "project config is trusted until it is fixed or removed",
                    path);
        }
    }
    if (a != NULL) {
        jc_arena_free(a);
    }
    return root;
}

static jc_status store_write(const cJSON *root)
{
    char dir[1100];
    char path[1100];
    char *text;
    jc_status st;

    jc_snprintf(dir, sizeof dir, "%s/.jichi.d", jc_home_dir());
    if (jc_mkdir_p_private(dir) != JC_OK) {
        return JC_ERR_IO;
    }
    jc_cfgtrust_store_path(path, sizeof path);
    text = jc_json_print(root);
    if (text == NULL) {
        return JC_ERR_OOM;
    }
    st = jc_write_file_atomic(path, text, (jc_size)strlen(text));
    free(text);
    if (st == JC_OK) {
        st = jc_make_private(path);
    }
    return st;
}

int jc_cfgtrust_is_trusted(const char *abs_path, const char *sha_hex)
{
    cJSON *root;
    const cJSON *ent;
    const char *rec;
    int ok = 0;

    if (abs_path == NULL || sha_hex == NULL || sha_hex[0] == '\0') {
        return 0;
    }
    root = store_read();
    if (root == NULL) {
        return 0;
    }
    ent = jc_json_get_obj(root, abs_path);
    rec = (ent != NULL && cJSON_IsObject(ent))
              ? jc_json_get_str(ent, "sha256", NULL) : NULL;
    ok = (rec != NULL && strcmp(rec, sha_hex) == 0);
    cJSON_Delete(root);
    return ok;
}

jc_status jc_cfgtrust_add(const char *abs_path, const char *sha_hex)
{
    cJSON *root;
    cJSON *ent;
    jc_status st;
    char when[40];
    time_t now = time(NULL);
    struct tm *tmv = gmtime(&now);

    if (abs_path == NULL || sha_hex == NULL || sha_hex[0] == '\0') {
        return JC_ERR_INVALID;
    }
    root = store_read();
    if (root == NULL) {
        root = cJSON_CreateObject();
        if (root == NULL) {
            return JC_ERR_OOM;
        }
    }
    ent = cJSON_CreateObject();
    if (ent == NULL) {
        cJSON_Delete(root);
        return JC_ERR_OOM;
    }
    cJSON_AddStringToObject(ent, "sha256", sha_hex);
    if (tmv != NULL) {
        strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%SZ", tmv);
        cJSON_AddStringToObject(ent, "trustedAt", when);
    }
    if (jc_json_get_obj(root, abs_path) != NULL) {
        cJSON_ReplaceItemInObject(root, abs_path, ent);
    } else {
        cJSON_AddItemToObject(root, abs_path, ent);
    }
    st = store_write(root);
    cJSON_Delete(root);
    return st;
}

int jc_cfgtrust_remove(const char *abs_path)
{
    cJSON *root;
    cJSON *ent;
    int removed = 0;

    if (abs_path == NULL) {
        return 0;
    }
    root = store_read();
    if (root == NULL) {
        return 0;
    }
    ent = (cJSON *)jc_json_get_obj(root, abs_path);
    if (ent != NULL) {
        detach(root, ent);
        cJSON_Delete(ent);
        removed = 1;
        if (store_write(root) != JC_OK) {
            removed = -1;
        }
    }
    cJSON_Delete(root);
    return removed;
}
