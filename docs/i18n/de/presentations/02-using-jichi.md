---
marp: true
title: jichi benutzen
theme: default
paginate: true
---
<!-- tracks: ../../../presentations/02-using-jichi.md @ 0632b94 -->

<!-- _class: lead -->

# jichi benutzen

### Vom ersten Start bis zur autonomen Aufgabe

---

# Einrichtung mit einem Befehl

```sh
jichi setup                      # interaktiver Assistent
# oder die globale Config in einem Projekt weiterverwenden:
jichi setup --from-global --preset developer
# oder ein fremdes Repo übernehmen (nur Vorschläge: Analyse + Tutorial-Entwurf):
jichi setup --onboard
jichi --config local/config.json doctor   # alles prüfen
```

`doctor` prüft libcurl, Config, Modelle, Keys, Erreichbarkeit, git, MCP, LSP und
deine Projekt-Assets.

---

# Drei Arten, es zu starten

| Oberfläche | Befehl | Wofür |
|---|---|---|
| **TUI** | `jichi` | Interaktive Arbeit, Review, Freigabe per Tastendruck. |
| **Headless** | `jichi -p "…"` | Skripte, CI, Automatisierung, SSH. |
| **Strukturiert** | `jichi -p "…" --output jsonl` | Ein anderes Programm oder ein Agent steuert es. |

Ist der Prompt `-`, wird stdin gelesen — so kann eine ganze Datei der Prompt
sein, ganz ohne `ARG_MAX`-Grenze.

---

# Modi: Wie viel Leine?

- **`/chat`** — normal; fragt vor verändernden Aktionen nach.
- **`/plan`** — nur lesen; untersuchen und vorschlagen, keine Edits.
- **`/auto`** — autonom; führt seine Sandbox-Tools ohne Nachfrage aus.

```sh
jichi --plan -p "how would you add feature X?"    # ein Plan
jichi --auto -p "add feature X and make tests pass"  # umsetzen
```

<!-- Der Plan-Modus ist die sichere Voreinstellung, um ein fremdes oder geteiltes Repo zu erkunden. -->

---

# Die Tools an Bord

- **Dateien:** `read_file`, `write_file`, `edit_file`, `apply_patch` (atomarer
  Multi-Edit), `list_files`, `search_code`.
- **Ausführen:** `run_terminal_command` (+ Hintergrund), `run_tests`.
- **Wissen:** `codebase_search`, `search_docs`, `fetch_url`, `web_search`.
- **Git:** status/diff/log/blame + add/commit/branch/stash.
- **Navigation/Refactoring:** LSP `find_definition`/`references`/`symbols`, rename, format.
- **Delegieren:** `spawn_subagent`, `spawn_parallel`.
- **Medien & Sound:** `generate_image`, `generate_audio`, `transcribe_audio`,
  `play_audio`, `record_audio`.

---

# Kontext, den du in einen Turn einbringst

`@`-Referenzen in einer einfachen Nachricht holen Kontext herein:

```
review @src/parser.c against @diff and the @rss:https://…/releases.xml feed
explain @sym:jc_agent_run_turn and @folder:src/net
```

`@file @diff @url @rss @sym @docs @problems @folder @mcp @audio @img` — jede wird
zu einem begrenzten Block aufgelöst und an deine Nachricht angehängt.

---

# Wenn eine Sitzung lang wird

- **Auto-Kompaktierung** fasst den älteren Verlauf zusammen, damit alles ins
  Fenster passt; **Mid-Turn-Kompaktierung** kürzt umfangreiche Tool-Ausgaben in
  einem einzelnen ausufernden Turn.
- **Token-Kalibrierung** lernt die echten Bytes pro Token jedes Modells, damit
  die Schätzungen nicht länger zu optimistisch ausfallen.
- `/context` zeigt die aktuelle Budget-Aufschlüsselung, `/cost` die laufenden Kosten.

Meistens denkst du gar nicht daran — genau das ist der Sinn.

---

# Autonome Läufe, abgesichert

```sh
jichi --auto \
  --verify "make test" \
  --budget-tokens 500k --deadline 30m \
  --edit-scope "src/**" \
  --journal run.jsonl --control \
  -p "fix the failing ring-buffer tests"
```

Bestanden → weiter; Fehlschlag → N-mal nachbessern (fix-forward), sonst zurück
zum letzten grünen Stand. Alles steht im Journal.

Live steuern: `jichi control <sock> status | inject "…" | pause | abort`.
Nachlesen: `jichi runs` und `jichi audit` (beide `--output json`).

---

# In deinem Editor

- **Emacs** (`jichi.el`), **Vim/Neovim** (`jichi.vim`), **nano** (`jichi-nano`) — alle
  über den Headless-Vertrag.
- **Zed / jeder ACP-Editor** — `jichi serve` (granulare Freigabe, Streaming).

```vim
:JichiAsk how does the fence work?    " Antwort in einem Scratch-Split
:'<,'>JichiRegion tidy this           " eine Auswahl direkt umwandeln
:JichiTask add a test for edge case Y " agentisch, fragt vorher nach
```

---

<!-- _class: lead -->

# Praktische Befehle

`/model` `/mode` `/diff` `/undo` `/rewind` `/compact` `/context` `/cost`
`/skills` `/mcp` `/export` `/fork` `/sessions`

`export` schreibt ein Transkript für einen PR oder einen Kurs; `fork` probiert
einen Zweig aus, ohne dass du die Stelle verlierst.
