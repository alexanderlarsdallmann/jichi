<!-- tracks: ../en/GETTING_STARTED.md (canonical) -->
# Erste Schritte mit jichi

jichi ist ein KI-Programmier-Agent für die Kommandozeile unter Linux. Diese
Seite führt Sie von Null bis zu einer funktionierenden Sitzung. Die
weiterführenden Anleitungen (unten verlinkt) sind auf Englisch.

## 1. Installation

Sie benötigen **libcurl** und einen C-Compiler. Erzeugen Sie die beiden Programme:

```sh
make
```

Das erzeugt `jichi` (den Agenten) und `jichi-convert` (einen Konfigurations-
Importer). Systemvoraussetzungen finden Sie in [INSTALL.md](../../INSTALL.md).

## 2. Konfigurieren

Starten Sie die geführte Einrichtung — sie erkennt die Programmiersprache Ihres
Projekts und schreibt eine Konfiguration:

```sh
jichi setup
```

Für einen minimalen, sicheren Einstieg (gut zum Lernen) fügen Sie `--profile
beginner` hinzu. Ihr API-Schlüssel wird aus einer **Umgebungsvariable** gelesen
(niemals in der Konfiguration gespeichert).

Prüfen Sie, ob alles eingerichtet ist:

```sh
jichi doctor      # Zustandsprüfung
jichi benchmark   # Bewertung der Best-Practice-Abdeckung
```

jichi kann in Ihrer Sprache antworten: setzen Sie `"language": "Deutsch"` in
der Konfiguration oder übergeben Sie `--language` — siehe
[LANGUAGE.md](../../LANGUAGE.md).

## 3. Verwenden

Interaktive Sitzung (eine REPL):

```sh
jichi
```

Formulieren Sie Ihr Anliegen in normaler Sprache. Verweisen Sie mit `@datei` oder
`@sym:name` auf Code. Nützliche Befehle: `/help`, `/model`, `/mode`,
`/constraints`, `/undo`. Mit **Strg-C** brechen Sie die aktuelle Aufgabe ab, ohne
das Programm zu beenden.

Ohne Oberfläche (für Skripte und Automatisierung):

```sh
jichi -p "erkläre, was src/main.c macht"
```

## 4. Die Kontrolle behalten

- **Modi:** chat (fragt vor dem Handeln), plan (nur lesen), auto (autonom).
- **Beschränkungen (constraints):** Sagen Sie „führe den Build nicht aus", und es
  wird *erzwungen*, nicht nur gemerkt — siehe [CONSTRAINTS.md](../../CONSTRAINTS.md).
- **Rückgängig:** Jede Änderung wird als Checkpoint gesichert; `/undo` macht sie
  rückgängig — siehe [SNAPSHOTS.md](../../SNAPSHOTS.md).

## Wie es weitergeht

- Der ganze Weg, vom ersten Schritt zur Meisterschaft:
  [JOURNEY.md](JOURNEY.md)
- Wählen Sie Ihren Weg: [WORKFLOWS.md](../../WORKFLOWS.md)
- Einführung für Einsteiger: [TUTORIAL_BEGINNER.md](../../TUTORIAL_BEGINNER.md)
- Subsysteme für Fortgeschrittene: [TUTORIAL_ADVANCED.md](../../TUTORIAL_ADVANCED.md)
