---
title: Ändere eine Zeile und nichts anderes
audience: student
phase: implementation
difficulty: plain
points: 2
verify: "grep -qx 'The speed is 80 steps.' docs/assignments/p3-change-one-line/notes.txt && grep -qx 'Line one must not change.' docs/assignments/p3-change-one-line/notes.txt && grep -qx 'Line three must not change.' docs/assignments/p3-change-one-line/notes.txt && [ \"$(wc -l < docs/assignments/p3-change-one-line/notes.txt)\" = 3 ]"
hints:
  - Nur die mittlere Zeile ändert sich. Die anderen zwei Zeilen bleiben genau so, wie sie sind.
  - Nennen Sie den alten Text und den neuen Text. Lesen Sie dann die Vorschau und prüfen Sie, dass nur eine Zeile als geändert markiert ist.
  - "Sagen Sie: ändere in docs/assignments/p3-change-one-line/notes.txt die Zeile 'The speed is 50 steps.' zu 'The speed is 80 steps.' Ändere nichts anderes."
---
<!-- tracks: ../../../assignments/p3-change-one-line.md @ c43115c -->

> **Bevor Sie anfangen.** Öffnen Sie ein **Terminal** in Ihrem Projektordner —
> dem Ordner, der `docs/` enthält. Tippen Sie `jichi` und drücken Sie Enter: das
> ist der Agent, er wartet auf Sie. Noch nie gemacht?
> [`EINFACHE_SPRACHE.md`](../EINFACHE_SPRACHE.md) fängt ganz am Anfang an, mit
> `jichi setup`. jichi zeigt Ihnen jede Änderung und fragt, bevor es schreibt.
>
> Der Befehl `jichi grade …` weiter unten ist anders: den tippen Sie im
> **Terminal**, nicht in jichi. Verlassen Sie jichi vorher mit `/exit`, oder
> nehmen Sie ein zweites Terminal.
## Was Sie lernen

Eine kleine Änderung soll klein bleiben.

Ein Agent, der eine Sache reparieren soll, räumt manchmal noch andere Dinge mit auf.
So wird aus einer Änderung von einer Zeile eine Änderung, die niemand geprüft hat.
Diese Aufgabe bewertet, **was Sie nicht geändert haben**.

## Die Aufgabe

Es gibt eine Datei mit drei Zeilen:

```
docs/assignments/p3-change-one-line/notes.txt
```

In der mittleren Zeile steht, die Geschwindigkeit ist 50 Schritte. Dort soll **80**
stehen.

Bitten Sie den Agenten, diese eine Zeile zu ändern.

Wenn er die Vorschau zeigt, sehen Sie hin, bevor Sie antworten. Prüfen Sie, dass nur
eine Zeile als geändert markiert ist.

## Woran Sie merken, dass Sie fertig sind

```sh
# in einem Terminal, im Projektordner (dem mit docs/) -- nicht in jichi
jichi grade docs/assignments/p3-change-one-line.md
```

Die Prüfung sieht vier Dinge nach:

1. In der mittleren Zeile steht jetzt 80 Schritte.
2. Die erste Zeile ist unverändert.
3. Die dritte Zeile ist unverändert.
4. Die Datei hat noch genau drei Zeilen.

Nummer 4 fängt eine häufige Überraschung: eine Datei bekommt am Ende eine leere
Zeile, ohne dass jemand das wollte.

## Wenn es FAIL zeigt

Raten Sie nicht. Lesen Sie die Datei und vergleichen Sie sie mit den vier Regeln
oben. Bitten Sie den Agenten dann, nur das zu reparieren, was falsch ist.

Sie können auch alles zurücknehmen und neu anfangen. Das tippen Sie **in
jichi**, an dessen Eingabezeile — es ist kein Terminal-Befehl:

```text
/undo
```
