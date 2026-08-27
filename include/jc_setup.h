/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_setup.h - interactive project setup wizard, pure core (M48).
 *
 * The `jichi setup` wizard takes a fresh directory to a working, validated,
 * role-tailored project: it scaffolds role-appropriate `.jichi/` assets (reusing
 * jc_scaffold packs), writes a config, emits helper start-scripts, and runs a
 * doctor-style validation pass. This header is the *pure* core (no I/O): the
 * compiled-in role-preset table, the config builder, and the start-script text
 * builder. The interactive prompting + file writing live in src/main.c's
 * run_setup shell, so this core is unit-tested offline (mirrors jc_scaffold).
 */
#ifndef JC_SETUP_H
#define JC_SETUP_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_str.h"

/* Default-feature bitmask on a preset (and the wizard's optional toggles). */
#define JC_SF_SNAPSHOTS  0x001u
#define JC_SF_LSP        0x002u
#define JC_SF_TESTCMD    0x004u
#define JC_SF_DOCS       0x008u
#define JC_SF_EMBED      0x010u
#define JC_SF_REFERENCES 0x020u
#define JC_SF_VERIFY     0x040u
#define JC_SF_HOOKS      0x080u
#define JC_SF_WEB        0x100u
#define JC_SF_ROUTING    0x200u
#define JC_SF_TELEMETRY  0x400u
#define JC_SF_LOWRES     0x800u  /* small-model profile: lowResource + small ctx */
#define JC_SF_LEANHOST  0x2000u  /* M326p: the HOST is small -- lowResource and
                                  * one parallel agent, but the context window
                                  * left alone. Distinct from JC_SF_LOWRES,
                                  * which also shrinks contextLimit because
                                  * there the MODEL is what is small. */
#define JC_SF_ASSIGN    0x1000u  /* teaching: assignments pack + "assignments": true
                                  * in the config (the hint/help tools register,
                                  * the /assign flow is on). M173/C1. */

/* Which flavour of start-script a role gets. */
enum jc_setup_profile {
    JC_SETUP_TUI = 0,  /* plain interactive TUI            */
    JC_SETUP_PLAN,     /* --plan (investigate, propose)    */
    JC_SETUP_AUTO,     /* --auto (autonomous, fix-forward) */
    JC_SETUP_TEST      /* --auto, seeded to run the tests  */
};

/* A role preset: a recipe composing an existing scaffold pack with config +
 * feature defaults + a start-script profile. Compiled-in (static storage). */
struct jc_setup_preset {
    const char *name;          /* "developer", "technical-writer", ...        */
    const char *description;
    const char *scaffold_pack; /* an existing jc_scaffold pack name           */
    const char *output_style;  /* default outputStyle name, or NULL           */
    const char *mode;          /* default mode ("plan"/"auto"), or NULL=chat  */
    unsigned    features;      /* JC_SF_* defaults                            */
    int         profile;       /* enum jc_setup_profile                       */
    int         asks_language; /* 1 => the wizard swaps in a language pack    */
    /* M326k: WHICH QUESTION this entry answers. Three axes, because the
     * "who are you?" list was conflating them: seven professions, two life
     * contexts, a domain, an opt-out -- and `small-local`, which is not a
     * person at all but a statement about your hardware, and composes with
     * every one of the others.
     *
     * An enum rather than a bag of flags: an entry answers exactly one
     * question, and making that structural is cheaper than documenting it.
     *
     * M326m sorted two more: `tester` and `reviewer` were never roles either.
     * They differ from the entries around them only in mode and start script
     * -- that is, in what you are DOING -- exactly like `refactor` and
     * `contributor`, which were already journeys. And `learner`/`instructor`
     * are a stance: not what you build or what you are doing to it, but how
     * you are working on it, which is orthogonal to both. */
    int         axis;          /* enum jc_setup_axis */
    /* One line on WHY you would choose this, for the wizard's menus (M326n).
     * NULL where the description already carries it. Editorial judgement, so
     * hand-written -- unlike the config keys, which are derived from
     * `features` and therefore cannot drift. */
    const char *why;
};

/* Render the config keys a preset writes, DERIVED from its feature bitmask so
 * the wizard's explanation cannot drift from its behaviour (M326n). Used to
 * teach the file format while filling it in: a learner who picks an option has
 * already seen the key they would have typed by hand. Pure; unit-tested. */
void jc_setup_preset_sets(const struct jc_setup_preset *p, char *buf,
                          jc_size cap);

