# jichi in Einfacher Sprache

> **Auch auf Englisch:** [Plain language](../../PLAIN_LANGUAGE.md).
>
> Diese Seite ist **kein Ersatz** für die anderen Seiten. Sie ist eine eigene Seite.
> Die anderen Seiten sind ausführlich. Das ist für ihre Leser richtig. Diese Seite
> ist einfach. Das ist für ihre Leser richtig.

## Was ist jichi?

jichi ist ein Programm für die Kommandozeile.

Es hilft beim Programmieren.

Sie schreiben eine Aufgabe. jichi liest Ihre Dateien. jichi schlägt Änderungen vor.
Sie sagen ja oder nein.

jichi arbeitet in dem Ordner, in dem Sie es starten.

## Was Sie brauchen

Sie brauchen drei Dinge:

1. Einen Computer mit Linux.
2. Ein Sprachmodell. Das ist ein Dienst im Internet oder auf Ihrem Computer.
3. Einen Schlüssel für diesen Dienst. Der Schlüssel ist wie ein Passwort.

## jichi bekommen

jichi ist ein einzelnes Programm. Sie bauen es einmal aus dem Quellcode:

```
make
```

Danach gibt es eine Datei `jichi` in dem Ordner. Sie können diese Datei in einen
Ordner Ihres `PATH` kopieren. `PATH` ist die Liste der Ordner, in denen Ihr Computer
nach Programmen sucht. Oder Sie rufen das Programm als `./jichi` auf.

Wenn `make` nicht geht, fehlt ein Bauwerkzeug. Lesen Sie dann
[INSTALL.md](../../INSTALL.md).

## Der erste Start

**Wo Sie das alles eintippen:** in einem **Terminal**, im Ordner des Projekts,
an dem Sie arbeiten wollen. Jeder Befehl auf dieser Seite wird dort eingetippt.
Die Ausnahme sind Befehle mit einem Schrägstrich davor (`/exit`, `/undo`) —
die tippen Sie *in* jichi, wenn es läuft, und diese Seite sagt das jedes Mal.

Geben Sie das ein:

```
jichi setup
```

jichi stellt Fragen. Sie antworten. jichi schreibt danach eine Datei mit Ihren
Einstellungen. Sie müssen diese Datei nicht öffnen.

Prüfen Sie dann, ob alles geht:

```
jichi doctor
```

`doctor` zeigt eine Liste. Ein Häkchen bedeutet: das ist in Ordnung. Ein Kreuz
bedeutet: hier fehlt etwas. Neben dem Kreuz steht, was fehlt.

**Wenn jede Zeile ein Häkchen hat, ist jichi bereit.**

## Eine Aufgabe geben

Starten Sie jichi:

```
jichi
```

Jetzt können Sie schreiben. Zum Beispiel:

```
Erkläre mir die Dateien in diesem Ordner
```

(Wenn Sie eine Datei nennen, nehmen Sie eine, die es gibt. `src/main.c` gibt es nur
in einem Projekt, das diese Datei hat.)

jichi antwortet.

## Drei Arbeitsweisen

jichi hat drei Arbeitsweisen. Sie heißen Modi.

| Modus | Was passiert |
|---|---|
| **chat** | jichi fragt, bevor es eine Datei ändert. Das ist die normale Weise. |
| **plan** | jichi ändert nichts. Es macht nur einen Plan. |
| **auto** | jichi arbeitet allein. Es fragt nicht. |

Sie wechseln so:

```
/chat
/plan
/auto
```

**Achtung bei auto:** In diesem Modus ändert jichi Dateien ohne zu fragen. Nutzen
Sie `auto` nur, wenn Sie das wirklich wollen.

## Wichtige Befehle

Diese Befehle beginnen mit einem Schrägstrich.

| Befehl | Was er macht |
|---|---|
| `/help` | zeigt alle Befehle |
| `/status` | zeigt, welches Modell und welcher Modus gerade aktiv ist |
| `/undo` | nimmt die letzten Änderungen zurück |
| `/cost` | zeigt, was diese Sitzung gekostet hat |
| `/exit` | beendet jichi |

## Wenn etwas schiefgeht

jichi macht Fehler. Das ist normal.

Sie können Änderungen zurücknehmen:

```
/undo
```

jichi speichert vor jeder Änderung einen Stand. `/undo` holt diesen Stand zurück.

Wenn jichi nicht antwortet, drücken Sie **Strg + C**. Das bricht die Arbeit ab.

## jichi vorlesen lassen

