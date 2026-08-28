# brick-car-gamepad-bridge

Ein LEGO® Technic™ Move Hub 88019 mit einem Xbox-Series-X|S-Controller fahren —
der ESP32-C3 sitzt als Bluetooth-Brücke dazwischen.

*[English version](README.md)*

---

Der Move Hub aus Set 42176 (Porsche GT4 e-Performance) lässt sich ab Werk nur
mit der LEGO® Control+-App vom Handy steuern. Zum Vorführen taugt das, zum
Fahren kaum: kein Gefühl für Gas, keine proportionale Lenkung, Touch-Flächen
statt Trigger.

Diese Firmware macht aus einem ESP32-C3 eine **BLE-Funkbrücke**. Sie hält
gleichzeitig zwei Bluetooth-LE-Verbindungen als Client — eine zum Controller,
eine zum Hub — und übersetzt die Eingaben ins LEGO Wireless Protocol.

**Am Fahrzeug wird nichts umgebaut.** Akku, Motoren und Hub bleiben unberührt,
die Control+-App funktioniert danach weiterhin. Der ESP32-C3 muss auch nicht
ins Auto: Beide Strecken sind Funk, die Brücke liegt also am besten zwischen
Fahrer und Fahrzeug.

### Warum das hier steht

Ich habe das für mein eigenes Auto gebaut, und meine Kinder fahren damit. Auf
meinem Aufbau läuft es, jeden Tag.

Ich veröffentliche es, weil der Weg dorthin einiges an Herumstochern in einem
undokumentierten Protokoll gekostet hat — und das muss niemand wiederholen.
Wer ein Modell mit diesem Hub besitzt und es lieber mit einem richtigen
Controller fahren würde als über einen Touchscreen, findet hier alles: die
Firmware und die Protokollnotizen, die am längsten gedauert haben.

Darum geht es bei der ganzen Sache. Nimm mit, was nützt, und lass den Rest
liegen.

## Was man braucht