int                            jc_setup_preset_count(void);
const struct jc_setup_preset  *jc_setup_preset_at(int i);
const struct jc_setup_preset  *jc_setup_find_preset(const char *name);

/* Collected wizard answers (interactive prompts or CLI flags both fill this).
 * String fields point into caller-owned storage (argv / arena) for the build's
 * duration. NULL/empty fields are simply omitted from the output. */
#define JC_SETUP_MAX_LSP  4
#define JC_SETUP_MAX_MCP  4
#define JC_SETUP_MAX_DOCS 4

struct jc_setup_lsp  { const char *name; const char *command; const char *extensions; };
struct jc_setup_mcp  { const char *name; const char *command; const char *args; };
struct jc_setup_docs { const char *name; const char *path; };

/* Which of the wizard's questions a preset answers (M326k). */
enum jc_setup_axis {
    JC_AXIS_ROLE = 0,   /* who you are            -- exactly one, required   */
    JC_AXIS_JOURNEY,    /* what you are walking into -- optional, layers on  */
    JC_AXIS_MACHINE,    /* what you are running on   -- optional, layers on  */
    JC_AXIS_STANCE      /* how you are working       -- learner/instructor    */
};

/* Where the API key ended up (M326f). The wizard used to close by telling every
 * user to `export` it, which is wrong twice over once it can store the key: it
 * nags about work already done, and the bare export does not survive a new
 * terminal (nor reach a cron/systemd run, whose non-interactive shell never
 * reads ~/.bashrc). */
enum jc_setup_key_state {
    JC_SETUP_KEY_NONE = 0,   /* not stored: the user must supply it            */
    JC_SETUP_KEY_STORED,     /* written to ~/.jichi.env; the script loads it   */
    JC_SETUP_KEY_ENV         /* already exported in the environment            */
};

struct jc_setup_answers {
    /* required: the chat model */
    const char *provider;      /* "anthropic" | "openai"                      */
    const char *model;         /* model id                                    */
    const char *api_key_env;   /* env-var NAME holding the key (never literal)*/
    const char *api_base;      /* optional override                           */
    const char *model_name;    /* optional display name                       */
    long        context_length; /* model context window in tokens; 0 => omit.
                                * jichi budgets against this and otherwise
                                * assumes JC_COMPACT_DEFAULT_LIMIT (32000),
                                * which is where an under-declared window
                                * comes from. */
    /* top-level knobs (tri-state ints: -1 unset / 0 off / 1 on) */
    const char *mode;          /* "plan"/"auto"/NULL                          */
    const char *output_style;  /* name or NULL                                */
    const char *test_command;  /* or NULL                                     */
    const char *verify;        /* verifier command or NULL                    */
    int         snapshots;
    int         references;
    int         repo_map;
    int         hooks;
    const char *log_level;     /* "metrics" / "full" / NULL                   */
    /* optional embed model (enables codebase_search / docs retrieval) */
    const char *embed_model;   /* model id or NULL                            */
    /* routing tiers (selectors) */
    const char *route_fast;
    const char *route_strong;
    /* M326q: OS-appropriate commands, offered as DEFAULTS to opt-in prompts
     * rather than written unconditionally -- a `sound` key registers the
     * play_audio/record_audio tools, so seeding it silently would advertise
     * two mutating tools to every project. */
    const char *sound_play;
    const char *notify_cmd;
    /* web search */
    const char *search_url;
    const char *search_key_env;
    /* multi-entry subsystems */
    struct jc_setup_lsp  lsp[JC_SETUP_MAX_LSP];   int nlsp;
    struct jc_setup_mcp  mcp[JC_SETUP_MAX_MCP];   int nmcp;
    struct jc_setup_docs docs[JC_SETUP_MAX_DOCS]; int ndocs;
    /* machine profile (from CPU/RAM detection; 0 => omit the key) */
    int          max_parallel;   /* maxParallelAgents (0 => omit)            */
    int          low_resource;   /* emit "lowResource": true when > 0        */
    int          assignments;    /* tri-state: emit "assignments" when >= 0   */
    long         context_limit;  /* contextLimit (0 => omit)                 */
    /* rewrite journey (M183): the codebase being ported FROM, emitted as a
     * one-entry "referenceRoots" array -- readable through the fence, never
     * writable (M54). NULL => omit. */
    const char  *reference_root;
    /* M326f: what became of the API key during this run -- so the closing
     * validation and next-steps do not tell the user to export a key the
     * wizard just stored for them. Set only by the interactive path; the
     * flag-driven one leaves it NONE, which is the pre-M326f behaviour.
     * Not part of the config: jc_setup_build_config ignores it. */
    enum jc_setup_key_state key_state;
};

