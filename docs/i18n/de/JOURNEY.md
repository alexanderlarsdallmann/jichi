<!-- tracks: ../../JOURNEY.md @ 8ad8468 -->
# Die Reise — vom ersten Schritt bis zur Ruhe des Meisters

Dies ist kein Tutorial; die hat jichi
([TUTORIAL_BEGINNER.md](../../TUTORIAL_BEGINNER.md),
[TUTORIAL_ADVANCED.md](../../TUTORIAL_ADVANCED.md)). Dies ist die **Karte des
ganzen Weges**: die vorhandenen Features und Dokumente, geordnet in die
Stufen, in denen ein Handwerk tatsächlich erlernt wird — 守破離（しゅはり）, *shu-ha-ri*:
die Form bewahren, die Form brechen, die Form hinter sich lassen. Jede Stufe
benennt ihr Ziel, ihre Übungen (echte Befehle, echte Dateien), die Tugend, die
sie schult, und die Zeichen, an denen du erkennst, dass du bereit bist
weiterzugehen.

Erbeten wurde diese Karte in einem
[Dialog](../../dialogues/2026-07-14-the-one-feature.md), dessen Schluss sie
voraussetzt: **kein Feature trägt einen Menschen zur Meisterschaft** —
Software hält nur Spiegel hoch und senkt die Kosten des Übens. Der eine
unentbehrliche Begleiter ist *eine ehrliche Aufzeichnung der eigenen Fehler
und dessen, was jeder gelehrt hat*. Beginne diese Aufzeichnung am ersten Tag;
die Stufen unten geben ihr nur etwas, worüber sie schreiben kann.

---

## 仕度（したく） — Vorbereitung (vor dem ersten Schritt)

**Ziel:** eine funktionierende, geprüfte Werkbank — und die Demut, sie zu
prüfen.

- Installiere ([INSTALL.md](../../INSTALL.md)) und lass den geführten
  `setup`-Assistenten laufen ([SETUP_WIZARD.md](../../SETUP_WIZARD.md)); kein
  API-Schlüssel, kein Internet nötig — ein Modell auf der eigenen Maschine
  ist ein vollwertiger Lehrer ([LOCAL_MODELS.md](../../LOCAL_MODELS.md)).
- Führe **`doctor`** aus und lies jede Zeile ([DOCTOR.md](../../DOCTOR.md)).
  Das ist die erste Gewohnheit der Reise: *das System fragen, was nicht
  stimmt, statt anzunehmen, man wisse es.* Du wirst es für den Rest deines
  Lebens ausführen.
- Setze `language`, wenn Englisch nicht die Sprache deines Denkens ist
  ([LANGUAGE.md](../../LANGUAGE.md)) — Verstehen geht vor Konvention.

**Geschulte Tugend:** Demut in ihrer kleinsten Form — die eigene Einrichtung
zu prüfen, statt ihr zu vertrauen.

---

## 守（しゅ） Shu — die Form bewahren

**Ziel:** den Formen vertrauen; in den eigenen Händen lernen, dass Fehler
überlebbar sind.

- Arbeite [TUTORIAL_BEGINNER.md](../../TUTORIAL_BEGINNER.md) durch. Bleib im
  **Chat-Modus**; nutze den **Plan-Modus**, wenn du unsicher bist
  ([AGENT_MODES.md](../../AGENT_MODES.md)) — vor dem Handeln zu fragen ist
  eine Form, keine Schwäche.
- Mach deine erste Änderung. **Lies die Diff-Vorschau, bevor du `y` drückst**
  ([EDITING.md](../../EDITING.md), [TUI_RENDER.md](../../TUI_RENDER.md)). Der
  Bestätigungsprompt ist das Werkzeug, das dich lehrt, bewusst zuzustimmen;
  lass ihn nie zum Reflex werden.
- **Mach absichtlich etwas kaputt — dann `/undo`.** Spul eine ganze
  Unterhaltung mit `/rewind` zurück ([SNAPSHOTS.md](../../SNAPSHOTS.md),
  [REWIND.md](../../REWIND.md)). Tu das früh und oft, bis dein Körper lernt,
  was dein Geist später brauchen wird: *jeder Fehler lässt sich
  zurückgehen.* Furchtlosigkeit folgt aus Umkehrbarkeit, und Vergebung —
  zuerst dir selbst gegenüber — folgt aus Furchtlosigkeit.
