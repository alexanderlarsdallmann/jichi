<!-- tracks: ../../PHILOSOPHY.md @ 8ad8468 -->
# La filosofía de jichi

La mayor parte de la documentación de este repositorio dice *cómo*. Esta página
dice *por qué*: los principios detrás de una reimplementación desde cero, en
C89, de un agente de programación con IA, y las deudas que tiene contraídas. Es
un ensayo, no una referencia; nada de lo que hay aquí es normativo en el
sentido en que lo es [CONTRIBUTING.md](../../../CONTRIBUTING.md). Pero si
alguna decisión de diseño llegara a parecer arbitraria, este es el marco en el
que se tomó.

## 温故知新（おんこちしん） — estudiar lo antiguo para conocer lo nuevo

*Onkochishin* (温故知新（おんこちしん）), de las Analectas: «repasa lo antiguo y conocerás lo
nuevo».

La pregunta evidente sobre este proyecto es: ¿por qué C89, en la era de Rust y
TypeScript? La respuesta no es la nostalgia. C89 es el estándar de lenguaje de
programación más ampliamente implementado que existe. Un programa que se
atiene a él — sin comentarios `//`, declaraciones al comienzo del bloque, sin
`long long`, literales de cadena divididos por debajo de 509 caracteres —
compila en máquinas de las que el ecosistema de npm jamás ha oído hablar: un
ordenador de laboratorio con una década a cuestas, el servidor de acceso
compartido de una universidad, una placa única en un aula sin presupuesto. La
restricción es la funcionalidad. Una herramienta agéntica *para aprender* vale
poco si la máquina del aprendiz no puede ejecutarla.

Hay una segunda razón, más discreta. Lo más nuevo de la informática — un gran
modelo de lenguaje razonando sobre código — resulta estar bien servido por las
disciplinas más antiguas que tenemos: búferes acotados, propiedad explícita de
la memoria, códigos de estado en lugar de excepciones, herramientas pequeñas
compuestas mediante tuberías y procesos. El bucle del agente hace fork, abre
tuberías y llama a `select` tal como lo hacían los programas UNIX en 1985.
Estudiar lo antiguo fue la manera en que se construyó lo nuevo.

## 職人気質（しょくにんかたぎ） — el temperamento del artesano

*Shokunin kishitsu* (職人気質（しょくにんかたぎ）) nombra una actitud: la maestría perseguida como
una obligación hacia el trabajo mismo, no hacia un público. El shokunin barre
el suelo del taller venga o no venga un cliente.

En esta base de código, el suelo barrido tiene este aspecto: cero advertencias
bajo `-std=c89 -pedantic -Wall -Wextra`, siempre, con `WERROR=1` en la
integración continua; más de 10.000 comprobaciones unitarias que se ejecutan
sin conexión, de modo que quien contribuye desde un tren puede confiar en la
suite; el streaming de los proveedores probado alimentando eventos SSE
sintéticos en lugar de llamar a la red; cada `cJSON_Parse` emparejado con un
`cJSON_Delete`; un nivel de fuzzing para los analizadores expuestos a bytes no
confiables. Nada de esto se ve en una demostración. Todo se ve en el tercer
año.

El oficio también significa honestidad sobre lo que hizo la herramienta. Los
errores de las herramientas se devuelven como valores, nunca como flujo de
control. Un subagente detenido en su tope de iteraciones lo dice —
«[stopped at its iteration limit]» — en lugar de hacer pasar una respuesta
parcial por una terminada. Las traducciones al japonés y al chino bajo
`docs/i18n/` están marcadas como *borrador* hasta que un hablante nativo las
revise, porque una traducción errónea y segura de sí misma es peor que una
honestamente señalada.

## 改善（かいぜん） y 守破離（しゅはり） — pasos pequeños, y la forma del aprendizaje

*Kaizen* (改善（かいぜん）) es la mejora por acumulación. Este proyecto nunca ha tenido una
reescritura; ha tenido hitos — del esqueleto de M0 a la pasada de
endurecimiento de M134 — cada uno lo bastante pequeño como para revisarlo,
probarlo y, si hiciera falta, revertirlo. El [ROADMAP](../../ROADMAP.md) es el
cuaderno de laboratorio de esa acumulación, y
[ANECDOTES.md](../../ANECDOTES.md) registra los fracasos que vale la pena
recordar, porque un error estudiado es una lección y un error olvidado es un
ensayo general.

*Shu-ha-ri* (守破離（しゅはり）) describe cómo avanza el estudiante de cualquier oficio:
**shu** (守), conservar la forma con exactitud; **ha** (破), romper la forma
deliberadamente; **ri** (離), dejar la forma atrás. Las funciones de
aprendizaje están construidas según esa forma. La banda de tareas
(assignments) da al principiante la forma — un enunciado, una rúbrica, una
escalera de pistas — y los niveles de aprendiz
(`learner-junior|student|senior`) aflojan el andamiaje a medida que el
estudiante se lo gana. Incluso el propio agente es aquí un estudiante: el
bucle de aprendizaje/mentor lee su propia telemetría, redacta lecciones y —
desde M78 — *corrige* las que han quedado obsoletas en lugar de apilar
consejos nuevos sobre los viejos. Una enseñanza que no puede retractarse es
dogma; el bucle no estuvo terminado hasta que supo retirar una lección.

## 不易流行（ふえきりゅうこう） — lo inmutable y lo fluyente

Bashō enseñaba a sus estudiantes el *fueki-ryūkō* (不易流行（ふえきりゅうこう）): el arte necesita
tanto lo permanente como lo efímero, y en el fondo son la misma búsqueda.

