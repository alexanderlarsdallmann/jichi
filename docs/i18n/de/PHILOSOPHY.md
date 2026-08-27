<!-- tracks: ../../PHILOSOPHY.md @ 8ad8468 -->
# Die Philosophie von jichi

Der größte Teil der Dokumentation dieses Repositories sagt, *wie*. Diese Seite
sagt, *warum* — die Prinzipien hinter einer von Grund auf neuen
C89-Reimplementierung eines KI-Coding-Agenten und die Schulden, die sie trägt.
Sie ist ein Essay, kein Nachschlagewerk; nichts hier ist normativ in dem Sinne,
wie [CONTRIBUTING.md](../../../CONTRIBUTING.md) es ist. Aber sollte eine
Designentscheidung je willkürlich erscheinen: Dies ist der Rahmen, in dem sie
getroffen wurde.

## 温故知新（おんこちしん） — das Alte studieren, um das Neue zu erkennen

*Onkochishin* (温故知新（おんこちしん）), aus den Analekten: „Pflege das Alte, und du wirst das
Neue erkennen."

Die naheliegende Frage zu diesem Projekt lautet: Warum C89, in einer Ära von
Rust und TypeScript? Die Antwort ist nicht Nostalgie. C89 ist der am weitesten
implementierte Programmiersprachen-Standard, den es gibt. Ein Programm, das
sich an ihn hält — keine `//`-Kommentare, Deklarationen am Blockanfang, kein
`long long`, Stringliterale unter 509 Zeichen aufgeteilt —, kompiliert auf
Maschinen, von denen das npm-Ökosystem nie gehört hat: einem zehn Jahre alten
Laborrechner, dem gemeinsamen Login-Host einer Universität, einem
Einplatinenrechner in einem Klassenzimmer ohne Budget. Die Beschränkung ist das
Feature. Ein agentisches Werkzeug *zum Lernen* ist wenig wert, wenn die
Maschine des Lernenden es nicht ausführen kann.

Es gibt einen zweiten, leiseren Grund. Das Neueste in der Informatik — ein
großes Sprachmodell, das über Code nachdenkt — wird, wie sich zeigt, von den
ältesten Disziplinen, die wir haben, bestens bedient: begrenzte Puffer,
explizite Ownership, Statuscodes statt Exceptions, kleine Werkzeuge, über
Pipes und Prozesse komponiert. Die Agentenschleife forkt, pipet und
`select`-et, wie UNIX-Programme es 1985 taten. Das Alte zu studieren war der
Weg, auf dem das Neue entstand.

## 職人気質（しょくにんかたぎ） — das Temperament des Handwerkers

*Shokunin kishitsu* (職人気質（しょくにんかたぎ）) benennt eine Haltung: Meisterschaft, verfolgt
als Verpflichtung gegenüber der Arbeit selbst, nicht gegenüber einem Publikum.
Der Shokunin fegt den Boden der Werkstatt, ob ein Kunde kommt oder nicht.

In dieser Codebasis sieht der gefegte Boden so aus: null Warnungen unter
`-std=c89 -pedantic -Wall -Wextra`, immer, mit `WERROR=1` in der CI; mehr als
10.000 Unit-Checks, die offline laufen, sodass eine Contributorin im Zug der
Suite vertrauen kann; Provider-Streaming, getestet durch das Einspeisen
synthetischer SSE-Events statt durch Aufrufe ins Netz; jedes `cJSON_Parse`
gepaart mit einem `cJSON_Delete`; eine Fuzzing-Stufe für die Parser, die
nicht vertrauenswürdigen Bytes ausgesetzt sind. Nichts davon ist in einer Demo
sichtbar. All das ist im dritten Jahr sichtbar.

Handwerk heißt auch Ehrlichkeit darüber, was das Werkzeug getan hat.
Tool-Fehler werden als Werte zurückgegeben, nie als Kontrollfluss. Ein
Subagent, der an seiner Iterationsgrenze angehalten wurde, sagt das —
„[stopped at its iteration limit]" —, statt eine Teilantwort als fertige
auszugeben. Die japanischen und chinesischen Übersetzungen unter `docs/i18n/`
sind als *Entwurf* gekennzeichnet, bis ein Muttersprachler sie geprüft hat,
denn eine selbstgewiss falsche Übersetzung ist schlimmer als eine ehrlich
markierte.

