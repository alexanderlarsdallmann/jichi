# Learning game development with jichi — Godot, and a gate that runs the game

**For a self-learner.** You will point jichi at Godot's own documentation, build a
project it can read, and work through the official tutorial with jichi as a tutor
that **cites the documents instead of its memory** — and, unlike every reading
course in this tree, with a gate that **runs your code in the real engine** and
tells you whether it worked.

Every command on this page was run in the form shown, on 2026-09-21, against
**Godot 4.7.2** (`4.7.2.stable.official.ed1daf0bf`). Where a number appears it was
measured on this machine, uncapped.

> **New to this?** [`LANGUAGE_COURSE.md`](LANGUAGE_COURSE.md) is the same shape for
> a programming language and explains the corpus method in more detail; this page
> assumes it only where it saves repetition. [`VOCABULARY.md`](VOCABULARY.md)
> defines the terms — *corpus*, *snapshot*, *anchor*, *gate* — before using them.

## 0. Why game development is different from the reading courses

Every other course in this curriculum grades **structure**: did you write the five
sections, do your citations resolve, is the objection before the reply. Those are
honest floors and they are all a script can check when the subject is prose.

Godot changes that, and it is the whole reason this page exists. The engine runs
**headless**, a script's exit code is yours to set, and a scene can be loaded and
inspected without a window. So the question *"did it work?"* stops being a matter
of judgement:

```console
$ $GODOT --headless --path ~/games/first --script res://tests/harness.gd
ok - sum to 4 is 10
ok - sum to 1 is 1
ok - sum to 0 is 0
3 check(s), 0 failed
$ echo $?
0
```

**213 ms**, measured. That is a real gate — the thing the reading tasks cannot
have. (`$GODOT` is the engine binary; §1 sets it.)

## 1. Get the engine and the documents

You need three things, and all of them are free and official.

**The engine.** Download the Linux build from godotengine.org. It is a single
executable; there is no install step. Check it runs headless before anything else:

```console
$ ./Godot_v4.7.2-stable_linux.x86_64 --headless --version
4.7.2.stable.official.ed1daf0bf
```

Put it somewhere stable and remember the path. Everything below writes it as
`$GODOT`:

```sh
GODOT=/home/<you>/godot/Godot_v4.7.2-stable_linux.x86_64
```

**The documentation**, as a local snapshot you own:

```sh
git clone --depth 1 https://github.com/godotengine/godot-docs
```

**The demo projects**, which are the worked examples you will want at section 5:

```sh
git clone --depth 1 https://github.com/godotengine/godot-demo-projects
```

### What is in there, measured

| Directory | `.rst` files | Text |
|---|---|---|
| `getting_started/` | **33** | **283,723 bytes** — the tutorial |
| `tutorials/` | 398 | 4,570,048 bytes — the how-to guides |
| `classes/` | 1,079 | 27,580,218 bytes — the API reference |

**Index `getting_started` and nothing else, to begin with.** This is the finding
[`LANGUAGE_COURSE.md`](LANGUAGE_COURSE.md) made for Python and it holds here with
room to spare: 0.28 MB against 26 MB is not a tuning detail. Embedding the class
reference to answer *"what is a signal?"* is the difference between a course you
start and one you abandon. Add `classes/` later, as a **second** source, when you
are writing real code against the API.

## 2. Make the project

Two files. A Godot project is a directory containing `project.godot`:

```sh
mkdir -p ~/games/first/tests
cd ~/games/first
printf 'config_version=5\n\n[application]\n\nconfig/name="first"\n' > project.godot
```

Then the jichi config beside it, `.jichi/config.json`:

