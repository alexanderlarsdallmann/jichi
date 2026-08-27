# Tsuiseki 3 — The run that never reaches the network

*[追跡（ついせき）*Tsuiseki* — the traced run](TSUISEKI.md) · chapter 3 of 4*

## Why this exists

Chapters 1 and 2 both traced *turns*, and a reader could be forgiven for
thinking jichi is the agent loop with some plumbing attached. It is not. Most
of the binary is code that never meets a model: config resolution, the
workspace walk, the session store, the index, `doctor`, the exporters, the
TUI. This chapter traces one of those runs end to end — `jichi map`, which
walks the workspace, extracts each file's top-level symbols, prints them, and
exits.

Two things come out of it. One is a shape: what a jichi run looks like with the
provider removed. The other is a question this series has to answer sooner or
later — **which runs can be recorded at all?** Not all of them can, and the
reason is worth more than the trace.

## The run

```sh
# in the jichi checkout
sh docs/reading/traces/capture.sh no-network
```

No reply table, because there is no model:

```sh docs/reading/traces/no-network/trace.sh
NEEDS_MODEL=0
```

```sh docs/reading/traces/no-network/trace.sh
run_trace() {
    "$BIN" --config "$CONFIG" map
}
```

The workspace `trace.sh` seeds is a miniature project chosen to make the map
show its edges: two languages, a header, a subdirectory, a file with no
symbols in it, and filenames whose alphabetical order is not the order they
are created in.

**How the trace proves the network was never touched.** `capture.sh` writes a
config either way, and for a `NEEDS_MODEL=0` trace it points the endpoint at
`http://127.0.0.1:1/v1` — port 1, where nothing listens. A run that dialled
its model would fail there, loudly, the way chapter 2's third exercise does.
So the evidence is negative and it is real: the run completed, `stderr` is
empty, `exit_status` is 0, and no request artifact exists.

## What came out

```text docs/reading/traces/no-network/expected/stdout.txt
## Repository map
A high-level index of the project's source files and their top-level symbols, to help you navigate. It is heuristic and may be incomplete; use read_file / find_definition / search_code for detail.

main.c: helper, main
src/b.c: b_one, b_two
tool.py: hello
util.h: point
```

Read the output as claims and check each against the seeded workspace.

**`README.md` is absent, and not for the reason you would guess.** It is not
"no symbols, no line" — a `.c` file with nothing in it but a comment still gets
a line, with an empty symbol list (exercise 2 shows this). The filter is the
*extension*, applied during the walk, before any file is read:

```c src/index/jc_repomap.c:rm_walk
        } else if (lang_of(path_ext(full)) != RL_NONE &&
                   jc_file_size(full) <= RM_MAX_FILE_BYTES) {
```

`lang_of` knows C, C++, Python, Go, Rust, JavaScript/TypeScript, Java, Ruby,
shell, Racket/Scheme and more; `.md` is not among them, so a markdown file is
never even opened. The second half of that condition is the other thing to
notice: a source file above a size cap is skipped **entirely**, which is a
sentence worth remembering the next time a map does not mention the one file
you were looking for.

**`util.h` lists `point`, not `helper`.** The header declares
`int helper(int x);` and defines `struct point`. The map lists what a file
*provides*, and a prototype is a promise about something defined elsewhere —
which is why `helper` appears under `main.c`, where its body is.

**The list is sorted, and that is load-bearing.** main.c, src/b.c, tool.py,
util.h is alphabetical, not the order `seed_workspace` created
them in and not the order the filesystem hands them back. Directory order
differs between filesystems and between machines; if it leaked into this
output, this trace could not be committed at all, because a re-take on your
machine would diff against mine for a reason that has nothing to do with
jichi. One line prevents that:

```c src/index/jc_repomap.c:render_listing
    qsort(files.data, files.len, sizeof(char *), path_cmp);
```

That is the difference between a run you can record and a run you cannot, and
it was not put there for this chapter's benefit — a map whose order wobbled
would also change the *system prompt* between two otherwise identical turns,
which is a prompt-cache miss and a diff nobody can read.