## 改善（かいぜん） und 守破離（しゅはり） — kleine Schritte und die Gestalt des Lernens

*Kaizen* (改善（かいぜん）) ist Verbesserung durch Anhäufung. Dieses Projekt hatte nie
einen Rewrite; es hatte Meilensteine — von M0s Skelett bis zu M134s
Härtungsdurchgang —, jeder klein genug, um ihn zu prüfen, zu testen und
notfalls zurückzunehmen. Die [ROADMAP](../../ROADMAP.md) ist das Laborjournal
dieser Anhäufung, und [ANECDOTES.md](../../ANECDOTES.md) hält die Fehlschläge
fest, an die zu erinnern sich lohnt — denn ein studierter Fehler ist eine
Lektion, ein vergessener eine Generalprobe.

*Shu-ha-ri* (守破離（しゅはり）) beschreibt, wie ein Schüler jedes Handwerks
voranschreitet: **shu** (守), die Form genau bewahren; **ha** (破), die Form
bewusst brechen; **ri** (離), die Form hinter sich lassen. Die Lernfunktionen
sind nach dieser Gestalt gebaut. Das Assignments-Band gibt dem Anfänger die
Form — eine Aufgabenstellung, ein Bewertungsraster, eine Leiter von
Hinweisen —, und die Lernstufen (`learner-junior|student|senior`) lockern das
Gerüst in dem Maße, wie der Schüler es sich verdient. Selbst der Agent ist
hier Schüler: Die Lern-/Mentor-Schleife liest die eigene Telemetrie, entwirft
Lektionen und *korrigiert* — seit M78 — veraltete, statt neuen Rat auf alten
zu häufen. Eine Lehre, die nicht widerrufen kann, ist Dogma; die Schleife war
nicht fertig, bevor sie eine Lektion zurücknehmen konnte.

## 不易流行（ふえきりゅうこう） — das Unwandelbare und das Fließende

Bashō lehrte seine Schüler *fueki-ryūkō* (不易流行（ふえきりゅうこう）): Kunst braucht beides, das
Bleibende und das Flüchtige, und im Grunde sind sie dasselbe Streben.

Die Architektur dieses Projekts ist diese Lektion in der Anwendung. Das
**Unwandelbare**: C89, POSIX, Arenen, der `jc_status`-Vertrag, eine
Provider-Vtable, hinter die der Agent nie schaut. Das **Fließende**: Modelle,
Endpunkte und Protokolle, die jede Saison wechseln. Die Grenze zwischen beiden
ist so gezogen, dass der Fluss den Kern nie erodiert — der Agent verzweigt
nicht nach Provider; ein neues Backend ist eine neue Vtable, kein neuer Agent.
Darum kann dasselbe Binary, das mit einer Frontier-API spricht, auch gegen
einen llama.cpp-Server auf dem Schreibtisch nebenan laufen — und darum wird es
noch bauen, wenn beide längst ersetzt sind.

## 侘寂（わびさび） — die Schönheit ehrlicher Grenzen

*Wabi-sabi* (侘び寂び) findet Wert im Unvollkommenen, Vergänglichen und
Unvollständigen. Die Softwarekultur tut meist so, als wäre es anders; diese
Codebasis versucht, es nicht zu tun. Die Token-Schätzung ist eine
Byte-Heuristik und läuft *bekanntermaßen* optimistisch — also kalibriert M77
sie pro Modell an der Wirklichkeit, statt eine Präzision zu behaupten, die sie
nicht besitzt. Budgets beenden Läufe mitten im Gedanken, also lernte M80, die
Teilarbeit zu behalten, statt sie dafür zu verbrennen, dass sie kein Ganzes
wurde. Jeder Puffer ist begrenzt, jede Obergrenze ist benannt, und wo Ausgabe
gekürzt wird, sagt die Kürzung es. Einem Werkzeug, das seine Grenzen
eingesteht, kann man an ihnen vertrauen; einem Werkzeug, das sie verbirgt,
nirgends.

## Riesen — und Haufen von Zwergen

Wir stehen auf den Schultern von Riesen — und, ebenso wahr, auf Haufen von
Zwergen: den zahllosen kleinen Beiträgen, an denen kein berühmter Name hängt.
Beide Schulden sind real, und dieses Projekt erweist beiden seinen Respekt.

