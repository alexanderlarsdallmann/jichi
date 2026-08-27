---
marp: true
title: jichi — mit KI bauen
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/06-building-with-ai.md @ 0632b94 -->

<!-- _class: lead -->

# jichi bauen

### Ein Entwickler, unterstützt von einem KI-Agenten

Ein Rückblick auf die *Methode* — die Zahlen, die Liefermodelle und das, was der
Mensch tatsächlich tat. Alle Daten: `docs/PROJECT_TIMELINE.md`.

---

# Das Projekt in Zahlen

| Kennzahl | Wert |
|---|---|
| Kalenderspanne | 2026-06-18 → 2026-07-24 (**37 Tage**, **19 aktiv**) |
| Commits | **378** |
| Meilensteine | **M1 – M173** |
| Quellcode (`src`+`include`) | **~70.400 Zeilen** C89 |
| Tests | **~43.300 Zeilen**, **10.000+ Assertions** |
| Dokumentation | **~24.600 Zeilen**, 122 Dateien |
| Qualitäts-Gates | `-Werror` (gcc+clang), ASan/UBSan, valgrind, fuzz, e2e |

Ein Verhältnis von **1 : 0,36 : 0,35** Code : Tests : Docs — Tests und Docs
gehörten bei jedem Meilenstein zur Definition of Done.

---

# Sechs Phasen

```
P0  Fundament             Jun 18-24   M1-M20    Kern-Substrat, Provider, Loop
P1  Retrieval & Protokolle Jun 24-26  M21-M50   Cache, Kompaktierung, RAG, MCP
P2  Integrationen/Autonomie Jun26-Jul1 M50-M80  LSP, ACP, Envelope, Subagenten
P3  Härtung/Selbstverbess. Jul 1-10   M80-M106  Dogfood -> fundierte Leitplanken
P4  Suite & Release        Jul 13-15  M107-M134 Suite, Fuzzing, Sicherheitsband
P5  Post-Release-Tiefe     Jul 23-24  M135-M173 Schleifen, Steuerung, Robotik
```

Eine Lücke von 9 Tagen trennte die Release-Härtung (P4) von einer eigenständigen
**Welle neuer Fähigkeiten nach dem Release** (P5): autonomer Betrieb,
Observability, der Steuerkanal und der Vorstoß in den Einsatz an echter,
robotischer Hardware.

---

# Entwicklungsintensität (Commits pro aktivem Tag)

```
Jun18 ## 6      Jun30 ############ 24
Jun19 ########## 20   Jul01 #### 8
Jun22 ########## 20   Jul02 ###### 12
Jun23 ################## 36   Jul06 ###### 12
Jun24 ############### 31   Jul07 ##### 9
Jun25 ################## 36   Jul08 ################# 33
Jun26 ######## 16   Jul09 ################# 33
                    Jul10 ##### 10
                    Jul13 ################## 36
                    Jul14 #### 8
                    Jul23 ########## 20
                    Jul24 #### 8      -> 378 gesamt
```

Die hohe Commit-Rate blieb **sicher**, weil das Qualitäts-Gate automatisiert und
hart war.

---

# Die Meilenstein-Schleife (das wiederverwendbare Artefakt)

```
Anforderung  ->  Design (Naht + reiner Kern + dünne Shell; oft ein Proposal)
             ->  Implementierung in C89
             ->  Tests (Unit-Tests des reinen Kerns + e2e/PTY-Smoke)
             ->  Gate: -Werror + ASan/UBSan + valgrind + e2e  --(rot)--> Implementierung
             ->  Docs + ROADMAP-Notiz
             ->  klar umrissener, am Ergebnis abgesicherter Commit  ->  (nächster)
```

~160 Meilensteine, jeder eine kleine, bewusst entworfene Einheit. Die Historie
liest sich *wegen* dieser Disziplin wie eine Erzählung — und sie hielt die KI
über 378 Commits hinweg fehlerfrei, ohne Regressionen.

---

