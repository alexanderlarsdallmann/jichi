---
marp: true
title: jichi — Roadmap
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/03-roadmap.md @ 0632b94 -->
<!-- slides-behind: 2 (en has 15 slide separators, this has 13). The English deck
     gained slides this translation does not carry. Translating them is prose in a
     language this repository cannot review, so the gap is DECLARED rather than
     filled -- and the numbers above are checked, so the declaration cannot be left
     behind silently either. M582. -->

<!-- _class: lead -->

# jichi

### Woher es kommt, wohin es geht — und wie es entstand

---

# Wie es entstand

- **In Meilensteinen, nicht auf einen Schlag.** M1 (Skelett) → stetiger Ausbau
  der Fähigkeiten → **M173** heute, über sechs Phasen (Fundament → Protokolle →
  Autonomie → Härtung → Release → Post-Release-Tiefe).
- Jeder Meilenstein ist ein klar umrissener Plan; Design und Konsequenzen stehen
  in `docs/ROADMAP.md` (mit **thematischem Index** oben), bei neuartiger Arbeit
  zusätzlich in einem `docs/proposals/*.md`.
- Jeder Meilenstein landet mit **Tests** und **null Warnungen** — die Suite
  umfasst **11.000+ Prüfungen**, wächst mit jedem Feature und steht hinter einem
  `make ci`, das den Commit erst bei grünem Ergebnis freigibt.

---

# Die Fähigkeiten im Überblick (alles ausgeliefert)

- **Kern & Kontext:** Loop, zwei Provider, Kompaktierung, Kalibrierung, Prompt-Cache.
- **Bearbeiten:** robuste Fuzzy-Patches, atomarer Multi-Edit, Unified-Diffs.
- **Wissen:** hybrides RAG (BM25 + Embeddings + Rerank), Docs-Index, PDFs, RSS.
- **Autonomie:** Envelope (Budgets/Verify/Edit-Scope/Journal), Snapshots, Rewind.
- **Skalierung:** Subagenten (2 Ebenen tief), parallele Worktrees, warmer Daemon + Worker-Pool.
- **Integrationen:** MCP-Client, ACP-Server, LSP-Navigation + Refactorings, Editoren.
- **Medien & Sound:** Bild-/Audio-Erzeugung, Transkription, Wiedergabe/Aufnahme.
- **Sicherheit:** Path-Fence, Gate für privilegierte Befehle, kinetisches Gate — alle protokolliert.

---

# Der Bogen des autonomen Betriebs (M157–M162)

Ein unbeaufsichtigter Lauf ist jetzt **begrenzt, beobachtet, per Gate
abgesichert, steuerbar und abfragbar**:

| Band | Was dazukam |
|---|---|
| **M157** Schleifen | tmux/systemd/cron-Supervisor über eine Task-Queue + Referenz-Pack |
| **M158** Observability | `runs`- / `audit`-Leser; `doctor --unattended`-Gate; Docs↔Flags-Lint |
| **M159/M162** Steuerung | Unix-Socket zur Laufzeit: `status` / `inject` / `pause[--extend]` / `resume` / `abort` |
| **M160** maschinenlesbar | `--since`-Fenster + `--output json` für `runs`/`audit` |
| **M161** Herkunft | `runs` markiert einen vom Betreiber **gesteuerten** Lauf |

Jeder Eingriff hinterlässt eine Spur; jede Spur hat einen Leser.

---

# Der Vorstoß in die physische Welt (M163)

jichi als der **planende Kopf** eines Roboters — Sensoren, Aktoren, Sound, Flotten:

- **Geräte sind Tools** (User-Tools / MCP-Server, die JSON sprechen); jichi bindet
  keine Gerätebibliothek ein — es ruft externe Befehle auf, wie bei `pdftotext`.
- **Kinetisches Gate** — alles, was Masse oder Energie bewegt, ist
  `kinetic: true` und wird **unterhalb der Berechtigungs-Entscheidung**
  abgesichert (ein pauschales Auto-Approve kann es nicht erfüllen); die Allowlist
  wird zuerst geprüft (für den Not-Aus), ein Umgehen über die Shell per
  Shadow-Match erkannt, jeder Versuch protokolliert.
- **Sound-I/O** — `play_audio` / `record_audio` über konfigurierte Befehle.
- **Ehrlicher Umfang:** jichi arbeitet im **Sekundenbereich**; Reflexe und der echte
  Not-Aus sitzen darunter in der Firmware. Am Simulator nachgewiesen, die Hardware
  ist zurückgestellt.

---

# Die Bänder Mitte bis Ende Juli (M135–M156)

