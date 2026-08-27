---
marp: true
title: jichi an der Universität
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/04-university.md @ 0632b94 -->
<!-- slides-behind: 2 (en has 12 slide separators, this has 10). The English deck
     gained slides this translation does not carry. Translating them is prose in a
     language this repository cannot review, so the gap is DECLARED rather than
     filled -- and the numbers above are checked, so the declaration cannot be left
     behind silently either. M582. -->

<!-- _class: lead -->

# jichi an der Universität

### Forschung, Lehre, Reproduzierbarkeit

---

# Warum es in den akademischen Alltag passt

- **Läuft auf der Hardware, die da ist** — ein gemeinsam genutzter Login-Knoten,
  eine alte Laborkiste, ein Raspberry Pi, ein Handy in Termux. ~1,2 MB,
  ~10–17 MB RSS, spricht mit einem lokalen oder entfernten Modell.
- **Kein Vendor-Lock-in** — richte es auf einen Instituts-LLM-Server, ein lokales
  llama.cpp/vLLM oder eine kommerzielle API. Lokale Server ohne API-Key laufen sofort.
- **Nachprüfbar** — es ist C89 mit Test-Suite; Studierende können den Agenten
  *lesen*, nicht bloß benutzen.
- **Kostenbewusst** — Prompt-Caching und Live-`/cost`; Budgets deckeln einen autonomen Lauf.

---

# Für Forschungssoftware

- **Eine Alt-Codebasis schnell verstehen** — `@folder:`-Übersichten,
  `codebase_search` und `search_docs` über das Referenzmaterial des Papers.
- **Reproduzierbare Läufe** — Headless `-p` + `--output json`, gesteuert aus einem
  Makefile oder Slurm-Job; das JSONL-Journal ist dein Herkunftsnachweis.
- **Autonomie an der Leine** — `--auto --verify "make test" --budget-*`, damit ein
  nächtliches Refactoring nicht entgleist oder den Build heimlich zerlegt.
- **Fallstudie: der zigodot-Rewrite** — ein großer, autonomer Sprach-Port,
  vollständig von jichi gesteuert, mit Telemetrie, die genau zeigt, wohin der Aufwand floss.
- **Embodied- / Robotik-Forschung** — jichi als *planende* Schicht eines Roboters
  (Sensoren und Aktoren als Tools, ein kinetisches Sicherheits-Gate, Sound-I/O);
  ein hardwarefreier Simulator liegt in `examples/robot-sim/` bei (`docs/ROBOTICS.md`).
- **Software-Engineering & PM als Fach** — `docs/PROJECT_TIMELINE.md` ist eine
  datengestützte Retrospektive (Phasen, Intensität, vier Liefermodelle inkl. „ein
  Entwickler + KI"), einsetzbar in einem PM- oder SE-Kurs.

---

# Für einen Kurs

- Das **assignments**-Feature: Lehrende schreiben Aufgabenstellung,
  Bewertungsraster und eine Hinweis-Leiter; die Referenzlösung bleibt unter
  Verschluss; die Benotung erfolgt **nur lesend** und nach Raster. (Siehe
  `docs/TEACHING_ASSIGNMENTS.md`.)
- **Einheitliche Benotung über alle TAs hinweg** — gleiches Raster, gleiche
  Referenz, nur lesender Prüfer: weniger Streuung zwischen den Bewertenden.
- **Der Agent ist lesbar** — eine Aufgabe „Wie funktioniert eine Agent-Schleife?"
  kann direkt auf `jc_agent_run_turn` in *dieser* Codebasis verweisen.

---

# Reproduzierbarkeit & Herkunft

```sh
jichi --auto \
  --verify "pytest -q" \
  --budget-tokens 300k \
  --journal artifacts/run-$(date +%s).jsonl \
  --output json \
  -p "implement the FFT variant from the spec and pass the tests" \
  > artifacts/result.json
```

Jeder Modell- und Tool-Aufruf, jede Kosten- und Ergebnisangabe wird erfasst —
leg es dem Laborbuch oder dem Artefakt des Papers bei.

---

# Ressourcenschonend & remote

- **`--lite`** schaltet die schweren Subsysteme mit einem einzigen Flag ab
  (Snapshots, Repo-Map, Parallelität) — ein winziger Fußabdruck auf knappen Knoten.
- **SSH + tmux** — starte einen langen `--auto`-Lauf auf der GPU-Box, klink dich
  aus und später wieder ein (`docs/REMOTE_SSH.md`, `docs/TMUX.md`).
- **Der Daemon** — ein warmer Prozess auf einem Laborserver bedient viele schnelle
  Anfragen, ohne Config und Index jedes Mal neu zu laden.

---

# Datenschutz & Data Governance

- Läuft gegen ein **selbst gehostetes** Modell, sodass Studierendencode und
  Forschungsdaten die Institution nie verlassen müssen.
- **Path-Fence** und **Edit-Scope** begrenzen, was ein autonomer Lauf anfassen
  kann; **Referenz-Roots** erlauben nur lesenden Zugriff auf gemeinsame Korpora,
  ohne Schreibrisiko.
- Secrets kommen aus `apiKeyEnv`-Umgebungsvariablen — nie in die Config
  geschrieben, in Logs geschwärzt.

---

# Ein konkreter Platz im Lehrplan

> *„Woche 9: agentische Tools."* Studierende lesen `jc_agent_run_turn`, ergänzen
> hinter der Registry ein neues Built-in-Tool und sehen zu, wie das Modell es
> aufruft. Die ganze Schleife ist ~eine Funktion, die Tool-Schnittstelle ein
> Struct. Das entmystifiziert „KI-Agenten".

---

<!-- _class: lead -->

# Fang hier an

```sh
jichi setup --preset developer
# für die Lehre:
jichi init assignments   # + "assignments": true in der Config
```

`docs/TUTORIAL_ADVANCED.md`, `docs/TEACHING_ASSIGNMENTS.md`, `docs/LOW_MEMORY.md`.
