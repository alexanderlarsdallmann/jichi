---
marp: true
title: jichi — roadmap
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/03-roadmap.md @ 0632b94 -->
<!-- slides-behind: 2 (en has 15 slide separators, this has 13). The English deck
     gained slides this translation does not carry. Translating them is prose in a
     language this repository cannot review, so the gap is DECLARED rather than
     filled -- and the numbers above are checked, so the declaration cannot be left
     behind silently either. M582. -->

<!-- _class: lead -->

# jichi

### Dónde ha estado, a dónde va, y cómo se construyó

---

# Cómo se construyó

- **Milestones, no un big bang.** M1 (esqueleto) → una subida constante de
  capacidad → **M173** hoy, a lo largo de seis fases (cimientos → protocolos →
  autonomía → endurecimiento → release → profundidad post-release).
- Cada milestone es un plan enfocado; diseños + implicaciones registrados en
  `docs/ROADMAP.md` (con un **índice temático** arriba) y, para trabajo novedoso,
  un `docs/proposals/*.md`.
- Cada milestone aterriza con **pruebas** y **cero avisos** — la suite tiene
  **11,000+ comprobaciones** y crece con cada función, tras un `make ci` con puerta
  de veredicto.

---

# El mapa de capacidades (todo entregado)

- **Núcleo y contexto:** bucle, dos proveedores, compactación, calibración, caché
  de prompts.
- **Edición:** parches resilientes/difusos, multi-edición atómica, diffs
  unificados.
- **Conocimiento:** RAG híbrido (BM25 + embeddings + rerank), índice de docs,
  PDFs, RSS.
- **Autonomía:** envolvente (presupuestos/verify/edit-scope/journal), snapshots,
  rewind.
- **Escala:** subagentes (2 de profundidad), worktrees paralelos, un daemon
  caliente + pool de workers.
- **Integraciones:** cliente MCP, servidor ACP, nav + refactorizaciones LSP,
  editores.
- **Media y sonido:** generación de imagen/audio, transcripción, play/record.
- **Seguridad:** path fence, puerta de comandos privilegiados, puerta cinética —
  todo auditado.

---

# El arco de operaciones autónomas (M157–M162)

Una ejecución desatendida ahora está **acotada, observada, controlada, dirigida y
consultable**:

| Banda | Qué aterrizó |
|---|---|
| **M157** bucles | supervisor tmux/systemd/cron sobre cola de tareas + pack de referencia |
| **M158** observabilidad | lectores `runs` / `audit`; puerta `doctor --unattended`; lint docs↔flags |
| **M159/M162** control | socket unix en ejecución: `status` / `inject` / `pause[--extend]` / `resume` / `abort` |
| **M160** legible por máquina | ventanas `--since` + `--output json` para `runs`/`audit` |
| **M161** procedencia | `runs` marca una ejecución **dirigida** por operador |

Cada intervención deja rastro; cada rastro tiene un lector.

---

# El alcance hacia el mundo físico (M163)

jichi como la **mente deliberativa** de un robot — sensores, actuadores, sonido,
flotas:

- **Los dispositivos son herramientas** (user-tools / servidores MCP que hablan
  JSON); jichi no enlaza ninguna librería de dispositivo (shell-out, como
  `pdftotext`).
- **Puerta cinética** — cualquier cosa que mueva masa/energía es `kinetic: true`,
  controlada **bajo el veredicto de permiso** (un auto-approve general no puede
  satisfacerla), allowlist-primero para el E-stop, bypass de shell cotejado por
  sombra, siempre auditada.
- **E/S de sonido** — `play_audio` / `record_audio` vía comandos configurados.
- **Alcance honesto:** jichi es la capa de **escala de segundos**; los reflejos + el
  E-stop real viven debajo, en firmware. Probado contra un simulador; hardware
  diferido.

---

# Las bandas de mediados → finales de julio (M135–M156)