**The prose at the top is part of the artifact.** "It is heuristic and may be
incomplete" is addressed to the model, not to you: the map is a navigation
aid, and a model that trusts it as a complete index will conclude a symbol
does not exist when the extractor merely did not recognise it. Compare the
`read_file` gutter warning in chapter 1 — the same habit of telling the reader
what the data is *not*.

## The code, and the two lifetimes of one string

The dispatch is in `main()`, and its comment is the chapter title:

```c src/main.c:main
    /* `map` subcommand: print the repository map (no provider or network). */
    if (args.npos > 0 && strcmp(args.pos[0], "map") == 0) {
```

The subcommand itself is eleven lines:

```c src/main.c:run_map
static int run_map(struct jc_app *app)
{
    char *map = jc_repomap_render(app);
    if (map == NULL) {
        printf("(no recognised source files in this workspace)\n");
        return 0;
    }
    printf("%s", map);
    free(map);
    return 0;
}
```

`jc_repomap_render` returns a `malloc`'d string; `run_map` prints it and frees
it. Total lifetime: microseconds. Now read the *other* caller of the same
renderer:

```c src/index/jc_repomap.c:jc_repomap_build
char *jc_repomap_build(struct jc_app *app)
{
    char *rendered = jc_repomap_render(app);
    char *result;

    if (rendered == NULL) {
        return NULL;
    }
    result = jc_arena_strdup(app->arena, rendered);
    free(rendered);
    return result;
}
```

Same text, copied into the **session arena** and the heap copy freed. That is
the ownership question this series promised to make concrete: not "who calls
what", but *how long does this string need to exist, and who is responsible
for it?* Two answers, both correct, decided by what happens next —

- printed and discarded → `malloc`/`free`, and the shortest possible life;
- put in the system prompt → the session arena, because it will be re-sent on
  **every** request for the rest of the session, and a per-turn `strdup` of it
  would be a leak with a slow fuse.

Here is where it lands when a turn is involved:

```c src/chat/jc_sysmsg.c:jc_sysmsg_build_parts
        append_capped(&sb, app->repo_map, map_cap, "repository map");
```

Which closes a loop back to chapter 1. That trace's config said
`"repoMap":false`, so its 2,594-byte system message contained **no map**. Turn
the flag on and this chapter's output — capped, because a large repository's
map would eat the context window — becomes part of every request in chapters 1
and 2. The `map` subcommand is how you read that contribution on its own,
before deciding whether to pay for it.

[Fukabori 3](fukabori-03-the-three-arena-lifetime-model.md) argues the arena
model; this is what one of its decisions looks like from the outside.

## The run that could not be a trace

The obvious candidate for this chapter was `doctor`. It is the command whose
whole purpose is to run offline and tell you what is wrong. It is also
unrecordable, for two measured reasons — and finding that out is the most
useful thing in this chapter.

**It reports your machine.** Here is part of what it printed on the machine
that wrote this page:

```
✓ platform
    Linux 6.8.0-137-lowlatency (x86_64)
✓ state root
    /tmp/probe4/home
! active model id was DEFAULTED, not configured
    the config names no "model" for the active entry, so 'claude-opus-4-8' was substituted from the built-in default for provider 'anthropic'.
```

A kernel string, a path, a core count, a RAM figure, a libcurl verdict. Every
one of those is the *point* of doctor and the ruin of a byte-diff: the
committed artifact would fail on the next machine, correctly, and the gate
would be teaching people to ignore it.

**And it is not offline.** `docs/DOCTOR.md` says it plainly — everything in a
plain `doctor` run "is offline **or a bare reachability probe**" — and the
probe is visible in that output as `✓ model server reachable`. So the command
whose name means "check without doing anything" opens a TCP connection to
whatever endpoint your config names. Nothing is spent and no model is called
(`doctor --live` is the opt-in that does), but "never reaches the network" is
false, and this chapter would have been built on it.