/* Zero an answers struct and set the tri-state knobs to "unset" (-1). */
void jc_setup_answers_init(struct jc_setup_answers *a);

/* Apply a preset's defaults onto `a` (mode, outputStyle, and the feature
 * bitmask -> snapshots/references/test_command/embed/.../search placeholders).
 * Required model fields are left untouched. */
void jc_setup_apply_preset(struct jc_setup_answers *a,
                           const struct jc_setup_preset *p);

/* Complexity level layered over a role preset (M116). */
enum jc_setup_complexity {
    JC_SETUP_STD = 0,   /* the role's own defaults */
    JC_SETUP_BEGINNER,  /* minimal + safe: snapshots on, power-user subsystems off */
    JC_SETUP_ADVANCED   /* everything sensible on */
};

/* Adjust the answers for a complexity level (applied AFTER jc_setup_apply_preset):
 * BEGINNER keeps the safety net (snapshots) + references and turns OFF hooks /
 * routing / web / telemetry / auto-mode; ADVANCED turns ON references + hooks +
 * telemetry (routing is left to the preset -- it needs two real models). STD is a
 * no-op. Never touches model fields. Pure; unit-tested. */
void jc_setup_apply_complexity(struct jc_setup_answers *a, int complexity);

/* Parse "beginner"/"advanced"/"standard" (or "std") into *out. 1 on success,
 * 0 on an unknown name. Pure. */
int jc_setup_complexity_parse(const char *s, int *out);

/* Build the config JSON for `a` and append it (human-readable, indented) to
 * `out`. The chat model gets roles chat/edit/apply; an embed model (if set)
 * gets roles embed/rerank. Secrets are NEVER emitted: only `apiKeyEnv`. A
 * leading `_comment` points at the docs. Returns JC_ERR_INVALID if `provider`
 * or `model` is missing, else JC_OK. Pure. */
jc_status jc_setup_build_config(const struct jc_setup_answers *a,
                                struct jc_sb *out);

/* Merge `a` into an existing config JSON and append the (re-serialized) result
 * to `out`. Gap-fill semantics: existing top-level keys and models are
 * preserved untouched; only pieces the config lacks are added — the chat/embed
 * model is added only if no existing model already holds that role, and each
 * top-level key only if absent. Returns JC_ERR_PARSE if `existing_json` is not a
 * JSON object, JC_ERR_INVALID if `a` lacks provider/model. Pure (M53). */
jc_status jc_setup_merge_config(const char *existing_json,
                                const struct jc_setup_answers *a,
                                struct jc_sb *out);

/* Build a project config that INHERITS from an existing (global) config: start
 * from `source_json`, optionally restricted to a subset of top-level keys
 * (`inherit_keys` = comma-separated names, e.g. "models,routing"; NULL/empty =
 * the whole config), then gap-fill with `a` (only pieces still missing). Unlike
 * jc_setup_merge_config this does NOT require `a` to carry a model — the source
 * usually supplies one — so a pure `setup --from-global` (no --model) works.
 * Returns JC_ERR_PARSE if source_json isn't a JSON object. Pure (W5). */
jc_status jc_setup_inherit_config(const char *source_json,
                                  const char *inherit_keys,
                                  const struct jc_setup_answers *a,
                                  struct jc_sb *out);

/* Append the text of a POSIX `sh` start-script for `preset` that runs
 * `jichi --config <config_path>` with the profile's flags. Pure. */
void jc_setup_start_script(const struct jc_setup_preset *preset,
                           const char *config_path, struct jc_sb *out);

/* The default start-script filename for a preset's profile ("run.sh",
 * "task.sh", "review.sh", "test.sh"). Never NULL. Pure. */
const char *jc_setup_script_name(const struct jc_setup_preset *preset);

/* Map a source-file extension (no leading dot; case-insensitive) to the name of
 * the language scaffold pack it implies ("c-cli"/"python-cli"/"zig-cli"/
 * "godot"), or NULL if the extension isn't a recognized primary language. Used
 * by the wizard to auto-detect a project's language (M52). Pure. */
const char *jc_setup_lang_for_ext(const char *ext);

#ifdef __cplusplus
}
#endif
#endif /* JC_SETUP_H */
