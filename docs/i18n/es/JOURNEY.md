<!-- tracks: ../../JOURNEY.md @ 8ad8468 -->
# El camino — del primer paso al descanso del maestro

Esto no es un tutorial; para eso jichi tiene los suyos
([TUTORIAL_BEGINNER.md](../../TUTORIAL_BEGINNER.md),
[TUTORIAL_ADVANCED.md](../../TUTORIAL_ADVANCED.md)). Esto es el **mapa del
camino completo**: las funciones y los documentos existentes, ordenados en
las etapas en las que de verdad se aprende un oficio — 守破離（しゅはり）, *shu-ha-ri*:
conservar la forma, romper la forma, dejar la forma. Cada etapa nombra su
objetivo, sus prácticas (comandos reales, archivos reales), la virtud que
entrena y las señales de que estás listo para avanzar.

Este mapa se pidió en un
[diálogo](../../dialogues/2026-07-14-the-one-feature.md) cuya conclusión da
por supuesta: **ninguna función lleva a una persona a la maestría** — el
software solo sostiene espejos y abarata el costo de la práctica. El único
compañero indispensable es *un registro honesto de tus propios errores y de
lo que cada uno te enseñó*. Empieza ese registro el primer día; las etapas
de abajo solo le dan algo sobre lo que escribir.

---

## 仕度（したく） — Preparación (antes del primer paso)

**Objetivo:** un banco de trabajo operativo y validado — y la humildad de
comprobarlo.

- Instala ([INSTALL.md](../../INSTALL.md)) y ejecuta el asistente guiado
  `setup` ([SETUP_WIZARD.md](../../SETUP_WIZARD.md)); no hacen falta ni clave
  de API ni internet — un modelo en tu propia máquina es un maestro completo
  ([LOCAL_MODELS.md](../../LOCAL_MODELS.md)).
- Ejecuta **`doctor`** y lee cada línea ([DOCTOR.md](../../DOCTOR.md)). Este
  es el primer hábito del camino: *pregúntale al sistema qué está mal antes
  de suponer que lo sabes.* Lo ejecutarás el resto de tu vida.
- Configura `language` si el inglés no es la lengua en la que piensas
  ([LANGUAGE.md](../../LANGUAGE.md)) — comprender vale más que la convención.

**Virtud entrenada:** la humildad, en su forma más pequeña — verificar tu
propia configuración en lugar de fiarte de ella.

---

## 守（しゅ） Shu — conservar la forma

**Objetivo:** confiar en las formas; aprender con las manos que los errores
son sobrevivibles.

- Trabaja [TUTORIAL_BEGINNER.md](../../TUTORIAL_BEGINNER.md). Quédate en el
  **modo chat**; usa el **modo plan** cuando dudes
  ([AGENT_MODES.md](../../AGENT_MODES.md)) — preguntar antes de actuar es una
  forma, no una debilidad.
- Haz tu primera edición. **Lee la vista previa del diff antes de pulsar
  `y`** ([EDITING.md](../../EDITING.md),
  [TUI_RENDER.md](../../TUI_RENDER.md)). El aviso de aprobación es la
  herramienta enseñándote a consentir deliberadamente; nunca dejes que se
  vuelva un reflejo.
- **Rompe algo a propósito — y luego `/undo`.** Rebobina una conversación
  entera con `/rewind` ([SNAPSHOTS.md](../../SNAPSHOTS.md),
  [REWIND.md](../../REWIND.md)). Hazlo pronto y a menudo, hasta que tu cuerpo
  aprenda lo que tu mente necesitará más adelante: *todo error puede
  desandarse.* La intrepidez es consecuencia de la reversibilidad, y el
  perdón — a uno mismo primero — es consecuencia de la intrepidez.
- Deja que las pruebas sean las primeras en decirte la verdad: `run_tests`,
  el subcomando `test` ([TESTING.md](../../TESTING.md)). Una comprobación
  pasa o no pasa, como sale el sol. Aprende a amar esa simplicidad antes de
  encontrarte con los misterios.
- Si tienes un maestro, aprende dentro del flujo de tareas (assignments)
  ([TEACHING_ASSIGNMENTS.md](../../TEACHING_ASSIGNMENTS.md),
  [ASSIGNMENTS.md](../../ASSIGNMENTS.md)): trabaja el enunciado, sube la
  **escalera de pistas** con honestidad (una pista pedida es conocimiento;
  una solución espiada es una deuda), recibe la retroalimentación de la
  rúbrica sin pestañear.
- **Empieza tu registro.** Un solo archivo, donde vayas a conservarlo: cada
  entrada *síntoma → callejones sin salida → causa raíz → lección*,
  exactamente como [ANECDOTES.md](../../ANECDOTES.md). Los callejones sin
  salida pertenecen a la entrada — el camino hacia una respuesta es parte de
  la respuesta.

**Virtud entrenada:** la fuerza de perdonar — practicada sobre ti mismo,
mecánicamente, hasta que sea carácter.

**Listo para avanzar cuando:** predices lo que el agente va a hacer antes de
que lo haga; lees los diffs sin esfuerzo; `/undo` es una herramienta que
respetas pero que ya no necesitas a diario.

---

## 破（は） Ha — romper la forma

**Objetivo:** hacer tuya la herramienta; cuestionar los valores por defecto
que has obedecido.