```json
{
  "models": [
    { "name": "chat", "provider": "openai", "model": "<your chat model>",
      "apiBase": "http://127.0.0.1:1234/v1", "apiKey": "lm-studio",
      "roles": ["chat"] },
    { "name": "embed", "provider": "openai",
      "model": "text-embedding-nomic-embed-text-v1.5",
      "apiBase": "http://127.0.0.1:1234/v1", "apiKey": "lm-studio",
      "roles": ["embed"] }
  ],
  "docs": [
    { "name": "godot-start",
      "path": "/home/<you>/godot-docs/getting_started" }
  ],
  "testCommand": "$GODOT --headless --path . --script res://tests/harness.gd"
}
```

The `embed` role is what makes the documents searchable; without it `docs index`
has nothing to call. `testCommand` is what makes jichi's own verify gate run the
engine — see §6.

## 3. Check where you are

```console
$ jichi docs
Configured documentation sources:
  godot-start      /home/<you>/godot-docs/getting_started
```

That listing is **offline** — it proves the config parsed, not that anything works
yet. `jichi doctor` is the fuller answer and will tell you if the embed model is
unreachable.

## 4. Build the index, and ask it something

```console
$ jichi docs index godot-start
godot-start: 34 files, 209 chunks (209 embedded, 0 reused)
```

**2 seconds**, measured — but read §7 first if it is slow, because the number that
matters here is not the one you will get with a chat model resident.

```console
$ jichi docs search godot-start "what is a signal and how do I connect one"
```

**~180 ms**, and the first hit is
`getting_started/step_by_step/signals.rst:1-36` — the file and the line range.
A second query, *"how do I make a node move when a key is pressed"*, took the same
**~180 ms** and landed on `first_2d_game/03.coding_the_player.rst:136-187`.

*(Eight runs of the two queries, 165–186 ms, measured uncapped in exactly the form
printed above. An earlier probe of the same corpus through a different binary and
an explicit `--config` came out at 73 ms; I could not account for the gap — it is
**not** `repoMap`, which I tested and which changed nothing — so the number
published is the one the published command produces. An unexplained difference is
not a licence to quote the faster half of it.)*

**The `file:line` anchor is the point of the whole exercise.** It is what lets you
answer *"where does this come from?"* with a path instead of a feeling, and it is
what [`GROUNDED_DISCOURSE.md`](GROUNDED_DISCOURSE.md) means by *grounds that
resolve*. When jichi tells you something about Godot, ask it which file said so,
and open that file.

## 5. Work through a section with jichi

Start jichi in the project directory. The loop that works:

1. **Read the tutorial section yourself first.** `getting_started/step_by_step/`
   in order; each page is short.
2. **Predict before you ask.** Say what you think the answer is, then ask jichi.
   A tutor that confirms is worth less than one that corrects, and you cannot be
   corrected on a prediction you did not make.
3. **Make it cite.** `@docs:godot-start` puts the corpus in front of the model;
   *"which file says that?"* is the follow-up that keeps it honest.
4. **Write the test before the feature** — §6 is what makes this possible here
   and not in the reading courses.
5. **Compare against a demo.** `godot-demo-projects` holds a working version of
   most things the tutorial teaches. Reading a real one after writing your own is
   worth more than reading it before.

## 6. The verification surface — what "it worked" means

This is the section to get right, and it contains one trap that will silently
ruin every test you write if you do not know it.

### The harness

```gdscript
extends SceneTree

var failures := 0

func check(name: String, got, want) -> void:
	if got == want:
		print("ok - %s" % name)
	else:
		failures += 1
		print("not ok - %s: got %s, want %s" % [name, got, want])

func _init() -> void:
	check("sum to 4 is 10", _sum_to(4), 10)
	check("sum to 1 is 1", _sum_to(1), 1)
	print("%d check(s), %d failed" % [3, failures])
	quit(1 if failures > 0 else 0)
```

Run it, from anywhere — `--path` means you do not have to be in the directory:

```console
$ $GODOT --headless --path ~/games/first --script res://tests/harness.gd
$ echo $?
0
```

Break the implementation and it reports **rc=1** with `not ok` lines naming what
it got and what it wanted. That is a gate.