Note what happened here: two claims about a command, both plausible, both
wrong, and both cheap to check by *running it and reading the output*. That is
the method the whole guide is for.

So `map` is the committed trace and `doctor` is the exercise. The boundary is
worth stating as a rule: **a run can be recorded when its output depends only
on its input.** Machine facts, wall-clock times, PIDs, and anything that
dials out put a run on the other side of that line. Such runs still deserve to
be read — they just cannot be diffed, and a series that pretended otherwise
would be selling a gate it does not have.

## Prove it to yourself

**1. Re-take it and diff.**

```sh
# in the jichi checkout
sh docs/reading/traces/capture.sh no-network /tmp/nn
diff -r docs/reading/traces/no-network/expected /tmp/nn && echo identical
```

**2. Add a file and predict the output before you look.** Copy the trace, add
a `zeta.c` with two functions and an `alpha.c` with none, re-take, and check
both your prediction and the ordering.

```sh
# in the jichi checkout
cp -r docs/reading/traces/no-network /tmp/nn2 && rm -rf /tmp/nn2/expected
printf 'seed_workspace() {\n    printf "int one(void){return 1;}\\nint two(void){return 2;}\\n" > zeta.c\n    printf "/* just a comment */\\n" > alpha.c\n}\n' >> /tmp/nn2/trace.sh
sh docs/reading/traces/capture.sh /tmp/nn2 /tmp/nn2-out
cat /tmp/nn2-out/stdout.txt
```

(The appended `seed_workspace` replaces the sourced one, so this workspace has
only those two files — a smaller question than the committed trace, on
purpose.)

**3. Run the unrecordable one.** `./jichi doctor` in the checkout, then read
your own output for the lines that could never be committed. Two cautions,
both real: it opens a connection to whatever endpoint your config names, and
with no config at all it *defaults* a model id — on this machine an
`anthropic` one, which is exactly the kind of silent selection
`docs/LOCAL_MODELS.md` and this project's spending rule exist to prevent. Read
the warning it prints about that.

**4. Make the map appear in a turn — and be wrong first.** `REPO_MAP=true` in
a trace's `trace.sh` switches the injection on. Predict what it does to
chapter 1's system message, then do it:

```sh
# in the jichi checkout
cp -r docs/reading/traces/tool-round /tmp/withmap && rm -rf /tmp/withmap/expected
printf 'REPO_MAP=true\n' >> /tmp/withmap/trace.sh
sh docs/reading/traces/capture.sh /tmp/withmap /tmp/withmap-out
head -4 /tmp/withmap-out/shape
```

`[0] system content=2594` — **exactly as before**. Nothing was injected, and
if you predicted a growth you have just met the fact this chapter opened with:
chapter 1's workspace is one text file, no source file, so `jc_repomap_render`
found nothing to render and returned `NULL`. There is no map to pay for.

Now give it something to index — append a `seed_workspace` that also writes a
`main.c` (a later definition replaces the sourced one) and re-take. The system
message goes to **2,833**: +239 bytes for a two-symbol map, charged on
**every** request of the run, not once. That is chapter 1's `growth`
arithmetic applied to a fixed cost — and the reason
[`docs/REPOMAP.md`](../REPOMAP.md) has a limit knob at all.

**5. The reading exercise.** `jc_repomap_scan` is the symbol extractor. Find
how it decides what a top-level symbol is for C, and then for Python, and
find the case that made "heuristic and may be incomplete" the honest wording.
Then answer: why does the map list `point` for `util.h` but not `x` and `y`?

## Where to go next

- [Chapter 4](tsuiseki-04-the-call-that-was-wrong.md): back to the agent loop,
  for the run whose answer contradicts its own trace.
- [Annai 7](annai-07-memory-the-jichi-way.md) for the arenas from zero, then
  [Fukabori 3](fukabori-03-the-three-arena-lifetime-model.md) for the argument
  behind the two lifetimes above.
- `docs/REPOMAP.md` for what the map is for in a real session, and
  `docs/DOCTOR.md` for the command this chapter could not record.
