---
title: Bitte um eine Datei
audience: student
phase: implementation
difficulty: plain
points: 1
verify: "grep -qx 'I asked and it wrote this line' docs/assignments/p1-ask-for-a-file/note.txt"
hints:
  - Sie schreiben die Datei nicht. Sie bitten den Agenten darum. Dann lesen Sie, was er ändern will, und Sie sagen ja.
  - Nennen Sie den genauen Pfad und die genaue Zeile. Wenn Sie unklar sind, rät der Agent.
  - "Sagen Sie: erstelle die Datei docs/assignments/p1-ask-for-a-file/note.txt mit dieser einen Zeile: I asked and it wrote this line"
---
<!-- tracks: ../../../assignments/p1-ask-for-a-file.md @ c43115c -->

> **Bevor Sie anfangen.** Öffnen Sie ein **Terminal** in Ihrem Projektordner —
> dem Ordner, der `docs/` enthält. Tippen Sie `jichi` und drücken Sie Enter: das
> ist der Agent, er wartet auf Sie. Noch nie gemacht?
> [`EINFACHE_SPRACHE.md`](../EINFACHE_SPRACHE.md) fängt ganz am Anfang an, mit
> `jichi setup`. jichi zeigt Ihnen jede Änderung und fragt, bevor es schreibt.
>
> Der Befehl `jichi grade …` weiter unten ist anders: den tippen Sie im
> **Terminal**, nicht in jichi. Verlassen Sie jichi vorher mit `/exit`, oder
> nehmen Sie ein zweites Terminal.
Das ist Ihre erste Aufgabe. Sie ist absichtlich klein.

## Was Sie lernen

Sie machen eine ganze Runde Arbeit mit dem Agenten:

1. Sie bitten.
2. Der Agent zeigt Ihnen, was er ändern will.
3. Sie lesen das.
4. Sie sagen ja.
5. Ein Befehl prüft das Ergebnis.

Schritt 3 ist der wichtige. Um diese Gewohnheit geht es im ganzen Kurs.

## Die Aufgabe

Bitten Sie den Agenten, eine neue Datei zu machen. Die Datei ist:

```
docs/assignments/p1-ask-for-a-file/note.txt
```

In der Datei steht genau diese eine Zeile und nichts anderes:

```
I asked and it wrote this line
```

**Schreiben Sie die Datei nicht selbst.** Bitten Sie den Agenten darum.

> **Warum ist die Zeile auf Englisch?** Der Prüfbefehl ist derselbe wie in der
> englischen Aufgabe. So gibt es nur eine Prüfung, und beide Sprachen können nicht
> auseinanderlaufen. Die Zeile ist Text für den Computer, nicht für Sie.

## Woran Sie merken, dass Sie fertig sind

Geben Sie das ein:

```sh
# in einem Terminal, im Projektordner (dem mit docs/) -- nicht in jichi
jichi grade docs/assignments/p1-ask-for-a-file.md
```

Es zeigt **drei Zeilen**. So sieht ein Erfolg aus:

```text
Ask for one file: PASS
  verify: grep -qx 'I asked and it wrote this line' docs/assignments/p1-ask-for-a-file/note.txt (exit 0)
  score: 100%
```

Das Wort in der **ersten** Zeile ist die Antwort: `PASS` oder `FAIL`. Die zweite
Zeile zeigt den Befehl, der geprüft hat, die dritte Ihre Punktzahl. Bei einem
Fehlschlag stehen dort dieselben drei Zeilen mit `FAIL`, `(exit 2)` und
`score: 0%`. Die Ausgabe ist englisch, auch wenn diese Seite deutsch ist.

- **PASS** heißt: die Datei ist richtig. Sie sind fertig.
- **FAIL** heißt: etwas ist anders. Lesen Sie die Datei. Vergleichen Sie sie
  Buchstabe für Buchstabe mit der Zeile oben. Ein Leerzeichen zu viel reicht schon.

## Eine Warnung

Der Agent zeigt Ihnen eine Vorschau, bevor er schreibt. Lesen Sie sie. Es ist
leicht, ohne Hinsehen ja zu sagen. Diese Gewohnheit kostet Sie später etwas — bei
einer Aufgabe, in der der Agent falsch liegt.