- Lass die Tests die Ersten sein, die dir die Wahrheit sagen: `run_tests`,
  das `test`-Subkommando ([TESTING.md](../../TESTING.md)). Ein Check besteht
  oder er besteht nicht, so gewiss, wie die Sonne aufgeht. Lerne diese
  Einfachheit zu lieben, bevor du den Mysterien begegnest.
- Wenn du eine Lehrerin oder einen Lehrer hast, lerne im Assignments-Ablauf
  ([TEACHING_ASSIGNMENTS.md](../../TEACHING_ASSIGNMENTS.md),
  [ASSIGNMENTS.md](../../ASSIGNMENTS.md)): arbeite die Aufgabenstellung ab,
  erklimm die **Hinweisleiter** ehrlich (ein erbetener Hinweis ist Wissen;
  eine erspähte Lösung ist eine Schuld), nimm das Feedback des
  Bewertungsrasters hin, ohne zu zucken.
- **Beginne deine Aufzeichnung.** Eine Datei, wo immer du sie behalten
  wirst: jeder Eintrag *Symptom → Sackgassen → Grundursache → Lektion*,
  genau wie in [ANECDOTES.md](../../ANECDOTES.md). Die Sackgassen gehören in
  den Eintrag — der Weg zu einer Antwort ist Teil der Antwort.

**Geschulte Tugend:** die Kraft zu vergeben — an dir selbst geübt,
mechanisch, bis sie Charakter ist.

**Bereit weiterzugehen, wenn:** du vorhersagst, was der Agent tun wird, bevor
er es tut; du Diffs ohne Anstrengung liest; `/undo` ein Werkzeug ist, das du
respektierst, aber nicht mehr täglich brauchst.

---

## 破（は） Ha — die Form brechen

**Ziel:** das Werkzeug zu deinem eigenen machen; die Voreinstellungen
hinterfragen, denen du gehorcht hast.

- Arbeite [TUTORIAL_ADVANCED.md](../../TUTORIAL_ADVANCED.md) und
  [WORKFLOWS.md](../../WORKFLOWS.md) durch. Dann bau die Werkbank um: eigene
  Slash-Befehle ([COMMANDS.md](../../COMMANDS.md)), Skills
  ([SKILLS.md](../../SKILLS.md)), Subagenten-Profile
  ([SUBAGENTS.md](../../SUBAGENTS.md)), Output-Styles
  ([OUTPUT_STYLES.md](../../OUTPUT_STYLES.md)), User-Tools
  ([USER_TOOLS.md](../../USER_TOOLS.md)), Hooks ([HOOKS.md](../../HOOKS.md)).
  Ein `.jichi/`-Verzeichnis, das aussieht wie das aller anderen, heißt: du
  bist noch im *shu*.
- Gib dem Agenten dein Wissen: `remember`-Notizen
  ([MEMORY.md](../../MEMORY.md)), ein Glossar
  ([GLOSSARY.md](../../GLOSSARY.md)), Projektregeln
  ([RULES.md](../../RULES.md)). Das Werkzeug zu lehren ist die Generalprobe
  dafür, Menschen zu lehren.
- Leiste **echte Arbeit** damit — Dogfooding. Lass `--auto` innerhalb der
  Autonomie-Hülle laufen ([AUTONOMY.md](../../AUTONOMY.md)): Budgets, ein
  Verify-Gate, ein Edit-Scope. Lerne, dass die Leitplanken keine
  Einschränkung sind, sondern *Fürsorge* — dieselbe Fürsorge, die du übst,
  wenn du Tests schreibst.
- Lies deine eigenen Fußspuren: die `telemetry`-Zusammenfassungen
  ([TELEMETRY.md](../../TELEMETRY.md)), `learn analyze`
  ([LEARNING.md](../../LEARNING.md)). Wo machst *du* Arbeit doppelt? Welchen
  deiner Gewohnheiten widersprechen die Daten? Hier wohnen die
  geheimnisvollen Wahrheiten — das grüne Gate, das null Tests ausführte, die
  Kosten, die vom erneuten Lesen kamen — und sie geben geduldiger
  Untersuchung nach, nicht der Selbstsicherheit.