| Tema | Qué aterrizó |
|---|---|
| **Habla tu idioma** | clave `language` + prompt de aprobación localizado (en/de/es/ja/zh) |
| **Integridad** | rollback en fase de escritura de `apply_patch`; auto-revert fuera de scope |
| **Recursos** | fixes de arena-lifetime, medidor `/context`, índice mmap'd, release `--lite` |
| **Modelos pequeños** | nudges de tool-calling nativo, arg repair, preset small-local + packs |
| **Seguridad privilegiada** | puerta sudo/doas bajo el veredicto + auditoría siempre activa (M152–M155) |
| **Entrada** | pegado multilínea en el prompt de la TUI (M156) |

---

# La banda de automejora (M100+)

Diseñada en `docs/SELF_IMPROVEMENT.md`:

- **Daemon** — proceso caliente, pool de workers acotado.
- **Arnés assign/grade** — evals comprobables por máquina.
- **Dream** — "consolidación de sueño" propose-only sobre telemetría en reposo.
- **Bucle de aprendizaje** — realimenta los logs del agente como lecciones
  duraderas.
- **Bucle de síntesis** — los ata a todos juntos.

El tema: un agente de código que mejora *mediblemente* en *este* proyecto.

---

# Qué aprendimos haciendo dogfooding

De `docs/ANECDOTES.md` — cada uno un bug que enseñó una lección duradera:

- Mantén la observabilidad **fuera** del radio de acción del snapshot/rollback.
- El agotamiento de presupuesto debe **parar**, no **descartar** — verifica,
  luego conserva el verde.
- Un "no puedo hacer eso" suele ser una **anuncio** de herramienta ausente, no una
  capacidad ausente (la puerta toolProfile).
- **Pon puerta de veredicto al commit, no al final del log** — un commit de
  aspecto verde una vez ocultó una suite roja (ANECDOTES #17); los commits ahora
  son `make ci && …`.

---

# Cómo se construyó — cuatro modelos de entrega

Esfuerzo humano para el **mismo alcance M1–M173** (persona-meses; ver
`docs/PROJECT_TIMELINE.md`):

```
AI-assisted (1 dev + AI)  |  ~1    (actual -- ~5 weeks, 19 active days)
Expert solo               |####################  ~21   (~20-26 months)
Team of ~6                |######################################  ~40  (~5-6 months)
Junior solo               |#########################################################  ~60  (~5-6 yrs)
```

La barra asistida por IA es tiempo de supervisión **humana** (diseño, revisión,
dirección) — excluye el cómputo del modelo. La lección no es velocidad bruta; es
*a dónde va el tiempo humano*.

---

# Qué hizo realmente el humano (asistido por IA)

El desarrollador escribió casi nada de C. El recurso escaso se movió **hacia
arriba en la pila**:

- **Dirección + requisitos** — elegir la siguiente banda (~30%).
- **Revisión de diseño** — aprobar cada `docs/proposals/*.md` antes del código (~25%).
- **Revisar diffs + dirigir en ejecución** — atrapar supuestos equivocados (~25%).
- **Secuenciación + prioridades** (~12%) · **verificar CI** (~8%).

> La IA comprimió *implementación + pruebas + docs*; **el diseño y la supervisión
> siguieron siendo humanos — y fijaron el techo de calidad.**

---

# Principios guía para lo que viene

1. **Correctitud primero, luego coste.** Verifica agresivamente; revierte solo el
   rojo.
2. **Superficies pequeñas, compuestas.** Las nuevas funciones reutilizan
   chokepoints existentes.
3. **Todo testeable offline.** Núcleos puros + shells delgados de E/S.
4. **Aprende de los logs.** La telemetría propia del agente es la entrada del
   roadmap.
5. **La novedad recibe una propuesta.** Sin referencia que copiar → diséñala por
   escrito primero.

---

<!-- _class: lead -->

# Es orientativo

El roadmap registra *diseños*, no promesas. Cada capacidad recibe su propio plan
cuando se aborda.

`docs/ROADMAP.md` · `docs/PROJECT_TIMELINE.md` · `docs/SELF_IMPROVEMENT.md`