| Thema | Was dazukam |
|---|---|
| **Sprich deine Sprache** | `language`-Key + lokalisierter Freigabe-Prompt (en/de/es/ja/zh) |
| **Integrität** | `apply_patch`-Rollback in der Schreibphase; Auto-Revert außerhalb des Scopes |
| **Ressourcen** | Arena-Lebensdauer korrigiert, `/context`-Anzeige, mmap-Index, `--lite`-Release |
| **Kleine Modelle** | native Tool-Calling-Hinweise, Arg-Reparatur, small-local-Preset + Packs |
| **Privilegierte Sicherheit** | sudo/doas-Gate unterhalb der Freigabe + stets aktives Audit (M152–M155) |
| **Eingabe** | mehrzeiliges Einfügen in den TUI-Prompt (M156) |

---

# Das Selbstverbesserungs-Band (M100+)

Entworfen in `docs/SELF_IMPROVEMENT.md`:

- **Daemon** — warmer Prozess, begrenzter Worker-Pool.
- **Assign/Grade-Harness** — maschinenprüfbare Evals.
- **Dream** — „Schlafkonsolidierung" (nur Vorschläge) über die Telemetrie im Leerlauf.
- **Lernschleife** — speist die eigenen Logs des Agenten als dauerhafte Lektionen zurück.
- **Synthese-Schleife** — führt das Ganze zusammen.

Der rote Faden: ein Coding-Agent, der bei *diesem* Projekt *messbar* besser wird.

---

# Was uns Dogfooding gelehrt hat

Aus `docs/ANECDOTES.md` — je ein Bug, der eine bleibende Lehre mitgab:

- Observability gehört **außerhalb** des Snapshot-/Rollback-Wirkungsbereichs.
- Ein erschöpftes Budget soll **anhalten**, nicht **verwerfen** — verifizieren, dann den grünen Stand behalten.
- Beim „das kann ich nicht" fehlt oft nur die **Ankündigung** eines Tools, nicht
  die Fähigkeit selbst (das toolProfile-Gate).
- **Gate den Commit am Ergebnis, nicht am Log-Ende** — ein grün wirkender Commit
  verbarg einst eine rote Suite (ANECDOTES #17); heute gilt `make ci && …`.

---

# Wie das hier entstand — vier Liefermodelle

Menschlicher Aufwand für den **gleichen M1–M173-Umfang** (Personenmonate; siehe
`docs/PROJECT_TIMELINE.md`):

```
KI-unterstützt (1 Dev + KI)|  ~1    (tatsächlich -- ~5 Wochen, 19 aktive Tage)
Experte solo              |####################  ~21   (~20-26 Monate)
Team von ~6               |######################################  ~40  (~5-6 Monate)
Junior solo               |#########################################################  ~60  (~5-6 J.)
```

Der KI-unterstützte Balken zeigt **menschliche** Aufsichtszeit (Design, Review,
Steuerung) — ohne Modell-Compute. Die Lehre ist nicht die reine Geschwindigkeit,
sondern *wohin die menschliche Zeit fließt*.

---

# Was der Mensch tatsächlich tat (KI-unterstützt)

Der Entwickler schrieb fast kein C. Die knappe Ressource verlagerte sich **nach
oben**:

- **Anleitung + Anforderungen** — das nächste Band auswählen (~30%).
- **Design-Review** — jedes `docs/proposals/*.md` vor dem Code freigeben (~25%).
- **Diffs durchsehen + im Lauf nachsteuern** — falsche Annahmen abfangen (~25%).
- **Reihenfolge + Prioritäten** (~12%) · **CI kontrollieren** (~8%).

> Die KI verdichtete *Implementierung, Tests und Doku*; **Design und Aufsicht
> blieben menschlich — und bestimmen die Qualitätsobergrenze.**

---

# Leitprinzipien für das, was kommt

1. **Korrektheit zuerst, dann Kosten.** Konsequent verifizieren; nur Rotes zurückrollen.
2. **Kleine, kombinierbare Flächen.** Neue Features greifen bestehende Engstellen wieder auf.
3. **Alles offline testbar.** Reine Kerne + dünne I/O-Shells.
4. **Aus den Logs lernen.** Die eigene Telemetrie des Agenten speist die Roadmap.
5. **Neues bekommt ein Proposal.** Nichts zum Abschauen → erst schriftlich entwerfen.

---

<!-- _class: lead -->

# Sie ist unverbindlich

Die Roadmap hält *Designs* fest, keine Versprechen. Jede Fähigkeit bekommt ihren
eigenen Plan, sobald sie angegangen wird.

`docs/ROADMAP.md` · `docs/PROJECT_TIMELINE.md` · `docs/SELF_IMPROVEMENT.md`