Die Riesen sind leicht zu nennen: C und das Komitee, das es 1989 zu etwas
einfror, auf dem die ganze Welt bauen konnte; UNIX und POSIX; libcurl, das
Bytes für mehr Menschen bewegt hat, als irgendeiner von uns je treffen wird;
das API-Design des mitgelieferten cJSON; git, dessen Plumbing im Stillen die
Snapshot- und Worktree-Maschinerie antreibt; das Language Server Protocol, das
Model Context Protocol und das Agent Client Protocol, jedes ein Akt der
Großzügigkeit, der einen privaten Vorteil gegen einen öffentlichen Standard
tauschte; [Continue](https://continue.dev), dessen CLI dieses Projekt
reimplementiert und dessen Design bewies, dass die Form es wert ist, gebaut zu
werden; und die Modellanbieter, deren APIs der Schleife ein Gegenüber geben.

Die Zwerge sind schwerer zu nennen — und genau das ist der Punkt: die Autoren
der Manpages, wer auf Stack Overflow die eine obskure `tcsetattr`-Frage
beantwortet hat, die Person, die vor Jahren in irgendeinem anderen Projekt den
Dezimalkomma-Locale-Bug meldete, sodass dieses hier wusste, `LC_NUMERIC`
abzusichern, die Reviewer von RFCs, die Übersetzer, die Tester von
Release-Kandidaten auf seltsamen Maschinen. Keiner von ihnen allein ist ein
Riese. Zusammen sind sie der Boden. Ein Projekt, das nur Riesen würdigt, hat
missverstanden, woher der Boden kommt — und ein Projekt, das etwas bedeuten
will, sollte vor allem danach streben, sich dem Haufen anzuschließen: jemand
anderes unauffälliges, verlässliches Fundament zu werden.

## Berge — und das Meer

井の中の蛙大海を知らず、されど空の深さを知る — „Der Frosch im Brunnen weiß
nichts vom großen Ozean; doch er kennt die Tiefe des Himmels."

Das Sprichwort schneidet nach beiden Seiten, und beide Schneiden zählen hier.
Tiefe ohne Breite ist der Brunnen: Dieses Projekt besteigt seinen Berg — ein
Sprachstandard, eine Plattformfamilie, gründlich gemeistert. Breite ohne Tiefe
ist der Ozean des Touristen. Die Disziplin besteht darin, beides mit Absicht
zu tun: Dogfooding-Läufe gegen fremde Codebasen (die zigodot-Sessions, aus
denen ein Dutzend Meilensteine hervorging), Lokalisierung, die
englischzentrierte Annahmen ans Licht zwingt (M127s Wide-Character-Arbeit
existiert, weil japanischer Text die Vorstellung des Zeileneditors davon, was
eine Spalte ist, zerbrach), Barrierefreiheitsarbeit, die fragt, wie sich die
Oberfläche anfühlt, wenn man sie nicht sehen kann.

Wir müssen erkunden, um unseren Blickwinkel zu ändern. Jeder wirklich gute
Meilenstein der Roadmap geht auf einen Wechsel des Standpunkts zurück — das
Werkzeug benutzen, statt es zu schreiben; die Telemetrie lesen, statt dem
Entwurf zu vertrauen; es einem Lernenden in die Hand geben statt einem
Experten. Der Berg lehrt Handwerk; das Meer lehrt Demut; der Brunnen, ehrlich
gepflegt, lehrt den Himmel. Ein Werkzeug zum Lernen sollte von Menschen gebaut
werden, die selbst noch lernen wollen — und sollte, wo es das vermag, selbst
eines von ihnen sein.

---

*Begleitseiten: [JOURNEY.md](JOURNEY.md) (der Weg vom ersten Schritt zur
Meisterschaft), [ANECDOTES.md](../../ANECDOTES.md) (bezahlte Lektionen),
[ROADMAP.md](../../ROADMAP.md) (das Kaizen-Journal),
[LEARNING.md](../../LEARNING.md) (die Mentor-Schleife),
[TEACHING_ASSIGNMENTS.md](../../TEACHING_ASSIGNMENTS.md) (Pädagogik),
[i18n/README.md](../README.md) (Lokalisierungsrichtlinie),
[ACCESSIBILITY.md](../../ACCESSIBILITY.md).*