| | |
|---|---|
| Ein ESP32-**C3** | Keine beliebige Wahl: Der Xbox-Controller braucht BLE 5.0. Auf dem klassischen ESP32 reißt die Verbindung mit HCI-Paketzählfehlern ab. |
| Xbox Series X\|S Controller | Modell 1914, mit Share-Taste und USB-C. Ein Xbox One S (1708) geht meist auch; der ältere Xbox One (1537/1697) hat gar kein Bluetooth. |
| LEGO Set 42176 | Oder ein anderes Modell mit dem Technic Move Hub 88019 (42214, 42239) — siehe [Anderes Modell anschließen](#anderes-modell-anschließen). |
| Ein USB-Kabel | Zum Flashen und als Stromversorgung. |

Toolchain, einmalig:

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install "XboxSeriesXControllerESP32_asukiaaa@1.1.2"
```

> **Die Versionen müssen zusammenpassen:** Bibliothek ≥ 1.1.0 verlangt
> NimBLE ≥ 2.1.0. Nach dem Installieren mit `arduino-cli lib list` prüfen, dass
> NimBLE nicht auf 1.4.x heruntergezogen wurde. Aus demselben Grund wird die
> verbreitete `Legoino`-Bibliothek hier **nicht** verwendet — sie hängt an
> NimBLE 1.x. Der Hub-Client in diesem Projekt ist stattdessen direkt gegen
> NimBLE 2.x geschrieben.

## Loslegen

```bash
# Die Board-Optionen sind entscheidend. Ohne CDCOnBoot=cdc bleibt der C3 auf
# der seriellen Schnittstelle völlig stumm — er läuft, man sieht nur nichts.
FQBN="esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app"
PORT=COM6           # unter Linux und macOS z. B. /dev/ttyACM0

# 1. Rauchtest: Hält der C3 beide Verbindungen gleichzeitig? Nichts bewegt sich.
arduino-cli compile --fqbn "$FQBN" smoketest
arduino-cli upload -p $PORT --fqbn "$FQBN" smoketest

# 2. Die eigentliche Firmware
arduino-cli compile --fqbn "$FQBN" firmware
arduino-cli upload -p $PORT --fqbn "$FQBN" firmware

# Serieller Monitor, 115200 Baud
arduino-cli monitor -p $PORT -c baudrate=115200
```

Ist die Platine kein „ESP32C3 Dev Module", weicht das FQBN ab;
`arduino-cli board listall` zeigt die Alternativen.

### Stufenweise Inbetriebnahme

Jede Stufe muss stehen, bevor die nächste beginnt. **Bis Stufe 5 durch ist,
bleibt das Auto aufgebockt, Räder frei in der Luft.**

1. **Rauchtest.** `smoketest` flashen, Controller einschalten (Pair-Taste 3 s
   halten, bis das Xbox-Logo schnell blinkt), Auto einschalten. Erwartet wird
   `xbox:OK hub:OK`, `failed=0` und — der wichtigste Wert — ein `drop detected
   after` deutlich unter einer Sekunde. Es bewegt sich dabei nichts.
2. **Firmware flashen.** Der Hub kalibriert beim Verbinden seine Lenkung: Sie
   fährt sichtbar gegen beide Anschläge und zentriert.
3. **Lenkung, noch entschärft.** Die Lenkung folgt bereits dem Stick, während
   der Antrieb gesperrt bleibt. Fährt sie mechanisch an den Anschlag, mit
   `s80` begrenzen.
4. **Antrieb, aufgebockt.** `m20`, dann **START**. Die Brücke schaltet nur bei
   losgelassenen Triggern scharf. Gas, Bremse und Rückwärtsgang prüfen.
5. **Failsafe im Trockenen.** Bei drehenden Rädern **B** drücken — sie müssen
   sofort stehen. Dann den **Controller ausschalten**, während du den Stick
   bewegst: Im Feld `drop:` der Statuszeile steht danach, wie lange die
   Erkennung gedauert hat.
6. **Erste Fahrt.** Auto absetzen, `m30`, im Freien. Grenze schrittweise
   hochziehen.

## Bedienung

| Eingabe | Wirkung |
|---|---|
| **RT** | Gas |
| **LT** | Bremsen, dann rückwärts — wie bei einem RC-Regler |
| **Linker Stick** | Lenkung, proportional mit Expo-Kurve |
| **START** (☰) | Scharfschalten (nur bei losgelassenen Triggern) |
| **B** / **Xbox** | Not-Aus — zieht die **Feststellbremse** an |
| **A** | Licht an / aus |
| **X** | Warnblinker |
| **Y** (halten) | Boost — volle Werkstattgrenze statt Tempostufe |
| **LB / RB** | Tempostufe 25 / 50 / 75 / 100 % |
| **Rechter Stick bewegen** | Lichtkranz — das Licht wandert ums Auto |
| **Rechter Stick drücken** | Lichthupe |
| **Steuerkreuz links/rechts** | Blinker, mit selbsttätiger Rückstellung |
| **VIEW** (⧉) | Akkustand des Hubs als Füllstand im Lichtkranz |

Nach dem Einschalten ist die Brücke **immer entschärft**. Das Auto fährt nie
von selbst los.

Die seriellen Befehle zum Nachjustieren ohne Neuflashen listet `?` im Monitor
auf; `m`, `s` und `t` liegen im Flash und überleben ein Firmware-Update.

## Was man wissen sollte

**Feststellbremse.** Entschärft heißt *festgehalten*, nicht ausgerollt — beim
Not-Aus über B ebenso wie beim Verlust des Controllers. Das macht einen
Funkabriss deutlich gutmütiger: Das Auto bremst, statt weiterzurollen.

**Den Hub fahren lassen.** Der kombinierte Fahr-Frame gibt dem Hub eine
*Geschwindigkeit* vor, seine VM regelt darauf und fährt dabei sanft an. Drei
Funktionen, die diese Arbeit noch einmal gemacht haben, wurden gebaut und
wieder entfernt:

- **Motor-Direktbefehle** („Power-Modus", das, was die Power-Taste der
  Control+-App tut). Die VM besitzt dieselben Ports und regelt weiter, also
  kämpfen beide gegeneinander: Die Motoren stottern hörbar, und auf der
  25-%-Stufe ist das Auto damit sogar *langsamer* als ohne. Bei 100 % ist
  kaum ein Unterschied zu merken.
- **Umkehr der Motor-Drehrichtung.** Die brauchten nur die Direktbefehle —
  der Fahr-Frame regelt die Drehrichtung intern.
- **Eine eigene Beschleunigungsrampe** zusätzlich zu der der VM. Zwei Rampen
  hintereinander ergaben einen sichtbar zweistufigen Antritt und Nachlauf
  beim Loslassen.

Geblieben ist eine einzige Rampe auf den *Sollwert*, einstellbar mit `t<ms>`.
Sie hat genau einen Zweck: Bittet man den Geschwindigkeitsregler der VM aus
dem Stand um 25 von 100, ist die Regelabweichung groß und er antwortet mit
Leistung. Ein ansteigender Sollwert hält den Antritt weich. `t0` schaltet sie
ab.

**Ladeanzeige (VIEW).** Der Akkustand des Hubs als Zeiger, der einmal um den
Lichtkranz läuft — Start an der linken Flanke, wachsend gegen den
Uhrzeigersinn. Halbe Ladung erhellt also die Hälfte von der linken Flanke über
das Heck bis rechts, ein Viertel das linke Viertel des Autos. Während des
Ladens bleibt die Anzeige stehen, und ein Licht wandert durch den dunklen Teil
des Rings.

Dass geladen wird, wird *erschlossen*, nicht gemeldet: LWP3 kennt kein
Lade-Flag, aber ein Ladestand, der steigt, kann nur eines heißen. Dafür braucht
es einen ganzen Prozentpunkt, deshalb liegt der Zustand im Flash — sonst wäre
die Anzeige nach jedem Neustart minutenlang leer, während das Auto am Kabel
steht.

**Lichtkranz.** Der Hub hat sechs Lichtleiter-Ausgänge, ringförmig angeordnet.
Der rechte Stick wählt eine Richtung in diesem Ring, das Licht wandert mit —
weich überblendet, sodass es gleitet statt zu springen. Das Lichtprogramm des
42176 nutzt nur vier der sechs, womit zwei für die Blinker frei bleiben.

> **Die Blinker brauchen noch Bastelarbeit.** Die Ausgänge 1 und 4 leuchten
> zwar, aber der 42176 führt keine Lichtleiter dorthin — am Modell ist davon
> also nichts zu sehen. Damit die Blinker sichtbar werden, müssen flexible
> Lichtleiter von diesen beiden Ausgängen zu den gewünschten Leuchten geführt
> werden, was einen kleinen Umbau bedeutet. Die Firmware-Seite steht, die
> Klemmbaustein-Seite noch nicht.

**Die Crash-Erkennung wurde gebaut und wieder entfernt — nicht neu bauen.**
Der Hub hat einen Beschleunigungssensor auf Port `0x38`, das Abonnement
funktioniert, und Stöße messen sich sauber (normales Fahren erreicht 255–530
milli-g, echte Einschläge 2361 und 2655). Es bringt den Hub aber um. Siehe
Fund 5 weiter unten.

## Was dieses Projekt über das Protokoll herausgefunden hat

LEGO veröffentlicht das [LEGO Wireless Protocol](https://lego.github.io/lego-ble-wireless-protocol-docs/)
selbst, und die Community hat darauf aufbauend den Move Hub analysiert. Beim
Bau dieser Brücke kamen **fünf Punkte zutage, die in der vorhandenen
Dokumentation falsch stehen oder fehlen**. Alle wurden am Gerät gemessen:

**1. Bit 0 im letzten Byte des Fahr-Frames ist die BREMSE, kein Lichtmodus.**
Die verbreitete Tabelle beschreibt vier „Lichtmodi" (`0x00/0x01/0x04/0x05`).
Tatsächlich zieht Bit 0 die Bremse an; dass dabei das Bremslicht angeht, ist
die sichtbare Folge. Behandelt man es als Lichtmodus, fährt das Fahrzeug gar
nicht mehr, und der Rückwärtsgang scheitert stillschweigend, weil die Bremse
gegen die negative Geschwindigkeit gewinnt.

**2. Nach dem Pairing wirbt der Hub mit der Adresse `00:00:00:00:00:00`.**
Name und RSSI stimmen, andere Geräte im selben Scan liefern gültige Adressen —
verbinden lässt sich damit trotzdem nicht. Die im BLE-Bond gespeicherte Adresse
ist gültig, und genau darauf verbindet diese Firmware. Der Scan dient nur noch
der Erstkopplung.

**3. Der Xbox-Controller meldet nur bei Änderungen.** Ein ruhender, verbundener
Controller lieferte **0 Meldungen pro Sekunde**. Jede Failsafe, die auf einem
Melde-Timeout beruht, ist damit gefährlich falsch: Vollgas gegen den Anschlag
ist ein konstanter Wert und erzeugt nichts, das Auto würde mitten in der Fahrt
abschalten. Belastbar ist allein die BLE-Verbindung selbst.

**4. Die sechs LEDs *sind* einzeln ansteuerbar** — über Port `0x35`, Modus
`6LEDS`, mit genau zwei Nutzbytes: einer Bitmaske und einer Helligkeit. Zwei
Fallen dabei: Die Maske wählt nur aus, *was gesetzt wird*, und schaltet nichts
ab; und der Hub setzt die LEDs von sich aus nie zurück.

**5. Das Abonnieren des Beschleunigungssensors bringt den Hub um.** Port
`0x38`, Modus `GRV`, ist dokumentiert, das Abonnement wird angenommen und die
Werte sind brauchbar. Nach einigen Minuten Fahrt bleibt der Hub jedoch tot
stehen: Status-LED aus, Fahrzeug-LEDs weiter an, BLE-Verbindung weg mit HCI
`0x13` („remote user terminated") — und nur ein Aus- und Einschalten holt ihn
zurück. Von außen sieht das exakt wie ein Funkproblem aus, was die Suche teuer
macht.

Belegt durch einen kontrollierten Versuch, nicht durch Überlegung: Dieselbe
Fahrt läuft mit abgemeldetem Sensor durch und scheitert mit angemeldetem. Es
ist **keine** Frage der Last — der Sensor erzeugte 299 Meldungen in 625
Sekunden, also weniger als eine pro Sekunde. Es ist dieselbe Art Falle wie
`GOPOS` auf dem Lenkmotor, von dem bereits bekannt ist, dass er den Hub
abstürzen lässt: ein dokumentierter LWP3-Weg, den der Technic Move Hub im
laufenden Betrieb nicht überlebt.

Die vollständige Byte-Referenz samt Portliste und Messwerten steht in
**[docs/protokoll.de.md](docs/protokoll.de.md)**.

### Eine Zahl, die man zitieren kann

Weil die Failsafe an der BLE-Verbindung hängt, entscheidet das Supervision
Timeout, wie lange das Fahrzeug nach einem Ausfall des Controllers führerlos
weiterfährt:

| | Abriss erkannt nach |
|---|---|
| Standardwerte des Controllers | **2610 ms** und **3180 ms** |
| Nach `updateConnParams(12, 24, 0, 100)` | **963 ms** |

## Anderes Modell anschließen

Der Move Hub steckt auch in 42214 und 42239. Zum Fahren ist nichts
modellspezifisch einzustellen: Der kombinierte Fahr-Frame kümmert sich um die
Motoren, auch darum, wie herum sie eingebaut sind.

Anpassen muss man womöglich die **Lichtleiter**. Welcher Ausgang welche Leuchte
speist, hängt vom Aufbau ab — beim 42176 ist das vordere Paar gekreuzt. `f` und
`h` im seriellen Monitor vertauschen vorderes und hinteres Paar, die Einstellung
liegt im Flash, ohne neu zu kompilieren.

Ein Blick auf die Werkstattgrenzen `m` (Tempo) und `s` (Lenkwinkel) lohnt
ebenfalls: ±100 ist der *Protokoll*bereich, nicht zwangsläufig das, was ein
bestimmtes Modell mechanisch mag.

## Wenn etwas klemmt

**Der Monitor bleibt stumm.** Entweder passt das FQBN nicht zur Platine, oder
darin fehlt `CDCOnBoot=cdc` — ohne diese Option sagt der C3 gar nichts.
`arduino-cli board listall` listet die Alternativen.

**Das Auto fährt eine Weile, ist dann tot und nicht mehr auffindbar.**
Im Monitor steht es im Klartext: `disconnected: the hub hung up on us (531)` —
HCI `0x13`, der Hub beendet die Verbindung selbst. Dafür gibt es zwei Ursachen,
und vom Fahrersitz aus sehen sie gleich aus:

*Etwas anderes hat sich den Hub geschnappt.* Er nimmt **genau eine Verbindung**
an — öffnet man die Control+-App am Handy, übernimmt sie den Hub und wirft die
Brücke hinaus. App schließen, dann findet die Brücke den Hub von allein wieder.

*Der Hub ist abgestürzt.* Status-LED dunkel, während die Fahrzeug-LEDs weiter
leuchten, und er bleibt unerreichbar, bis man ihn aus- und einschaltet. Das ist
ein Hub, der steht — nicht einer, der belegt ist. Der bekannte Auslöser ist ein
abonnierter Sensor-Port, siehe Fund 5. Wer einen hinzugefügt hat, schaut dort
zuerst.

Die Suche danach hat einen Abend und vier falsche Theorien gekostet:
überlasteter Funkkanal, leerer Akku, der LED-Verkehr und der Lenkwinkel wurden
alle überzeugend begründet — und alle von den Zählern widerlegt. Entschieden
hat es am Ende ein einziger kontrollierter Versuch: dieselbe Fahrt mit
abgemeldetem und mit angemeldetem Sensor. Die Lehre daraus steckt als
dauerhaftes Frame-Protokoll in der Firmware (`w` im Monitor): Es schreibt die
letzten Frames vor einem Abbruch in den Flash, weil der Moment des Ausfalls nie
der Moment ist, in dem jemand die Konsole liest.

**Der Hub wird nicht gefunden.** Meist schläft er einfach: Ohne Verbindung
schaltet er sich nach wenigen Minuten ab, was nach jedem Flashen passiert. Kurz
den Knopf am Hub drücken. Außerdem die Control+-App schließen, aus dem Grund
oben. Zur Diagnose `HUB_SCAN_VERBOSE` in
[firmware/Config.h](firmware/Config.h) auf 1 setzen: Dann wird jedes sichtbare
BLE-Gerät aufgelistet, und man sieht, ob der Hub überhaupt sendet.

**`address 00:00:00:00:00:00 - unusable`.** Kein Fehler, sondern die Eigenheit
aus Befund 2. Die Firmware verbindet stattdessen über den gespeicherten Bond;
`b` zeigt ihn an, `x` löscht ihn und koppelt neu.

**Das Pairing mit dem Hub scheitert.** Zuerst `x` — ein veralteter Bond mit
falschem Schlüssel lässt das Pairing wortkarg scheitern. Hilft das nicht,
`HUB_FORCE_SECURE_CONNECTIONS` in Config.h auf 1 setzen und neu flashen.

**Der Controller verbindet sich nicht.** Er muss im Kopplungsmodus sein
(Xbox-Logo blinkt *schnell*). Vorher an Handy oder PC gekoppelte Controller
verbinden sich gern dorthin zurück — dort die Kopplung entfernen.

## Sicherheit

Drei Eigenschaften sind Absicht und sollten in jedem Fork erhalten bleiben:

- **Startzustand ist entschärft.** Das Auto fährt beim Einschalten nie los.
- **Scharfschalten wird verweigert, solange die Trigger gedrückt sind.**
- **Die Failsafe umgeht die Beschleunigungsrampe** und zieht die
  Feststellbremse an. Ein verlorener Controller darf nicht über eine halbe
  Sekunde ausschleichen.

Die Absicherung ist gestaffelt, und die Stufen unterscheiden sich stark im
Tempo:

| Was ausfällt | Reaktion | Nach |
|---|---|---|
| Controller aus oder außer Reichweite | Feststellbremse | **~1 s**, gemessen 963 ms |
| Hub-Verbindung verloren | Fahrzeug bekommt keine Befehle mehr | sofort |
| Die Brücke selbst fällt aus (Absturz, Strom weg) | Hub stoppt von allein | 10 s |

Die erste Zeile ist der Normalfall und die, auf die es ankommt. Die letzte ist
eine Rückfallebene, die nur greift, wenn der ESP32 gar nichts mehr sendet: Der
Fahr-Frame des Hubs verfällt nach zehn Sekunden ohne Wiederholung, sodass auch
eine tote Brücke das Auto nicht endlos weiterfahren lassen kann.

## Mit einem KI-Assistenten entstanden — bitte lesen

Ich bin Maschinenbauingenieur, kein Firmware-Entwickler. **Dieser Code ist mit
Hilfe eines KI-Assistenten entstanden (Claude von Anthropic), und er kann
durchaus Fehler enthalten, die mir entgangen sind.** Versteh ihn als
funktionierenden Ausgangspunkt eines Hobbybastlers, nicht als fertige
Software.

Wofür ich geradestehen kann:

- Die Firmware **läuft auf meinem eigenen Aufbau**, jeden Tag. Meine Kinder
  fahren damit.
- Die Entwurfsentscheidungen und **sämtliche Tests am echten Gerät stammen von
  mir.**
- **Jede Protokollaussage in diesem Projekt wurde am Auto gemessen**, nicht
  geglaubt. Wo etwas nicht messbar war, steht das da, statt geraten zu werden.

Wofür ich nicht geradestehen kann: dass es sich an deinem Hub, deinem
Controller oder deiner Platine genauso verhält. Geh die stufenweise
Inbetriebnahme oben durch und lass das Auto aufgebockt, bis die Failsafe sich
bewiesen hat. Wenn dir etwas Falsches auffällt, ist ein Issue willkommen —
genau dafür liegt das hier.

## Dank

Dieses Projekt steht auf der Arbeit anderer:

- [LEGO Wireless Protocol 3.0.00](https://lego.github.io/lego-ble-wireless-protocol-docs/) — die offizielle Spezifikation, von LEGO veröffentlicht
- [DanieleBenedettelli/TechnicMoveHub](https://github.com/DanieleBenedettelli/TechnicMoveHub) — Reverse Engineering des Hubs 88019; Fahr-Frame und Kalibriersequenz stammen von dort
- [pybricks Discussion #1733](https://github.com/orgs/pybricks/discussions/1733) — Hub-Typ `0x84`, Portliste, der `GOPOS`-Absturz
- [toorisrael/LEGO-Porsche-Controller](https://github.com/toorisrael/LEGO-Porsche-Controller) — das Format der LED-Maske
- [move-hub.site](https://move-hub.site/guide) — Fahrmodi und der 10-Sekunden-Timeout
- [asukiaaa/arduino-XboxSeriesXControllerESP32](https://github.com/asukiaaa/arduino-XboxSeriesXControllerESP32) — der Xbox-BLE-Client
- [h2zero/NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) — der BLE-Stack

## Marken

LEGO®, Technic™ und Control+ sind Marken der LEGO Gruppe, die dieses Projekt
weder sponsert noch autorisiert noch unterstützt. Xbox® ist eine Marke der
Microsoft Corporation, Porsche® eine Marke der Dr. Ing. h.c. F. Porsche AG —
beide sponsern, autorisieren oder unterstützen es ebenso wenig. „Porsche GT4
e-Performance" ist Teil des offiziellen Namens von LEGO Set 42176. Alle
genannten Namen bezeichnen hier ausschließlich die Produkte, mit denen diese
Brücke zusammenarbeitet.

## Lizenz

[MIT](LICENSE). Die Quellen und Bibliotheken, auf denen diese Firmware
aufbaut, stehen mit ihren Lizenzhinweisen in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