### The trap: `quit()` is a request, not a `return`

**`SceneTree.quit()` does not stop your function.** It asks the engine to shut
down after the current frame; execution continues to the next line. So this,
which is how everybody writes it the first time, **always exits 0**:

```gdscript
func _init() -> void:
	if got != want:
		push_error("expected %s, got %s" % [want, got])
		quit(1)          # <- does NOT return
	print("ok")           # <- runs anyway
	quit(0)               # <- and this one wins
```

Measured, on exactly that shape: the run printed **both** `ERROR: expected 10, got
10` **and** `ok - _sum_to(4) == 10`, and exited **0**. A test that reports success
while failing is worse than no test, and this one looks completely correct.

Two ways out, and the second is better:

- `return` immediately after `quit(n)`;
- **count failures and call `quit()` exactly once, as the last statement** — the
  harness above. One exit point cannot be overwritten by a later one.

*(Confirmed directly: `quit(1)` then `print()` then `quit(0)` prints the line and
exits 0; `quit(1)` then `return` exits 1; `quit(3)` exits 3.)*

### A second, cheaper gate: `--check-only`

Godot will type-check a script without running it:

```console
$ $GODOT --headless --path ~/games/first --check-only --script res://tests/bad.gd
SCRIPT ERROR: Parse Error: Cannot assign a value of type "String" as "int".
$ echo $?
1
```

Use it as a fast first gate — it catches a whole class of mistake before anything
executes, and it costs nothing.

### Scenes load headless too

```gdscript
var ps = load("res://Main.tscn")
var n = ps.instantiate()
print("SCENE_OK name=", n.name, " type=", n.get_class())
```

Measured: `SCENE_OK name=Main type=Node2D`. So *"the scene still loads"* is a
checkable property, not an opinion — useful as a regression gate once you have
more than one scene.

## 7. When it goes wrong

**Indexing hangs, or the embedding endpoint looks broken.** Suspect VRAM before
you suspect the software. Measured on a 16 GB card, one embedding request,
`text-embedding-nomic-embed-text-v1.5`:

| Resident | Same request |
|---|---|
| a 12B chat model also loaded (14.7 / 17.2 GB used) | **493,000 ms** |
| the embedder alone (4.5 / 17.2 GB used) | **20 ms** |

Four orders of magnitude. **On a card this size, index with the chat model
unloaded, then load the chat model.** And note what the first measurement looked
like through a 300-second timeout: `http=000`, which reads as *the endpoint is
broken* and is not. If you are timing something, do not cap it — the cap does not
hide the answer, it invents a different one.

**`docs search` returns nothing useful.** Check the index exists
(`jichi docs index godot-start` prints its chunk count) and that you indexed
`getting_started` rather than the whole of `godot-docs`.

**The test passes when it should not.** Read §6's trap again; it is almost always
that.

**jichi cannot read the engine source.** It does not need to, and pointing it at
86,451 commits of C++ will not help you learn GDScript. Add `classes/` as a docs
source when you need the API; leave the engine source alone until you have a
reason.

## 8. Where to go next

- [`LANGUAGE_COURSE.md`](LANGUAGE_COURSE.md) — the same method for a programming
  language, with the Python track already graded.
- [`GROUNDED_DISCOURSE.md`](GROUNDED_DISCOURSE.md) — the discipline for the
  conversation itself: making the model's claims resolve to something you can open.
- [`DOCS.md`](DOCS.md) — the full reference for docs sources, caching, and the
  three ways to reach them.
- `godot-demo-projects` — after the tutorial, read one all the way through.

---

*Companion pages: [LANGUAGE_COURSE.md](LANGUAGE_COURSE.md) · [DOCS.md](DOCS.md) ·
[GROUNDED_DISCOURSE.md](GROUNDED_DISCOURSE.md) · [CURRICULUM.md](CURRICULUM.md)*