- Lies [ANECDOTES.md](../../ANECDOTES.md) — ganz. Es ist das Projekt, das
  sich öffentlich selbst vergibt, Eintrag für Eintrag. Dann streite mit
  etwas: einer Voreinstellung, einem Bewertungsraster, einer Konvention in
  [CONTRIBUTING.md](../../../CONTRIBUTING.md). Die Form zu brechen heißt,
  den Bruch *verteidigen* zu können.

**Geschulte Tugend:** Liebe zum Wissen — jene Art, die es überlebt zu
erfahren, dass man falsch lag.

**Bereit weiterzugehen, wenn:** du einer Voreinstellung widersprichst und
begründen kannst, warum; deine Aufzeichnung Einträge hat, in denen die
Grundursache *du selbst* warst; dein `.jichi/` unverkennbar deines ist.

---

## 離（り） Ri — die Form hinter sich lassen

**Ziel:** das Werkzeug verschwindet; was bleibt, ist Urteilskraft — und du
lehrst.

- Wechsle den Platz im Assignments-Ablauf: verfasse Aufgabenstellungen,
  Bewertungsraster und Hinweisleitern für jemand anderen (`/assign`,
  `/solve`, `/check` —
  [TEACHING_ASSIGNMENTS.md](../../TEACHING_ASSIGNMENTS.md)). Eine gute
  Hinweisleiter zu schreiben wird dich schneller demütigen als jeder Bug: du
  musst dich erinnern, wie sich Nichtwissen anfühlte.
- Baue Scaffold-Packs für dein Team ([SCAFFOLDING.md](../../SCAFFOLDING.md));
  trage upstream bei ([CONTRIBUTING.md](../../../CONTRIBUTING.md)); übersetze
  eine Seite für den nächsten Lernenden in deiner Sprache
  ([i18n/README.md](../README.md)).
- Lass die volle Lernschleife auf dich selbst laufen: `/learn`, den Entwurf
  redigieren und — das Schwerste von allem — **Korrekturen** schreiben (M78,
  [LEARNING.md](../../LEARNING.md)): eigene frühere Lektionen zurücknehmen,
  wenn der Code über sie hinausgewachsen ist. Eine Lehre, die du nicht
  zurücknehmen kannst, ist Dogma. Um Vergebung zu bitten und sie anzunehmen
  heißt auf dieser Stufe: korrigieren, was du einst mit Gewissheit gelehrt
  hast.
- Bring jemandem sein erstes `/undo` bei. Sieh zu, wie die Schultern sinken,
  wenn er lernt, dass der Fehler überlebbar ist. Dieser Moment ist die ganze
  Reise, weitergereicht.

**Geschulte Tugend:** Demut in ihrer letzten Form — die Bereitschaft, dass
andere über dich hinauswachsen.

**Du bist angekommen, wenn:** deine Schüler deine Formen brechen — und es
dich freut.

---

## Die Ruhe des Meisters

Der Meister ist nicht, wer nicht mehr irrt. Der Meister ist, wer **mit dem
Irren im Frieden ist**: wer ohne Scham zurücksetzt, ohne Ausrede aufzeichnet,
ohne Anhaften korrigiert — und die Wahrheit in ihren beiden Gesichtern ehrt,
dem einfachen (ein Test besteht oder er besteht nicht, so schlicht wie Sonne
und Mond) und dem geheimnisvollen (der Defekt, der sich tagelang in einem
System deiner eigenen Hand verbirgt, so tief wie der Himmel mit seinen
Sternen, die Erde, das Meer und alles). Friede ist nicht die Abwesenheit von
Versagen; er ist die geschlossene Schleife — jeder Fehler untersucht, jede
Lektion niedergeschrieben, jede veraltete Lehre zurückgenommen, nichts, das
noch spukt.

Die Aufzeichnung, die du am ersten Tag begonnen hast, ist jetzt lang. Lies
sie einmal im Jahr. Das ist die Ruhe.

---

*Begleitseiten: [PHILOSOPHY.md](PHILOSOPHY.md) (warum das Projekt so
gebaut ist), [ANECDOTES.md](../../ANECDOTES.md) (die eigene Aufzeichnung des
Projekts), [LEARNING.md](../../LEARNING.md) (die Schleife, mechanisiert),
[TEACHING_ASSIGNMENTS.md](../../TEACHING_ASSIGNMENTS.md) (der Platz des
Lehrers),
[dialogues/2026-07-14-the-one-feature.md](../../dialogues/2026-07-14-the-one-feature.md)
(woher diese Karte stammt).*