La arquitectura de este proyecto es esa lección aplicada. Lo **inmutable**:
C89, POSIX, las arenas, el contrato `jc_status`, una vtable de proveedor
detrás de la cual el agente nunca mira. Lo **fluyente**: modelos, endpoints y
protocolos que se renuevan cada temporada. La frontera entre ambos está
trazada para que el flujo nunca erosione el núcleo — el agente no bifurca
según el proveedor; un backend nuevo es una vtable nueva, no un agente nuevo.
Por eso el mismo binario que habla con una API de frontera también funciona
contra un servidor llama.cpp en el escritorio de al lado, y por eso seguirá
compilando cuando ambos hayan sido reemplazados.

## 侘寂（わびさび） — la belleza de los límites honestos

El *wabi-sabi* (侘び寂び) encuentra valor en lo imperfecto, lo impermanente y
lo incompleto. La cultura del software suele fingir lo contrario; esta base de
código intenta no hacerlo. La estimación de tokens es una heurística por bytes
y se *sabe* que peca de optimista — así que M77 la calibra contra la realidad,
modelo por modelo, en lugar de reclamar una precisión que no tiene. Los
presupuestos terminan las ejecuciones a mitad de pensamiento, así que M80
aprendió a conservar el trabajo parcial en lugar de quemarlo por no llegar a
estar completo. Todo búfer está acotado, todo tope tiene nombre, y cuando la
salida se trunca, el truncamiento lo declara. Una herramienta que admite sus
límites merece confianza en ellos; una que los oculta no la merece en ninguna
parte.

## Gigantes, y montones de enanos

Estamos sobre los hombros de gigantes — y, con la misma verdad, sobre montones
de enanos: las incontables contribuciones pequeñas sin ningún nombre famoso
adosado. Ambas deudas son reales, y este proyecto rinde respeto a cada una.

Los gigantes son fáciles de nombrar: C y el comité que lo congeló en 1989 en
algo sobre lo que el mundo entero pudo construir; UNIX y POSIX; libcurl, que
ha movido bytes para más humanidad de la que ninguno de nosotros llegará a
conocer; el diseño de la API del cJSON vendorizado; git, cuya fontanería
impulsa silenciosamente la maquinaria de instantáneas y worktrees; el Language
Server Protocol, el Model Context Protocol y el Agent Client Protocol, cada
uno un acto de generosidad que cambió una ventaja privada por un estándar
público; [Continue](https://continue.dev), cuya CLI esto reimplementa y cuyo
diseño demostró que la forma merecía construirse; y los proveedores de
modelos cuyas API le dan al bucle algo con lo que hablar.

Los enanos son más difíciles de nombrar, y esa es precisamente la cuestión:
los autores de páginas de manual, quien respondió en Stack Overflow aquella
oscura pregunta sobre `tcsetattr`, la persona que hace años reportó en otro
proyecto el fallo de la coma decimal según la configuración regional, gracias
a lo cual este supo proteger `LC_NUMERIC`, los revisores de RFC, los
traductores, quienes prueban candidatas de lanzamiento en máquinas extrañas.
Ninguno de ellos, por sí solo, es un gigante. Juntos son el suelo. Un proyecto
que solo acredita a los gigantes ha entendido mal de dónde viene el suelo — y
un proyecto que aspire a importar debería aspirar, sobre todo, a unirse al
montón: a ser el cimiento fiable y sin brillo de alguien más.

## Montañas, y el mar

井の中の蛙大海を知らず、されど空の深さを知る — «la rana del pozo nada sabe
del gran océano; y sin embargo conoce la profundidad del cielo».

El proverbio corta por ambos lados, y aquí importan ambos filos. La
profundidad sin amplitud es el pozo: este proyecto escala su montaña — un
estándar de lenguaje, una familia de plataformas, dominados a fondo. La
amplitud sin profundidad es el océano del turista. La disciplina consiste en
hacer ambas cosas deliberadamente: sesiones de dogfooding contra bases de
código ajenas (las sesiones de zigodot que produjeron una docena de hitos),
una localización que obliga a los supuestos anglocéntricos a salir a la luz
(el trabajo de caracteres anchos de M127 existe porque el texto japonés rompió
la idea de columna del editor de línea), un trabajo de accesibilidad que
pregunta cómo es la interfaz cuando no puedes verla.

Debemos explorar para cambiar de punto de vista. Cada hito genuinamente bueno
de la hoja de ruta se remonta a un cambio de atalaya: ejecutar la herramienta
en vez de escribirla, leer la telemetría en vez de fiarse del diseño,
entregársela a un aprendiz en vez de a un experto. La montaña enseña oficio;
el mar enseña humildad; el pozo, cuidado con honestidad, enseña el cielo. Una
herramienta para aprender debería ser construida por gente aún dispuesta a
aprender — y, en la medida en que pueda lograrlo, ser ella misma una de ellas.

---

*Páginas complementarias: [JOURNEY.md](JOURNEY.md) (el camino del primer
paso a la maestría), [ANECDOTES.md](../../ANECDOTES.md) (lecciones
pagadas), [ROADMAP.md](../../ROADMAP.md) (el libro mayor del kaizen),
[LEARNING.md](../../LEARNING.md) (el bucle del mentor),
[TEACHING_ASSIGNMENTS.md](../../TEACHING_ASSIGNMENTS.md) (pedagogía),
[README.md de i18n](../README.md) (política de localización),
[ACCESSIBILITY.md](../../ACCESSIBILITY.md).*
