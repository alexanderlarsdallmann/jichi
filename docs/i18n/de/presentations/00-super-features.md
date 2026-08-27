---
marp: true
title: jichi — Highlights
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/00-super-features.md @ 0632b94 -->
<!-- slides-behind: 1 (en has 12 slide separators, this has 11). The English deck
     gained slides this translation does not carry. Translating them is prose in a
     language this repository cannot review, so the gap is DECLARED rather than
     filled -- and the numbers above are checked, so the declaration cannot be left
     behind silently either. M582. -->

<!-- _class: lead -->

# jichi

### Ein KI-Coding-Agent in **~C89** — ein einziges kleines Binary

Die Continue CLI von Grund auf neu gebaut: Chat, agentische Tool-Schleife, RAG,
Autonomie, MCP, LSP, eine TUI — in portablem ANSI C, ganz ohne Runtime.

<!-- Auftakt: Die Kernaussage lautet „ein vollwertiger moderner Coding-Agent, der
in ~1,2 MB passt und überall läuft, wo es POSIX und libcurl gibt." -->

---

# Zehn Dinge, die überraschen

1. Reines **C89** — null Warnungen unter `-std=c89 -pedantic -Wall -Wextra`.
2. Das Binary misst **~1,2 MB**; ein Headless-Durchlauf braucht **~10–17 MB RSS**.
3. Es **bearbeitet deinen Code** — robuste Fuzzy-Multi-Datei-Patches mit Diffs.
4. Es läuft **autonom** mit Leitplanken — mitten im Lauf pausier- und steuerbar.
5. Es beherrscht **RAG** — BM25 + Embeddings + Rerank über Repo *und* Doku.
6. Es spricht **MCP** (Client) *und* **ACP** (Server, für Editoren wie Zed).
7. Es bietet **LSP** — Navigation *und* Refactorings (Rename/Format/Code-Actions).
8. Es erzeugt **Bilder und Sprache**, spielt Audio ab und nimmt es auf.
9. Kein `sudo`, kein **Motor** ungefragt — alles wird protokolliert.
10. Sichert jede Änderung per Checkpoint; läuft auf **Pi oder Handy** (Termux).

---

# Die Agent-Schleife, kurz gefasst

```
history + system + tools
  → provider.build_request()      (Anthropic oder OpenAI, gestreamt)
  → HTTP/SSE                      (libcurl)
  → execute tool calls           (read/edit/run/search/git/…)
  → append results, loop         bis zur finalen Antwort
```

Eine Schleife, zwei Provider, ~35 Built-in-Tools — und keinerlei Verzweigung
nach Provider.

<!-- Das Ganze ist eine einzige Funktion, jc_agent_run_turn; alles Übrige ist
ein Tool oder eine Provider-Vtable. -->

---

# Leitplanken, auf die du dich verlassen kannst

- **Modi:** chat (fragt nach), plan (nur lesen), auto (autonom).
- **Path-Fence:** Datei-Tools kommen nicht aus dem Workspace heraus; Lesezugriffe
  dürfen zusätzlich auf benannte Referenz-Roots reichen, Schreibzugriffe nie.
- **Autonomie-Envelope:** Budgets für Tokens, Laufzeit und Tool-Aufrufe, ein
  Edit-Scope-Fence, ein **Verify-Gate** (deine Tests) und ein JSONL-Audit-Journal
  — auf Wunsch mit automatischem **Zurücknehmen** von Shell-Änderungen außerhalb
  des Edit-Scopes.
- **Änderungen über mehrere Dateien halten ihr Versprechen:** `apply_patch` prüft
  nach dem Alles-oder-nichts-Prinzip; scheitert ein Schreibvorgang mittendrin,
  nimmt es die bereits geschriebenen Dateien zurück und meldet den Zustand jeder
  einzelnen.
- **Snapshots:** Ein Schatten-Git-Repo legt vor dem ersten Edit einen Checkpoint
  an — `/undo` stellt Dateien wieder her, `/rewind` auch die Konversation.

> Budget mitten in der Aufgabe erschöpft? Es verifiziert ein letztes Mal und
> behält brauchbare Arbeit — zurückgerollt wird nur ein *roter* Baum.
> Teilfortschritt geht nicht verloren.

