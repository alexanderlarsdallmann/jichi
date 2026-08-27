---
marp: true
title: jichi en la escuela
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/05-school.md @ 0632b94 -->
<!-- slides-behind: 1 (en has 11 slide separators, this has 10). The English deck
     gained slides this translation does not carry. Translating them is prose in a
     language this repository cannot review, so the gap is DECLARED rather than
     filled -- and the numbers above are checked, so the declaration cannot be left
     behind silently either. M582. -->

<!-- _class: lead -->

# jichi en el aula

### Aprender a programar *con* un agente, con seguridad

---

# La preocupación, nombrada

> "Si una IA escribe el código, ¿aprenden algo los estudiantes?"

La función de assignments de jichi está diseñada para lo contrario: el agente es un
**entrenador con un dial**, no una máquina de respuestas. El aprendiz hace el
trabajo; la ayuda es una cantidad ajustable, y los criterios de evaluación son
visibles.

---

# Cómo transcurre una lección

```
teacher: /assign implementation "a function that reverses a list"
         → docs/assignments/reverse-list.md  (brief + rubric + hint ladder)
student: works it in the editor; stuck? → one hint at a time (hint tool)
         still stuck? → a focused question (ask_for_help)
teacher: /check reverse-list.md student.py  → rubric-keyed feedback (read-only)
```

La solución de referencia se **retiene**; la rúbrica se **muestra**.

---

# La escalera de pistas es la pedagogía

- Estar atascado se vuelve un **dial**, no un muro.
- Las pistas están **graduadas** — el empujón más suave primero, el spoiler
  completo al final.
- El aprendiz gasta su "presupuesto de esfuerzo" productivamente.
- `ask_for_help` da una respuesta dirigida a una confusión *específica*, no un
  prompt hazme-los-deberes.

> Ajusta la escalera por edad/nivel. Ese ajuste *es* la enseñanza.

---

# Salvaguardas que la hacen segura para el aula

- **Modo plan / solo lectura** — los estudiantes exploran sin cambiar material
  compartido.
- **Path fence** — el agente no puede tocar archivos fuera de la carpeta de la
  lección.
- **Corrección solo lectura** — el `solution-checker` nunca edita el código de un
  estudiante.
- **Autoría propose-only** — el profesor aprueba cada ejercicio y referencia antes
  de repartirlo.
- **Modelo autoalojado** — el trabajo de los chicos se queda en el servidor de la
  escuela.

---

# Aprendices por niveles modelan buenos hábitos

Cuatro perfiles muestran *cómo* abordar un problema a un nivel:

- **`learner-junior`** — se apoya en pistas, ayuda y delegación.
- **`learner-student`** — usa referencias, ayuda moderada.
- **`learner-senior`** — ayuda mínima, planifica primero.
- **`learner-agent`** — estratégico y eficiente; el nivel para agentes.

Ver a un nivel enfrentar un problema siembra una conversación de clase sobre
*estrategia*, no solo la respuesta.

---

# Qué hace realmente el profesor

1. Redactar un pequeño conjunto de ejercicios una vez (`/assign`, revisar los
   borradores).
2. Repartir los enunciados (guardar los archivos `.solution.md`).
3. Circular; cuando un estudiante se atasca, guíalo a la *siguiente pista*, no a
   la respuesta.
4. `/check` las entregas para una primera nota consistente que tú revisas.

No hay tooling nuevo que aprender — es el mismo binario `jichi`.

---

# Más allá de los ejercicios

- **"Explícame esto"** — `:JichiExplain` un fragmento en el editor, o `jichi-nano
  explain file.py` — un explicador paciente y a demanda.
- **Revisión de código como lección** — `/check` o un agente de revisión convierte
  una entrega en feedback etiquetado y respaldado por evidencia.
- **Corre en una máquina tipo Chromebook** por red hacia un servidor LLM de la
  escuela.

---

<!-- _class: lead -->

# Cómo empezar (profesor)

```sh
jichi init assignments        # scaffold the pack
# add  "assignments": true  to config, then:
jichi -p "/assign implementation 'sum a list'"
```

Guía completa: `docs/TEACHING_ASSIGNMENTS.md` (aula, tutoría, autoestudio,
cohorte/TA).
