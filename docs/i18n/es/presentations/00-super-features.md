---
marp: true
title: jichi — súper funciones
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/00-super-features.md @ 0632b94 -->
<!-- slides-behind: 1 (en has 12 slide separators, this has 11). The English deck
     gained slides this translation does not carry. Translating them is prose in a
     language this repository cannot review, so the gap is DECLARED rather than
     filled -- and the numbers above are checked, so the declaration cannot be left
     behind silently either. M582. -->

<!-- _class: lead -->

# jichi

### Un agente de código con IA en **~C89**, un pequeño binario

Una reescritura desde cero de la CLI de Continue: chat, un bucle agéntico de
herramientas, RAG, autonomía, MCP, LSP, una TUI — en ANSI C portable, sin
runtime.

<!-- Apertura: el pitch es "un agente de código moderno y completo que cabe en
~1.2 MB y corre donde exista POSIX + libcurl." -->

---

# Diez cosas que sorprenden a la gente

1. Es **C89**. Cero avisos bajo `-std=c89 -pedantic -Wall -Wextra`.
2. El binario pesa **~1.2 MB**; un turno headless vive en **~10–17 MB RSS**.
3. **Edita tu código** — parches multi-archivo resilientes/difusos con diffs.
4. Corre **autónomamente** con salvaguardas — pausable/dirigible en ejecución.
5. Hace **RAG** — BM25 híbrido + embeddings + rerank sobre tu repo *y* docs.
6. Habla **MCP** (cliente) *y* **ACP** (servidor, para editores como Zed).
7. Tiene navegación **LSP** *y* refactorizaciones (rename/format/code-actions).
8. Genera **imágenes y voz**, y **reproduce/graba** audio.
9. No hará `sudo` ni moverá un **motor** sin pedirlo — cada intento auditado.
10. Hace **checkpoint** de cada cambio, y corre en una **Pi o un móvil** (Termux).

---

# El bucle del agente, en un respiro

```
history + system + tools
  → provider.build_request()      (Anthropic o OpenAI, en streaming)
  → HTTP/SSE                      (libcurl)
  → execute tool calls           (read/edit/run/search/git/…)
  → append results, loop         hasta una respuesta final
```

Un bucle, dos proveedores, ~35 herramientas built-in, y nunca ramifica según el
proveedor.

<!-- Todo es una función, jc_agent_run_turn; lo demás es una herramienta o un
vtable de proveedor. -->

---

# Salvaguardas en las que puedes confiar

- **Modos:** chat (pregunta), plan (solo lectura), auto (autónomo).
- **Path fence:** las herramientas de archivos no escapan del workspace; las
  lecturas pueden extenderse a reference roots nombrados, las escrituras nunca.
- **Envolvente de autonomía:** presupuestos de token/reloj/llamadas-a-herramienta,
  una edit-scope fence, una **puerta de verificación** (corre tus tests), y un
  journal de auditoría JSONL — con **auto-revert** opcional de cambios de shell
  fuera de la edit-scope.
- **Las ediciones multi-archivo cumplen su promesa:** `apply_patch` valida
  todo-o-nada y, si una escritura falla a medias, revierte los archivos ya
  escritos y reporta el estado de cada archivo.
- **Snapshots:** un repo git-sombra hace checkpoint antes de la primera edición —
  `/undo` y `/rewind` restauran archivos *y* conversación.

> ¿Presupuesto agotado a mitad de tarea? Verifica una vez y conserva el trabajo
> que pasa; solo revierte un árbol *rojo*. El progreso parcial no se tira.

---

# También escala *hacia arriba*

- **Subagentes** — delega una subtarea acotada (historia, modelo y tool fence
  propios), ahora dos niveles de profundidad por defecto con reducción de
  presupuesto por nivel.
- **Agentes paralelos** — un pool por fork, cada tarea en un worktree git
  aislado, fusionado a nivel de archivo con primer-gana.
- **Daemon** — un proceso caliente mantiene config/MCP/LSP/index en caliente y
  sirve peticiones por un socket, con un **pool de workers acotado** + watchdog
  por petición.
- **Bucles y flotas** — un supervisor drena una cola de tareas (tmux/systemd/cron);
  un coordinador reparte trabajo entre instancias pares por SSH + MCP.

<!-- Esto hace factible la reescritura zigodot: repartir, aislar, fusionar. -->

---

# Dogfood real: la reescritura zigodot

jichi está impulsando un **port grande y autónomo de Godot → Zig** — la prueba
estrella.

- Ejecuciones largas de `--auto` bajo la envolvente, sobre un código real.
- Telemetría + cronologías por sesión muestran a dónde van realmente
  tokens/coste.
- El **bucle de aprendizaje** realimenta sus propios logs como lecciones
  duraderas para dejar de repetir errores.
- Cada batallita que nos enseñó algo vive en `docs/ANECDOTES.md`.

Las partes difíciles (desbordamiento de contexto, economía sin caché, bugs de
aislamiento) se hallaron *usándolo*, no teorizando.

---

# Te encuentra donde trabajas

- **Terminal:** una TUI real — markdown + resaltado de sintaxis, diffs en vivo,
  aprobaciones de una tecla, `/cost`, `/context`, `/undo`.
- **Headless:** `jichi -p "…"`; `--output json`/`jsonl` para automatización.
- **Editores:** Emacs, Vim/Neovim, nano, y cualquier editor **ACP** (Zed).
- **Remoto:** SSH + tmux para ejecuciones autónomas largas en una máquina GPU/CI.
- **Tu idioma:** `"language": "日本語"` y responde en él — el prompt de aprobación
  le sigue (en/de/es/ja/zh); docs de onboarding en los cinco.

Un binario, todas las superficies, el mismo contrato debajo.

---

# Local, privado, barato

- Apúntalo a **cualquier endpoint compatible con OpenAI** — LM Studio, llama.cpp,
  LocalAI, vLLM. Sin vendor lock-in; los servidores locales sin clave funcionan
  sin más.
- **Caché de prompts** (ambos proveedores) + contabilidad de coste consciente de
  la caché.
- **Generación de media** contra un binario **LocalAI** local — generación de
  imagen verificada en una GPU de consumo sin Docker.
- Corre donde un contenedor no puede: embebido, poca RAM, casi air-gapped.

---

<!-- _class: lead -->

# ¿Por qué C89?

Porque "portable, diminuto, ligero en dependencias, y todavía aquí en diez años"
es una función — para objetivos embebidos, para enseñar, y para la confianza.

**Dependencias:** libcurl + una lib JSON incorporada. Eso es todo.

---

<!-- _class: lead -->

# Pruébalo

```sh
make && ./jichi setup            # guided setup
./jichi -p "explain this repo"   # headless
./jichi                          # the TUI
```

Docs: `README.md`, `docs/ROADMAP.md` (índice temático), `docs/AGENTS_GUIDE.md`.
