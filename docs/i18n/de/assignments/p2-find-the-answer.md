---
title: Finde die Antwort und schreibe sie auf
audience: student
phase: implementation
difficulty: plain
points: 1
verify: "grep -qx 'timeout = 30' docs/assignments/p2-find-the-answer/answer.txt && grep -q 'timeout = 30' docs/assignments/p2-find-the-answer/settings.txt"
hints:
  - Bitten Sie den Agenten, settings.txt zuerst zu lesen. Bitten Sie noch nicht um eine Änderung.
  - Eine Zeile in settings.txt beginnt mit dem Wort timeout. Kopieren Sie diese ganze Zeile.
  - "Sagen Sie: lies docs/assignments/p2-find-the-answer/settings.txt und schreibe dann die timeout-Zeile in docs/assignments/p2-find-the-answer/answer.txt. Ersetze, was dort steht."
---
<!-- tracks: ../../../assignments/p2-find-the-answer.md @ c43115c -->

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

Zuerst lesen. Danach ändern.

Ein Agent kann eine Datei ändern, ohne sie zu verstehen. Sie können das auch. Diese
Aufgabe trennt die zwei Schritte. So merken Sie den Unterschied.

## Die Aufgabe

Es gibt eine Datei mit Einstellungen:

```
docs/assignments/p2-find-the-answer/settings.txt
```

Eine Zeile darin setzt einen **timeout** (eine Wartezeit).

1. Bitten Sie den Agenten, die Datei zu **lesen** und Ihnen die timeout-Zeile zu
   nennen.
2. Bitten Sie ihn dann, diese Zeile in diese Datei zu schreiben:

```
docs/assignments/p2-find-the-answer/answer.txt
```

Am Ende steht in der Antwort-Datei **nur** diese eine Zeile.

Kopieren Sie die Zeile genau so, wie sie dort steht. Machen Sie sie nicht schöner.

## Woran Sie merken, dass Sie fertig sind

```sh
# in einem Terminal, im Projektordner (dem mit docs/) -- nicht in jichi
jichi grade docs/assignments/p2-find-the-answer.md
```

Die Prüfung sieht zwei Dinge nach:

- In der Antwort-Datei steht die timeout-Zeile, und
- **die timeout-Zeile steht immer noch in settings.txt.**

Die zweite Prüfung ist Absicht. Sie sollten diese Datei lesen, nicht ändern. Eine
Aufgabe, die nur Ihre Antwort prüft, würde Sie aus dem falschen Grund bestehen
lassen.

Genau gesagt: die zweite Prüfung sucht die Zeile, nicht die unveränderte Datei.
Sie erkennt also den Fehler, den man wirklich macht — die Datei zu ändern, die man
lesen sollte — und würde eine zusätzliche Zeile an anderer Stelle nicht bemerken.
Eine Prüfung, die sagt, was sie tut, ist mehr wert als eine, der man glauben muss.
