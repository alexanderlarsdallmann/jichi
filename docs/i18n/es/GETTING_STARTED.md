<!-- tracks: ../en/GETTING_STARTED.md (canonical) -->
# Primeros pasos con jichi

jichi es un agente de programación con IA para la línea de comandos en
Linux. Esta página te lleva desde cero hasta una sesión funcional. Las guías más
detalladas (enlazadas abajo) están en inglés.

## 1. Instalación

Necesitas **libcurl** y un compilador de C. Compila los dos binarios:

```sh
make
```

Esto genera `jichi` (el agente) y `jichi-convert` (un importador de
configuración). Consulta los requisitos del sistema en [INSTALL.md](../../INSTALL.md).

## 2. Configuración

Ejecuta la configuración guiada — detecta el lenguaje de tu proyecto y escribe una
configuración:

```sh
jichi setup
```

Para un punto de partida mínimo y seguro (ideal para aprender), añade `--profile
beginner`. Tu clave de API se lee de una **variable de entorno** (nunca se guarda
en la configuración).

Comprueba que todo esté correcto:

```sh
jichi doctor      # comprobación de estado
jichi benchmark   # puntuación de buenas prácticas
```

jichi puede responder en tu idioma: añade `"language": "español"` a la
configuración, o pasa `--language` — consulta
[LANGUAGE.md](../../LANGUAGE.md).

## 3. Uso

Sesión interactiva (una REPL):

```sh
jichi
```

Escribe tu petición en lenguaje natural. Señala el código con `@archivo` o
`@sym:nombre`. Comandos útiles: `/help`, `/model`, `/mode`, `/constraints`,
`/undo`. Pulsa **Ctrl-C** para detener la tarea actual sin salir.

Sin interfaz (para scripts y automatización):

```sh
jichi -p "explica qué hace src/main.c"
```

## 4. Mantén el control

- **Modos:** chat (pregunta antes de actuar), plan (solo lectura), auto (autónomo).
- **Restricciones (constraints):** di «no ejecutes la compilación» y se *aplica*,
  no solo se recuerda — consulta [CONSTRAINTS.md](../../CONSTRAINTS.md).
- **Deshacer:** cada cambio queda en un punto de control; `/undo` lo revierte —
  consulta [SNAPSHOTS.md](../../SNAPSHOTS.md).

## Adónde ir después

- El camino completo, del primer paso a la maestría:
  [JOURNEY.md](JOURNEY.md)
- Elige tu recorrido: [WORKFLOWS.md](../../WORKFLOWS.md)
- Tutorial para principiantes: [TUTORIAL_BEGINNER.md](../../TUTORIAL_BEGINNER.md)
- Subsistemas avanzados: [TUTORIAL_ADVANCED.md](../../TUTORIAL_ADVANCED.md)
