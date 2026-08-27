---
marp: true
title: jichi en la universidad
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/04-university.md @ 0632b94 -->
<!-- slides-behind: 2 (en has 12 slide separators, this has 10). The English deck
     gained slides this translation does not carry. Translating them is prose in a
     language this repository cannot review, so the gap is DECLARED rather than
     filled -- and the numbers above are checked, so the declaration cannot be left
     behind silently either. M582. -->

<!-- _class: lead -->

# jichi en la universidad

### Investigación, cursos, reproducibilidad

---

# Por qué encaja en entornos académicos

- **Corre en el hardware que tienes** — un nodo de login compartido, una máquina
  vieja de laboratorio, una Raspberry Pi, un móvil en Termux. ~1.2 MB, ~10–17 MB
  RSS, habla con un modelo remoto o local.
- **Sin vendor lock-in** — apúntalo a un servidor LLM del departamento, un
  llama.cpp/vLLM local, o una API comercial. Los servidores locales sin clave
  funcionan sin más.
- **Auditable** — es C89 con una suite de pruebas; los estudiantes pueden *leer*
  el agente, no solo usarlo.
- **Consciente del coste** — caché de prompts + `/cost` en vivo; los presupuestos
  limitan una ejecución autónoma.

---

# Para software de investigación

- **Entiende un código heredado rápido** — mapas `@folder:` + `codebase_search` +
  `search_docs` sobre el material de referencia del paper.
- **Ejecuciones reproducibles** — headless `-p` + `--output json`, dirigido desde
  un Makefile o un job de Slurm; el journal JSONL es tu registro de procedencia.
- **Autonomía con correa** — `--auto --verify "make test" --budget-*` para que un
  refactor nocturno no se desboque ni rompa el build en silencio.
- **Caso de estudio: la reescritura zigodot** — un port de lenguaje grande y
  autónomo impulsado enteramente por jichi, con telemetría que muestra exactamente a
  dónde fue el esfuerzo.
- **Investigación corpórea / robótica** — jichi como capa *deliberativa* de un robot
  (sensores + actuadores como herramientas, una puerta de seguridad cinética, E/S
  de sonido); un simulador sin hardware viene en `examples/robot-sim/`
  (`docs/ROBOTICS.md`).
- **Ingeniería de software y PM como asignatura** — `docs/PROJECT_TIMELINE.md` es
  una retrospectiva fundamentada en datos (fases, intensidad, cuatro modelos de
  entrega incl. un desarrollador + IA) usable en un curso de gestión de proyectos
  o de IS.

---

# Para impartir un curso

- La función de **assignments**: los instructores redactan enunciados + rúbricas +
  una escalera de pistas; la solución de referencia se retiene; la corrección es
  **solo lectura** y guiada por rúbrica. (Ver `docs/TEACHING_ASSIGNMENTS.md`.)
- **Corrección consistente entre TAs** — la misma rúbrica + referencia +
  corrector solo-lectura significan menos varianza entre correctores.
- **El agente es legible** — un ejercicio sobre "¿cómo funciona un bucle de
  agente?" puede apuntar al `jc_agent_run_turn` de *este* código.

---

# Reproducibilidad y procedencia

```sh
jichi --auto \
  --verify "pytest -q" \
  --budget-tokens 300k \
  --journal artifacts/run-$(date +%s).jsonl \
  --output json \
  -p "implement the FFT variant from the spec and pass the tests" \
  > artifacts/result.json
```

Cada llamada al modelo, llamada a herramienta, coste y resultado queda
capturado — adjúntalo al cuaderno de laboratorio o al artefacto del paper.

---

# Bajos recursos y remoto

- **`--lite`** apaga los subsistemas pesados en un solo flag (snapshots, repo map,
  paralelismo) para una huella diminuta en nodos restringidos.
- **SSH + tmux** — arranca una ejecución larga de `--auto` en la máquina GPU,
  desengánchate, reengánchate luego (`docs/REMOTE_SSH.md`, `docs/TMUX.md`).
- **El daemon** — un proceso caliente en un servidor de laboratorio sirve muchas
  peticiones rápidas sin recargar config/index cada vez.

---

# Privacidad y gobernanza de datos

- Corre contra un modelo **autoalojado**, así el código de estudiantes y los
  datos de investigación nunca necesitan salir de la institución.
- El **path fence** y el **edit-scope** acotan lo que una ejecución autónoma puede
  tocar; los **reference roots** permiten acceso de solo lectura a corpus
  compartidos sin riesgo de escritura.
- Los secretos vienen de variables de entorno `apiKeyEnv` — nunca escritos en la
  config, redactados de los logs.

---

# Un hueco concreto de temario

> *"Semana 9: herramientas agénticas."* Los estudiantes leen `jc_agent_run_turn`,
> añaden una nueva herramienta built-in tras el registro, y ven al modelo
> llamarla. El bucle entero es ~una función; la interfaz de herramienta es un
> struct. Desmitifica los "agentes de IA".

---

<!-- _class: lead -->

# Empieza aquí

```sh
jichi setup --preset developer
# teaching:
jichi init assignments   # + "assignments": true in config
```

`docs/TUTORIAL_ADVANCED.md`, `docs/TEACHING_ASSIGNMENTS.md`, `docs/LOW_MEMORY.md`.
