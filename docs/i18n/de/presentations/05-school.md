---
marp: true
title: jichi in der Schule
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

# jichi im Klassenzimmer

### Programmieren lernen *mit* einem Agenten — sicher

---

# Die Sorge, offen benannt

> „Wenn die KI den Code schreibt — lernen die Schüler dann überhaupt etwas?"

Das assignments-Feature von jichi ist fürs Gegenteil gemacht: Der Agent ist ein
**Coach mit Regler**, keine Antwortmaschine. Die Lernenden machen die Arbeit
selbst; die Hilfe ist dosierbar, und die Bewertungskriterien liegen offen.

---

# Wie eine Unterrichtsstunde abläuft

```
teacher: /assign implementation "a function that reverses a list"
         → docs/assignments/reverse-list.md  (Aufgabenstellung + Raster + Hinweis-Leiter)
student: bearbeitet es im Editor; hängt fest? → ein Hinweis nach dem anderen (hint-Tool)
         immer noch fest? → eine gezielte Frage (ask_for_help)
teacher: /check reverse-list.md student.py  → Feedback nach Raster (nur lesend)
```

Die Referenzlösung bleibt **verborgen**; das Raster liegt **offen**.

---

# Die Hinweis-Leiter ist das didaktische Prinzip

- Feststecken wird zum **Regler** statt zur Wand.
- Hinweise sind **abgestuft** — der sanfteste Anstoß zuerst, der volle Spoiler zuletzt.
- Die Lernenden investieren ihr „Ringen-Budget" produktiv.
- `ask_for_help` gibt eine gezielte Antwort auf eine *konkrete* Verständnislücke,
  keinen Mach-meine-Hausaufgaben-Prompt.

> Stimme die Leiter auf Alter und Niveau ab. Genau dieses Abstimmen *ist* das Unterrichten.

---

# Leitplanken, die es klassenzimmersicher machen

- **Plan- / Read-only-Modus** — Lernende erkunden, ohne gemeinsames Material zu verändern.
- **Path-Fence** — der Agent kommt an keine Datei außerhalb des Lektionsordners.
- **Read-only-Benotung** — der `solution-checker` verändert den Code der Lernenden nie.
- **Nur-Vorschlag-Autorenschaft** — die Lehrkraft gibt jede Aufgabe und Referenz
  frei, bevor sie ausgeteilt wird.
- **Selbst gehostetes Modell** — die Arbeit der Kinder bleibt auf dem Schulserver.

---

# Gestufte Lernprofile führen gute Gewohnheiten vor

Vier Profile zeigen, *wie* man ein Problem auf seinem Niveau angeht:

- **`learner-junior`** — stützt sich auf Hinweise, Hilfe und Delegation.
- **`learner-student`** — nutzt Referenzen, mäßige Hilfe.
- **`learner-senior`** — minimale Hilfe, plant zuerst.
- **`learner-agent`** — strategisch und effizient; die Maschinen-Stufe.

Zuzusehen, wie ein Profil ein Problem angeht, stößt ein Unterrichtsgespräch über
*Strategie* an — nicht bloß über die Antwort.

---

# Was die Lehrkraft tatsächlich tut

1. Einmalig einen kleinen Satz Aufgaben erstellen (`/assign`, die Entwürfe prüfen).
2. Die Aufgabenstellungen austeilen (die `.solution.md`-Dateien zurückbehalten).
3. Herumgehen; hängt jemand fest, zum *nächsten Hinweis* coachen, nicht zur Antwort.
4. Einreichungen mit `/check` für eine einheitliche Erstnote, die du dann prüfst.

Kein neues Werkzeug nötig — es ist dasselbe `jichi`-Binary.

---

# Über Aufgaben hinaus

- **„Erklär das"** — `:JichiExplain` über ein Snippet im Editor, oder `jichi-nano
  explain file.py` — ein geduldiger Erklärer auf Abruf.
- **Code-Review als Lektion** — `/check` oder ein Review-Agent macht aus einer
  Einreichung beschriftetes, mit Belegen unterlegtes Feedback.
- **Läuft auf einem Rechner der Chromebook-Klasse** über das Netz zu einem Schul-LLM-Server.

---

<!-- _class: lead -->

# Loslegen (Lehrkraft)

```sh
jichi init assignments        # das Pack anlegen
# "assignments": true zur Config hinzufügen, dann:
jichi -p "/assign implementation 'sum a list'"
```

Vollständige Anleitung: `docs/TEACHING_ASSIGNMENTS.md` (Klassenzimmer, Nachhilfe,
Selbststudium, Kohorte/TA).