jichi kann sprechen. Das hilft, wenn Sie den Bildschirm nicht lesen können.

```
/voice on
```

Dann liest jichi die Antworten vor. jichi liest auch vor, wenn es um Erlaubnis
fragt. Denn wenn jichi wartet und nichts sagt, wissen Sie nicht, warum es still ist.

Sie brauchen dafür zwei Einstellungen. Wenn eine fehlt, sagt jichi das. jichi bleibt
dann still — aber es sagt Ihnen vorher, warum.

Mehr dazu: [VOICE.md](../../VOICE.md) (auf Englisch).

## Ihre Daten

- jichi schickt Ihre Fragen und Teile Ihrer Dateien an das Sprachmodell. Das ist
  nötig, damit das Modell antworten kann.
- jichi speichert die Gespräche auf Ihrem Computer, in `~/.jichi.d/`.
- jichi speichert dort auch Zahlen zu jedem Lauf: wie lange ein Aufruf dauerte,
  welche Werkzeuge benutzt wurden. Nicht Ihre Worte und nicht Ihren Code. Aus
  diesen Zahlen lernt jichi. Sie können das mit `--log-level off` abschalten.
- jichi schickt keine Daten an andere Stellen.

## Vorsicht

jichi kann Befehle auf Ihrem Computer ausführen. Das ist nützlich und gefährlich.

Deshalb:

- Nutzen Sie **git** in Ihrem Ordner. git ist ein Programm. Es speichert jede
  Version Ihrer Dateien. Mit git sehen Sie jede Änderung von jichi. Und Sie können
  jede Änderung zurücknehmen.
- Lesen Sie, was jichi vorschlägt, bevor Sie ja sagen.
- Seien Sie besonders vorsichtig im Modus `auto`.

Texte aus dem Internet sind für jichi nur Daten. jichi soll Anweisungen darin nicht
befolgen. jichi markiert solche Texte. Das hilft. Es ist aber keine Garantie.

## Üben mit Bewertung

Es gibt drei kleine Übungen in Einfacher Sprache. Jede Übung hat eine Prüfung. Die
Prüfung zeigt drei Zeilen, und die erste endet mit **PASS** oder **FAIL**. Das rät nicht, das prüft.

| Übung | Punkte | Was Sie lernen |
|---|---|---|
| [p1 — Bitte um eine Datei](assignments/p1-ask-for-a-file.md) | 1 | eine ganze Runde: bitten, Vorschau lesen, ja sagen, prüfen |
| [p2 — Finde die Antwort](assignments/p2-find-the-answer.md) | 1 | zuerst lesen, dann ändern |
| [p3 — Ändere eine Zeile](assignments/p3-change-one-line.md) | 2 | eine kleine Änderung bleibt klein |

So starten Sie eine Übung:

```
jichi assign docs/i18n/de/assignments/p1-ask-for-a-file.md
```

So prüfen Sie Ihr Ergebnis:

```
jichi grade docs/i18n/de/assignments/p1-ask-for-a-file.md
```

Die Übungen werden **genauso streng** bewertet wie alle anderen. Eine leichtere Note
wäre nett und würde Ihnen nichts beibringen.

Ein Hinweis: In den Dateien der Übungen stehen englische Sätze. Das ist Absicht. Die
Prüfung ist in beiden Sprachen dieselbe. So können die deutsche und die englische
Übung nicht auseinanderlaufen. Die englischen Sätze sind Text für den Computer.

## Wo Sie weiterlesen

Die anderen Seiten sind auf Englisch und ausführlicher:

- [GETTING_STARTED.md](GETTING_STARTED.md) — der erste Start (auf Deutsch).
- [`docs/TUTORIAL_BEGINNER.md`](../../TUTORIAL_BEGINNER.md) — ein längeres Tutorial.
- [`docs/AGENT_MODES.md`](../../AGENT_MODES.md) — die Modi genau erklärt.
- [`docs/ACCESSIBILITY.md`](../../ACCESSIBILITY.md) — Barrierefreiheit.

## Über diese Seite

Diese Seite folgt den Regeln der Einfachen Sprache:

- kurze Sätze,
- ein Gedanke pro Satz,
- keine Bilder und Vergleiche,
- keine verschachtelten Nebensätze,
- konkrete Wörter,
- Fachwörter werden erklärt.

Es ist eine **eigene Seite** und keine Kürzung einer anderen. Die ausführlichen
Seiten bleiben ausführlich. Beide Arten von Text haben ihre Leser.