---

# Es skaliert auch *nach oben*

- **Subagenten** — delegieren eine klar umrissene Teilaufgabe (eigene History,
  eigenes Modell, eigener Tool-Fence), standardmäßig zwei Ebenen tief und mit pro
  Ebene sinkendem Budget.
- **Parallel-Agenten** — ein Fork-Pool; jede Aufgabe läuft in einem isolierten
  Git-Worktree und wird dateiweise nach dem First-wins-Prinzip zusammengeführt.
- **Daemon** — ein warmer Prozess hält Config, MCP, LSP und Index vorgeladen und
  bedient Anfragen über einen Socket, mit begrenztem Worker-Pool und einem
  Watchdog pro Anfrage.
- **Schleifen & Flotten** — ein Supervisor arbeitet eine Task-Queue ab
  (tmux/systemd/cron); ein Koordinator verteilt Arbeit per SSH und MCP auf
  Peer-Instanzen.

<!-- Genau das macht den zigodot-Rewrite machbar: ausfächern, isolieren, zusammenführen. -->

---

# Aus der Praxis: der zigodot-Rewrite

jichi treibt einen **großen, autonomen Port von Godot nach Zig** — unsere Nagelprobe.

- Lange `--auto`-Läufe unter dem Envelope, an einer echten Codebasis.
- Telemetrie und Sitzungs-Zeitleisten zeigen, wohin Tokens und Kosten wirklich fließen.
- Die **Lernschleife** speist die eigenen Logs als dauerhafte Lektionen zurück,
  damit sich Fehler nicht wiederholen.
- Jede lehrreiche Anekdote steht in `docs/ANECDOTES.md`.

Die harten Stellen — Kontextüberlauf, Ökonomie ohne Cache, Isolationsfehler —
kamen beim *Benutzen* ans Licht, nicht in der Theorie.

---

# Überall dort, wo du arbeitest

- **Terminal:** eine echte TUI — Markdown mit Syntaxhervorhebung, Live-Diffs,
  Freigabe per Tastendruck, `/cost`, `/context`, `/undo`.
- **Headless:** `jichi -p "…"`; `--output json`/`jsonl` für die Automatisierung.
- **Editoren:** Emacs, Vim/Neovim, nano — und jeder **ACP**-Editor (Zed).
- **Remote:** SSH und tmux für lange autonome Läufe auf einer GPU- oder CI-Box.
- **Deine Sprache:** `"language": "日本語"`, und es antwortet darin — der
  Freigabe-Prompt zieht mit (en/de/es/ja/zh), die Onboarding-Doku gibt es in allen fünf.

Ein Binary, jede Oberfläche, darunter derselbe Vertrag.

---

# Lokal, privat, günstig

- Richte es auf **jeden OpenAI-kompatiblen Endpunkt** — LM Studio, llama.cpp,
  LocalAI, vLLM. Kein Vendor-Lock-in; lokale Server ohne API-Key laufen sofort.
- **Prompt-Caching** (bei beiden Providern) und eine cache-bewusste Kostenrechnung.
- **Medienerzeugung** gegen ein lokales **LocalAI**-Binary — Bildgenerierung auf
  einer Consumer-GPU, verifiziert, ganz ohne Docker.
- Läuft, wo ein Container scheitert: eingebettet, mit wenig RAM, nahezu abgeschottet.

---

<!-- _class: lead -->

# Warum C89?

Weil „portabel, winzig, genügsam bei Abhängigkeiten und in zehn Jahren noch
lauffähig" ein Feature ist — für Embedded-Ziele, für die Lehre und fürs Vertrauen.

**Abhängigkeiten:** libcurl und eine mitgelieferte JSON-Bibliothek. Mehr nicht.

---

<!-- _class: lead -->

# Probier es aus

```sh
make && ./jichi setup            # geführtes Setup
./jichi -p "explain this repo"   # headless
./jichi                          # die TUI
```

Doku: `README.md`, `docs/ROADMAP.md` (thematischer Index), `docs/AGENTS_GUIDE.md`.