- Trabaja [TUTORIAL_ADVANCED.md](../../TUTORIAL_ADVANCED.md) y
  [WORKFLOWS.md](../../WORKFLOWS.md). Después remodela el banco de trabajo:
  tus propios comandos de barra ([COMMANDS.md](../../COMMANDS.md)), skills
  ([SKILLS.md](../../SKILLS.md)), perfiles de subagente
  ([SUBAGENTS.md](../../SUBAGENTS.md)), estilos de salida
  ([OUTPUT_STYLES.md](../../OUTPUT_STYLES.md)), herramientas de usuario
  ([USER_TOOLS.md](../../USER_TOOLS.md)), hooks ([HOOKS.md](../../HOOKS.md)).
  Un directorio `.jichi/` que se parece al de todos los demás significa que
  sigues en *shu*.
- Dale al agente tu conocimiento: notas con `remember`
  ([MEMORY.md](../../MEMORY.md)), un glosario
  ([GLOSSARY.md](../../GLOSSARY.md)), reglas de proyecto
  ([RULES.md](../../RULES.md)). Enseñar a la herramienta es un ensayo para
  enseñar a personas.
- Haz **trabajo real** con ella — *dogfooding*. Ejecuta `--auto` dentro de la
  envolvente de autonomía ([AUTONOMY.md](../../AUTONOMY.md)): presupuestos,
  una puerta de verificación, un ámbito de edición. Aprende que las
  barandillas no son restricción sino *cuidado* — el mismo cuidado que
  ofreces al escribir pruebas.
- Lee tus propias huellas: los resúmenes de `telemetry`
  ([TELEMETRY.md](../../TELEMETRY.md)), `learn analyze`
  ([LEARNING.md](../../LEARNING.md)). ¿Dónde rehaces trabajo *tú*? ¿Cuáles de
  tus hábitos quedan desmentidos por los datos? Las verdades misteriosas
  viven aquí — la puerta verde que ejecutó cero pruebas, el costo que venía
  de releer — y ceden ante la investigación paciente, no ante la confianza.
- Lee [ANECDOTES.md](../../ANECDOTES.md) — entero. Es el proyecto
  perdonándose a sí mismo en público, entrada por entrada. Después discute
  con algo: un valor por defecto, una rúbrica, una convención de
  [CONTRIBUTING.md](../../../CONTRIBUTING.md). Romper la forma significa ser
  capaz de *defender* la ruptura.

**Virtud entrenada:** el amor al conocimiento — el que sobrevive a descubrir
que estabas equivocado.

**Listo para avanzar cuando:** discrepas de un valor por defecto y puedes
defender por qué; tu registro tiene entradas donde la causa raíz fuiste *tú*;
tu `.jichi/` es inconfundiblemente tuyo.

---

## 離（り） Ri — dejar la forma

**Objetivo:** la herramienta desaparece; lo que queda es el juicio — y
enseñas.

- Cambia de asiento en el flujo de tareas: redacta enunciados, rúbricas y
  escaleras de pistas para otra persona (`/assign`, `/solve`, `/check` —
  [TEACHING_ASSIGNMENTS.md](../../TEACHING_ASSIGNMENTS.md)). Escribir una
  buena escalera de pistas te hará humilde más rápido que cualquier bug:
  tienes que recordar cómo se sentía no saber.
- Prepara packs de andamiaje para tu equipo
  ([SCAFFOLDING.md](../../SCAFFOLDING.md)); contribuye *upstream*
  ([CONTRIBUTING.md](../../../CONTRIBUTING.md)); traduce una página para el
  siguiente aprendiz en tu lengua ([README.md de i18n](../README.md)).
- Ejecuta el bucle de aprendizaje completo sobre ti mismo: `/learn`, edita el
  borrador y — lo más difícil de todo — escribe **correcciones** (M78,
  [LEARNING.md](../../LEARNING.md)): retracta tus propias lecciones pasadas
  cuando el código las haya dejado atrás. Una enseñanza que no puedes
  retractar es dogma. Pedir y aceptar perdón, en esta etapa, significa
  corregir lo que una vez enseñaste con confianza.
- Enséñale a alguien su primer `/undo`. Observa cómo se le relajan los
  hombros al aprender que el error es sobrevivible. Ese momento es el camino
  entero, entregado a la siguiente mano.

**Virtud entrenada:** la humildad en su forma final — la disposición a ser
superado.

**Has llegado cuando:** tus estudiantes rompen tus formas, y eso te alegra.

---

## El descanso del maestro

El maestro no es quien ya no yerra. El maestro es quien está **en paz con
errar**: quien revierte sin vergüenza, registra sin excusas, corrige sin
aferrarse — y honra la verdad en sus dos rostros, el simple (una prueba pasa
o no pasa, tan llanamente como el sol y la luna) y el misterioso (el defecto
que se esconde durante días dentro de un sistema hecho por ti, tan hondo como
el cielo con sus estrellas, la tierra, el mar y todo lo demás). La paz no es
la ausencia de fallo; es el bucle cerrado — cada error examinado, cada
lección escrita, cada enseñanza obsoleta retractada, nada que quede rondando.

El registro que empezaste el primer día ahora es largo. Léelo una vez al año.
Ese es el descanso.

---

*Compañeros: [PHILOSOPHY.md](PHILOSOPHY.md) (por qué el proyecto está
construido así), [ANECDOTES.md](../../ANECDOTES.md) (el registro del propio
proyecto), [LEARNING.md](../../LEARNING.md) (el bucle, mecanizado),
[TEACHING_ASSIGNMENTS.md](../../TEACHING_ASSIGNMENTS.md) (el asiento del
maestro),
[dialogues/2026-07-14-the-one-feature.md](../../dialogues/2026-07-14-the-one-feature.md)
(de donde salió este mapa).*