# Vier Wege, denselben Umfang zu liefern

Menschlicher Aufwand für den gleichen **M1–M173**-Umfang (Personenmonate, Mittelwert):

```
KI-unterstützt (1 Dev + KI)| ~1    <- tatsächlich (~5 Wochen, 19 aktive Tage)
Experte solo              |#################### ~21   (~20-26 Monate)
Team von ~6               |###################################### ~40  (~5-6 Monate)
Junior solo               |######################################################## ~60 (~5-6 J.)
```

- **KI-unterstützt** = *menschliche* Aufsichtszeit (Design + Review + Steuerung);
  ohne Modell-Compute.
- Ein **Team** kostet mehr *Gesamt*-Aufwand als der Experte solo
  (Koordinationsaufwand), aber deutlich weniger *Kalenderzeit* — die bekannte
  Abwägung zwischen Aufwand und Termin.
- Der **Junior** trägt ein Risiko der Unvollständigkeit, nicht nur der
  Langsamkeit: mehrere Subsysteme sind ohne Mentoring in dieser Qualität kaum zu erreichen.

---

# Wohin die menschliche Zeit floss

```
Anleitung + Anforderungen (was & warum)        ##############  30%
Design-Review + Freigabe von Proposals           ############    25%
Diffs reviewen + Laufzeit-Steuerung                 ############    25%
Prioritäten / Reihenfolge der Bänder               ######          12%
Ergebnisse verifizieren / CI lesen                   ####            8%
```

Der Entwickler schrieb **fast kein C**. Der Schwerpunkt verlagerte sich nach
oben: das nächste Band wählen, ein Design freigeben, eine falsche Annahme
abfangen, einen Lauf anhalten.

---

# Warum die Hygiene *mehr* zählte, nicht weniger

Dieselben Disziplinen, die ein **Team** skalieren lassen, machten
**KI-Unterstützung** vertrauenswürdig:

- **Enge Meilensteine** → reviewbare Einheiten, eine saubere Historie.
- **Eine Design-Notiz vor dem Code** → der Mensch prüft zuerst die *Absicht*, und das günstig.
- **Testbare reine Kerne** → 10.000+ Offline-Assertions; schnelle, hermetische CI.
- **Ein hartes Qualitäts-Gate** → Regressionen sofort erwischt; hohe Commit-Rate bleibt sicher.
- **Ein Doc + Commit pro Einheit** → die Arbeit ist lesbar und fortsetzbar.

> Die KI ersetzt die Ingenieursdisziplin nicht — sie **steigert ihre Rendite**.

---

# Ehrliche Grenzen

- Die **Personenmonate** für die drei menschlichen Modelle sind Schätzungen
  (±40%), von unten herauf aus dem gelieferten Umfang — keine gemessenen Läufe.
- Die **KI-unterstützte** Zahl ist die einzige direkt gemessene (Kalender + aktive
  Tage), zählt aber nur *menschliche* Zeit; **Modell-Compute ist ein realer
  Kostenpunkt**, in Tokens bezahlt.
- Eine **Referenzimplementierung** (die Continue CLI) senkte das Anforderungsrisiko
  in P0–P4; die neuartigen P5-Bänder hatten keine — deshalb begann jedes mit einem Proposal.
- Ein Projekt ist ein Datenpunkt. Die übertragbare Aussage ist die **Methode**,
  kein allgemeingültiger Multiplikator.

---

<!-- _class: lead -->

# Die Kernbotschaft

**KI-Unterstützung verdichtet Implementierung, Tests und Dokumentation.
Design und Aufsicht bleiben menschlich — und bestimmen die Qualitätsobergrenze.**

Investiere die gewonnene Zeit dort, wo es jetzt am meisten zählt: zu entscheiden,
*was* zu bauen ist, und zu beurteilen, *ob das Ergebnis stimmt*.

`docs/PROJECT_TIMELINE.md` · `docs/ROADMAP.md` · `docs/ANECDOTES.md`
