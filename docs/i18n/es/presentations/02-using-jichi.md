---
marp: true
title: Usando jichi
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/02-using-jichi.md @ 0632b94 -->

<!-- _class: lead -->

# Usando jichi

### De la primera ejecución a la tarea autónoma

---

# Configúralo en un comando

```sh
jichi setup                      # interactive wizard
# or, reuse your global config in a project:
jichi setup --from-global --preset developer
# or, adopt an unfamiliar repo (propose-only analysis + tutorial draft):
jichi setup --onboard
jichi --config local/config.json doctor   # validate everything
```

`doctor` comprueba libcurl, config, modelos, claves, alcanzabilidad, git, MCP,
LSP y los assets de tu proyecto.

---

# Tres formas de ejecutarlo

| Superficie | Comando | Cuándo |
|---|---|---|
| **TUI** | `jichi` | Trabajo interactivo, revisión, aprobaciones de una tecla. |
| **Headless** | `jichi -p "…"` | Scripts, CI, automatización, SSH. |
| **Estructurado** | `jichi -p "…" --output jsonl` | Otro programa/agente lo dirige. |

stdin se lee cuando el prompt es `-`, así un archivo entero puede ser el prompt
sin límite de `ARG_MAX`.

---

# Modos: ¿cuánta correa?

- **`/chat`** — normal; pregunta antes de acciones mutantes.
- **`/plan`** — solo lectura; investiga y propone, sin ediciones.
- **`/auto`** — autónomo; corre sus herramientas en sandbox sin preguntar.

```sh
jichi --plan -p "how would you add feature X?"    # a plan
jichi --auto -p "add feature X and make tests pass"  # do it
```

<!-- El modo plan es el default seguro para explorar un repo desconocido o
compartido. -->

---

# Las herramientas que tiene

- **Archivos:** `read_file`, `write_file`, `edit_file`, `apply_patch`
  (multi-edición atómica), `list_files`, `search_code`.
- **Ejecución:** `run_terminal_command` (+ background), `run_tests`.
- **Conocimiento:** `codebase_search`, `search_docs`, `fetch_url`, `web_search`.
- **Git:** status/diff/log/blame + add/commit/branch/stash.
- **Nav/refactor:** LSP `find_definition`/`references`/`symbols`, rename, format.
- **Delegar:** `spawn_subagent`, `spawn_parallel`.
- **Media y sonido:** `generate_image`, `generate_audio`, `transcribe_audio`,
  `play_audio`, `record_audio`.

---

# Contexto que aportas a un turno

Las `@`-referencias en un mensaje plano traen contexto:

```
review @src/parser.c against @diff and the @rss:https://…/releases.xml feed
explain @sym:jc_agent_run_turn and @folder:src/net
```

`@file @diff @url @rss @sym @docs @problems @folder @mcp @audio @img` — cada una
resuelve a un bloque acotado añadido a tu mensaje.

---

# Cuando una sesión se alarga

- **Autocompactación** resume el prefijo viejo para mantenerse dentro de la
  ventana; **compactación en mitad de turno** recorta salida de herramientas
  pesada en un solo turno desbocado.
- **Calibración de tokens** aprende los bytes-por-token reales de cada modelo
  para que las estimaciones dejen de ser optimistas.
- `/context` muestra el desglose de presupuesto en vivo; `/cost` el gasto en
  curso.

Casi nunca piensas en ello — ese es el punto.

---

# Ejecuciones autónomas, con seguridad

```sh
jichi --auto \
  --verify "make test" \
  --budget-tokens 500k --deadline 30m \
  --edit-scope "src/**" \
  --journal run.jsonl --control \
  -p "fix the failing ring-buffer tests"
```

Pasa → avanza; falla → fix-forward N veces, si no, revierte al último verde.
Todo está en el journal.

Dirígelo en vivo: `jichi control <sock> status | inject "…" | pause | abort`.
Léelo de vuelta: `jichi runs` y `jichi audit` (ambos `--output json`).

---

# En tu editor

- **Emacs** (`jichi.el`), **Vim/Neovim** (`jichi.vim`), **nano** (`jichi-nano`) — todos
  sobre el contrato headless.
- **Zed / cualquier editor ACP** — `jichi serve` (aprobación granular,
  streaming).

```vim
:JichiAsk how does the fence work?    " answer in a scratch split
:'<,'>JichiRegion tidy this           " transform a selection in place
:JichiTask add a test for edge case Y " agentic, confirms first
```

---

<!-- _class: lead -->

# Comandos útiles

`/model` `/mode` `/diff` `/undo` `/rewind` `/compact` `/context` `/cost`
`/skills` `/mcp` `/export` `/fork` `/sessions`

`export` una transcripción para un PR o una clase; `fork` para explorar una rama
sin perder tu sitio.
