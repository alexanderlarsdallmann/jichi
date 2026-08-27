---
marp: true
title: jichi — construyéndolo con IA
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/06-building-with-ai.md @ 0632b94 -->

<!-- _class: lead -->

# Construyendo jichi

### Un desarrollador, asistido por un agente de IA

Una retrospectiva del *método* — las cifras, los modelos de entrega, y qué hizo
realmente el humano. Datos completos: `docs/PROJECT_TIMELINE.md`.

---

# El proyecto, en cifras

| Métrica | Valor |
|---|---|
| Lapso de calendario | 2026-06-18 → 2026-07-24 (**37 días**, **19 activos**) |
| Commits | **378** |
| Milestones | **M1 – M173** |
| Código (`src`+`include`) | **~70,400 líneas** C89 |
| Pruebas | **~43,300 líneas**, **10,000+ aserciones** |
| Documentación | **~24,600 líneas**, 122 archivos |
| Puertas de calidad | `-Werror` (gcc+clang), ASan/UBSan, valgrind, fuzz, e2e |

Una proporción código : pruebas : docs de **1 : 0.36 : 0.35** — pruebas y docs
fueron parte de la definición de hecho de cada milestone.

---

# Seis fases

```
P0  Foundation            Jun 18-24   M1-M20    core substrate, providers, loop
P1  Retrieval & protocols Jun 24-26   M21-M50   cache, compaction, RAG, MCP
P2  Integrations/autonomy Jun 26-Jul1 M50-M80   LSP, ACP, envelope, subagents
P3  Hardening/self-improve Jul 1-10   M80-M106  dogfood -> grounded guardrails
P4  Suite & release        Jul 13-15  M107-M134 suite, fuzzing, security band
P5  Post-release depth     Jul 23-24  M135-M173 loops, control, robotics
```

Un hueco de 9 días separó el endurecimiento para release (P4) de una **ola de
capacidad post-release** distinta (P5): ops de autonomía, observabilidad, el
canal de control, y el alcance hacia el uso corpóreo/robótico.

---

# Intensidad de desarrollo (commits/día activo)

```
Jun18 ## 6      Jun30 ############ 24
Jun19 ########## 20   Jul01 #### 8
Jun22 ########## 20   Jul02 ###### 12
Jun23 ################## 36   Jul06 ###### 12
Jun24 ############### 31   Jul07 ##### 9
Jun25 ################## 36   Jul08 ################# 33
Jun26 ######## 16   Jul09 ################# 33
                    Jul10 ##### 10
                    Jul13 ################## 36
                    Jul14 #### 8
                    Jul23 ########## 20
                    Jul24 #### 8      -> 378 total
```

El ritmo alto de commits se mantuvo **seguro** porque la puerta de calidad era
automatizada y dura.

---

# El bucle de milestones (el artefacto reutilizable)

```
requirement  ->  design (seam + pure core + thin shell; often a proposal)
             ->  implement in C89
             ->  tests (pure-core unit + e2e/PTY smoke)
             ->  gate: -Werror + ASan/UBSan + valgrind + e2e  --(red)--> implement
             ->  docs + ROADMAP note
             ->  scoped, verdict-gated commit  ->  (next)
```

~160 milestones, cada uno una pequeña unidad diseñada. La historia se lee como
narrativa *gracias* a esta disciplina — y es lo que mantuvo a la IA correcta a lo
largo de 378 commits sin regresiones.

---

# Cuatro formas de entregar el mismo alcance

Esfuerzo humano para el mismo alcance **M1–M173** (persona-meses, punto medio):

```
AI-assisted (1 dev + AI)  | ~1    <- actual (~5 weeks, 19 active days)
Expert solo               |#################### ~21   (~20-26 months)
Team of ~6                |###################################### ~40  (~5-6 months)
Junior solo               |######################################################## ~60 (~5-6 yrs)
```

- **Asistido por IA** = tiempo de supervisión *humana* (diseño + revisión +
  dirección); excluye el cómputo del modelo.
- **Equipo** cuesta más esfuerzo *total* que experto-solo (coste de coordinación)
  pero mucho menos *calendario* — el trade-off esfuerzo-vs-cronograma.
- **Júnior** conlleva un riesgo de completitud, no solo lentitud: varios
  subsistemas son difíciles de alcanzar con esta calidad sin mentoría.

---

# A dónde fue el tiempo del humano

```
Direction + requirements (what to build)      ##############  30%
Design review + approving proposals           ############    25%
Reviewing diffs + steering mid-run             ############    25%
Deciding priorities / sequencing bands         ######          12%
Verifying results / reading CI                  ####            8%
```

El desarrollador escribió **casi nada de C**. El valor se movió *hacia arriba en
la pila*: elegir la siguiente banda, aprobar un diseño, atrapar un supuesto
equivocado, pausar una ejecución.

---

# Por qué la higiene importó *más*, no menos

Las mismas disciplinas que permiten a un **equipo** escalar son lo que hizo
**confiable** la asistencia de IA:

- **Milestones ajustados** → unidades revisables, una historia limpia.
- **Una nota de diseño antes del código** → el humano revisa *intención*, barato,
  primero.
- **Testeabilidad de núcleo puro** → 10,000+ aserciones offline; CI rápida y
  hermética.
- **Una puerta de calidad dura** → regresiones atrapadas al instante; un ritmo
  alto de commits es seguro.
- **Un doc + commit por unidad** → el trabajo es legible y reanudable.

> La IA no reemplazó la disciplina de ingeniería — **elevó el retorno** de ella.

---

# Límites honestos

- Las cifras de **persona-mes** para los tres modelos humanos son estimaciones
  (±40%), de abajo hacia arriba desde el alcance entregado — no ejecuciones
  medidas.
- La cifra **asistida por IA** es la única medida directamente (calendario + días
  activos), pero cuenta solo tiempo *humano*; **el cómputo del modelo es un coste
  real** pagado en tokens.
- Una **implementación de referencia** (la CLI de Continue) recortó el riesgo de
  requisitos en P0–P4; las novedosas bandas de P5 no tuvieron ninguna — por eso
  cada una abrió con una propuesta.
- Un proyecto es un punto de datos. La afirmación transferible es el **método**,
  no un multiplicador universal.

---

<!-- _class: lead -->

# La conclusión

**La asistencia de IA comprime implementación, pruebas y documentación. El diseño
y la supervisión siguen siendo humanos — y fijan el techo de calidad.**

Invierte el tiempo ahorrado donde ahora más importa: decidir *qué* construir, y
juzgar *si el resultado es correcto*.

`docs/PROJECT_TIMELINE.md` · `docs/ROADMAP.md` · `docs/ANECDOTES.md`
