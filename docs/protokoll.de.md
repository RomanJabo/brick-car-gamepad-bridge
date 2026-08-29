# Protokoll-Nachschlagewerk

*[English version](protocol.md)*

Alles, was die Firmware über die beiden Funkstrecken wissen muss. Quellen am Ende.

---

## LEGO Technic Move Hub 88019

Verbaut in Set 42176 (Porsche GT4 e-Performance), außerdem in 42214 und 42239.
Antriebsmotor und Lenkservo sind fest im Hub verbaut.

| | |
|---|---|
| Advertising-Name | `Technic Move` (Präfix, danach folgt oft ein Leerzeichen) |
| Hersteller-ID | 919 = `0x0397` (LEGO) |
| Herstellerdaten | `97 03 <btn> 84 02 FF 01 00` → **Byte 3 = Hub-Typ `0x84`** |
| Service-UUID | `00001623-1212-EFDE-1623-785FEABCD123` |
| Characteristic | `00001624-1212-EFDE-1623-785FEABCD123` (Write + Notify) |
| Sicherheit | **Pairing zwingend**, Security Mode 1 Level 2 |

### Die sechs LEDs lassen sich einzeln ansteuern

**Die sechs LED-Ausgänge sind Lichtleiter-Buchsen** — zwei vorn an jeder Seite,
zwei hinten. Im 42176 führen Glasfaserstäbe von dort zu Scheinwerfern und
Rückleuchten; zwei Buchsen bleiben im Porsche ungenutzt.

> Die LEDs hinter den beiden ungenutzten Buchsen leuchten durchaus — nur ist
> davon ohne eingesteckten Lichtleiter am Modell nichts zu sehen. Wer sie
> nutzen will, muss eigene Lichtleiter verlegen, also ein Stück umbauen.

Modus 0 von Port `0x35` heißt laut Hub selbst `6LEDS` und erwartet **genau zwei
Datensätze**:

```
09 00 81 35 11 51 00 <maske> <helligkeit>
                      │       └─ 0..100
                      └───────── Bit 0 = LED 1 ... Bit 5 = LED 6
```

| gewünscht | Maske |
|---|---|
| nur LED 1 | `0x01` |
| nur LED 4 | `0x08` |
| LED 1 + 4 | `0x09` |
| alle sechs | `0x3F` |

Serieller Befehl: `E3F,100` für alle, `E09,100` für LED 1 und 4, `Ex` beendet
die Direktansteuerung.

**Am Gerät bestätigt (42176):**

- Die Bitnummer entspricht **direkt** der Beschriftung am Hub: Bit 0 = LED 1
  … Bit 5 = LED 6. Alle sechs leuchten einzeln ansteuerbar.
- Das **normale Lichtprogramm des Porsche nutzt nur 2, 3, 5 und 6.**
  **LED 1 und 4 bleiben frei** — dort lassen sich eigene Lichtleiter einstecken
  und beliebig schalten, ohne das Fahrlicht zu stören.

> **Die Maske schaltet nichts ab — sie wählt nur aus, was gesetzt wird.**
> Nicht adressierte LEDs behalten ihren Zustand. `E3F,100` gefolgt von
> `E01,100` lässt deshalb alle sechs an, statt auf LED 1 zu reduzieren.
>
> Für eine exklusive Anzeige braucht es **zwei Frames**: erst
> `(~maske & 0x3F)` mit Helligkeit 0, dann die Maske mit der gewünschten
> Helligkeit. Genau das macht `firmware.ino`, solange die Direktansteuerung
> aktiv ist.
>
> Ebenso setzt der Hub die LEDs **von sich aus nie zurück**. Nach dem Ende der
> Direktansteuerung bleibt der letzte Zustand stehen, bis die VM ihr
> Lichtprogramm neu schreibt.

**Die Direktvorgabe muss laufend wiederholt werden.** Der Fahr-Frame geht mit
20 Hz an die VM, und die setzt die LED-Reihe nach ihrem eigenen Programm. Ein
einmaliger Befehl wird überschrieben. Die Firmware sendet die LED-Frames
deshalb im Takt mit (jeden zweiten Tick, damit neben dem Fahr-Frame Funkluft
bleibt).

> **Wird ein Schreibzugriff mit `Generic Error 0x06` abgelehnt, zuerst die
> Nutzlänge prüfen.** `0x06` heißt „unzulässige Verwendung, Parameter falsch"
> und ist genau das, was der Hub antwortet, wenn die Zahl der Nutzbytes nicht
> zur Datensatzzahl des Modus passt. Hier sechs Helligkeiten zu senden — eine
> je LED — wirkt naheliegend und scheitert eben daran. `n35` zeigt die
> erwartete Zahl an.

Unabhängig davon gilt weiter: Im **Fahr-Frame** steuert Bit 2 nur alles
gemeinsam an oder aus, und Bit 1 (`0x02`) hat dort keine sichtbare Wirkung
(alle acht Werte durchgefahren). Wer einzelne LEDs will, nimmt den Weg über
Port `0x35` oben.

Quellen für das Maskenformat:
[toorisrael/LEGO-Porsche-Controller](https://github.com/toorisrael/LEGO-Porsche-Controller)
(Befehl `getledmask`), bestätigt durch die eigene Modusabfrage.

**Eine über das LED-Array gesetzte Vorgabe setzt sich gegen das Lichtprogramm
durch**, solange sie wiederholt wird. Am 2026-08-29 mit der Lichthupe belegt:
Sie blinkt LED 2 und 3 mit 200 ms Halbperiode über diesen Weg, während die VM
dieselben Lampen im 20-Hz-Takt neu malt — sauber, ohne Flackern. 200 ms liegt
deutlich über `LED_REFRESH_MS` (150 ms); ob ein schnelleres Blinken noch
durchkommt, wurde nicht geprüft.

### Vollständige Portliste dieses Hubs

Vom Hub selbst gemeldet (Hub Attached I/O) plus Port Information Request:

| Port | Gerätetyp | Was |
|---|---|---|
| `0x14` | — | antwortet auf Anfragen (5 Modi, Ein-/Ausgang), meldet aber **nichts angeschlossen** |
| `0x32`, `0x33` | 86 | Antriebsmotoren |
| `0x34` | 87 | Lenkmotor |
| `0x35` | 88 | LED-Reihe, 6 Elemente |
| `0x36` | 89 | VM / Fahr-Frame |
| `0x37`–`0x3B` | 60, 57, 58, 59, 93 | Temperatur, Beschleunigung, Gyro, Neigung |
| `0x3C` | 20 | Spannung |
| `0x3D`–`0x40` | 92, 94, 23, 95 | u. a. RGB-Statuslicht (23) |

`0x14` ist der einzige Port mit Motor-Profil, an dem nichts hängt — ein
möglicher externer Anschluss. Zum Prüfen etwas anstecken und `o` erneut
aufrufen: Taucht der Port dann unter „belegt" auf, ist er nutzbar.

> Die Abfrage blockiert die Regelschleife rund 1,6 s (64 Ports mit Wartezeit).
> Sie ist ein Werkzeug für die Werkbank, nicht für die Fahrt — bei einem Test
> riss dabei die Hub-Verbindung ab und wurde selbsttätig wiederhergestellt.

> **Finger weg von `GOPOS` auf Port `0x34`.** Der dokumentierte LWP3-Weg, den
> Lenkmotor einzeln auf eine Absolutposition zu fahren, **bringt den Hub zum
> Absturz**. Deshalb geht die Lenkung ausschließlich über den kombinierten
> Frame auf Port `0x36`; den Servo-Regelkreis erledigt der Hub dann selbst.
> Genau so macht es auch die Control+-App.

### Fahr-Frame (13 Byte)

```
0D 00 81 36 11 51 00 03 00 <speed> <angle> <lights> 00
 │  │  │  │  │  │  │  └──┴─ Sub-Kommando „Fahren"
 │  │  │  │  │  │  └─────── Mode 0
 │  │  │  │  │  └────────── WriteDirectModeData (0x51)
 │  │  │  │  └───────────── Startup/Completion Flags
 │  │  │  └──────────────── Port 0x36
 │  │  └─────────────────── Port Output Command (0x81)
 │  └────────────────────── Hub-ID
 └───────────────────────── Länge
```

- `speed`, `angle`: **int8, jeweils −100 … +100**, als `wert & 0xFF` geschrieben
- **Der Frame verfällt nach 10 s** ohne Wiederholung. Die Firmware sendet
  deshalb mit 20 Hz — das ist zugleich die unterste Sicherheitsebene: Stürzt
  die Brücke ab, hält das Auto spätestens nach 10 Sekunden an.

### Das letzte Byte — Bremse und Licht, nicht nur Licht

> **Korrektur gegenüber der Reverse-Engineering-Quelle.** Dort ist dieses Byte
> als reines Lichtbyte mit vier Modi beschrieben:
> `0x00` Front+Heck an · `0x01` Front an, Heck beim Bremsen · `0x04` alles aus ·
> `0x05` Front aus, Heck beim Bremsen.
>
> Am Gerät zeigt sich: **Bit 0 ist die Bremse.** Dass dabei das Bremslicht
> angeht, ist die sichtbare Folge — und genau das hat die Quelle beschrieben.

| Bit | Bedeutung |
|---|---|
| `0x01` | **Bremse** |
| `0x04` | Licht aus (`0` = Licht an) |

Zwei Konsequenzen, die beide beobachtet wurden:

1. Setzt man Bit 0 dauerhaft als vermeintlichen „Lichtmodus", **fährt das
   Fahrzeug nicht mehr** — die Bremse liegt permanent an. Genau das passierte
   in den „Modi" `0x01` und `0x05`.
2. **Rückwärtsfahren scheitert**, solange Bit 0 gesetzt ist: Die Bremse gewinnt
   gegen den negativen Geschwindigkeitswert. Der Motor zuckt beim Loslassen nur
   kurz.

Deshalb kennt die Firmware nur noch zwei Lichtzustände (`0x00` und `0x04`) und
setzt Bit 0 ausschließlich, wenn wirklich gebremst werden soll.

**Bremsen und Rückwärtsfahren schließen sich aus.** Die Firmware löst das wie
ein RC-Regler: Solange der Sollwert positiv ist, bremst LT (Bit 0, Geschwindigkeit
0); ist er auf 0 gelaufen, wird LT zur Rückwärtsfahrt (negative Geschwindigkeit,
Bit 0 gelöscht). Siehe `mapTriggers()` in `Mapping.cpp`.

### Lenkungs-Kalibrierung

Einmalig nach dem Verbinden, 100 ms Abstand:

```
0D 00 81 36 11 51 00 03 00 00 00 10 00
0D 00 81 36 11 51 00 03 00 00 00 08 00
```

Derselbe Frame mit `speed=0`, `angle=0` und den Flag-Bits `0x10` bzw. `0x08` im
Lights-Byte. Der Hub fährt danach die Lenkung gegen beide Anschläge und setzt
die Mitte — das dauert rund zwei Sekunden.

> **Während des Sweeps keine Frames senden.** Ein dazwischenfunkender
> Fahr-Frame bricht die Kalibrierung ab. Die Firmware pausiert deshalb
> `CALIBRATION_SETTLE_MS` lang komplett; der 10-s-Timeout des Hubs ist in
> dieser Zeit weit entfernt.

### Zwei Wege zum Antrieb — und warum es beide braucht

Der kombinierte Fahr-Frame auf Port `0x36` gibt eine **Geschwindigkeit** vor.
Der Hub regelt darauf und begrenzt dabei die Leistung, weshalb am Rad nie das
volle Drehmoment ankommt.

Daneben gibt es den Direktbefehl an einen einzelnen Motor:

```
08 00 81 <port> 00 51 00 <power>
                    │  └─ Mode 0 = POWER (1 = SPEED, 2 = POS)
                    └──── WriteDirectModeData
```

| Wert | Bedeutung |
|---|---|
| `-100 … 100` | Leistung, Vorzeichen = Richtung |
| `126` | Position halten (`END_STATE_HOLD`) |
| `127` | aktiv bremsen (`END_STATE_BRAKE`) |

> **Vorsicht:** `127` ist kein Vollgas, sondern die Bremse. Wer den
> Leistungsbereich versehentlich bis 127 aufzieht, bremst bei Vollausschlag.

**Diese Firmware benutzt den Weg nicht mehr — der Grund ist festhaltenswert.**
Er steckte als „Power-Modus" auf den Antriebsmotoren `0x32`/`0x33` drin, mit
einem auf Lenkung, Licht und Bremse reduzierten Fahr-Frame (Geschwindigkeit 0).
Er dreht die Motoren tatsächlich höher als der geregelte Frame und entspricht
der Power-Taste der Control+-App — aber die VM besitzt diese Ports weiterhin und
regelt sie auf die Geschwindigkeit, die der Fahr-Frame vorgibt. Beide kämpfen
dann im Takt der Frames gegeneinander:

- Die Motoren **stottern hörbar**, mit kurzen Aussetzern.
- Auf der 25-%-Stufe ist das Fahrzeug damit **langsamer** als ohne, weil der
  Direktbefehl schwach genug ist, dass die VM gewinnt.
- Bei 100 % ist der Unterschied kaum zu merken.

Gibt man der VM stattdessen die *echte* Geschwindigkeit, hört das Stottern auf
und ein schlimmeres Problem beginnt: Die VM regelt dann richtig, mit ihrer
eigenen sanften Rampe, und die Räder folgen dieser Rampe statt dem
Direktbefehl — das Fahrzeug reagiert etwa eine Sekunde verzögert.

**Wer den Weg dennoch nutzt, muss die Drehrichtung selbst besorgen.** Der
Fahr-Frame erledigt das intern, der Direktbefehl nicht.

Die Lenkung muss in jedem Fall im Fahr-Frame bleiben — Einzelansteuerung des
Lenkmotors stürzt den Hub ab.

Die beiden Motoren sitzen sich gegenüber und brauchen deshalb **entgegengesetzte
Vorzeichen** — es muss also *genau einer* umgekehrt sein. Zwei Umkehrungen heben
sich im Verhältnis wieder auf und ergeben dasselbe wie keine. *Welcher* der
beiden umgekehrt wird, bestimmt anschließend die Fahrtrichtung.

Am 42176 ermittelt:

| Einstellung | Ergebnis |
|---|---|
| keiner umgekehrt | gegenläufig, das Fahrzeug mahlt nur |
| beide umgekehrt | gegenläufig — identisch zu „keiner" |
| nur B umgekehrt | gleichsinnig, fährt aber verkehrt herum |
| **nur A umgekehrt** | **richtig** |

Bei einem anderen Modell (42214, 42239) kann das abweichen. Beim Fahren über
den Fahr-Frame spielt nichts davon eine Rolle, weshalb diese Firmware die
Einstellung gar nicht mehr mitführt.

Beim ersten Umschalten in den Direktmodus wurde einmal ein
`Generic Error 0x06 (unzulässige Verwendung)` auf Kommando `0x81` gemeldet,
danach nicht wieder — vermutlich eine Übergabe zwischen VM und Direktzugriff.
Die Motoren liefen anschließend normal.

### Was der Hub über sich selbst sagt

Abgefragt mit dem seriellen Befehl `n` (Port Mode Information):

| Port | Modus 0 heißt | Datensätze |
|---|---|---|
| `0x35` | `6LEDS` | 2 |
| `0x36` | `PLAYVM` | 8 |

Die **zwei Datensätze** von `6LEDS` sind Maske und Helligkeit — nicht zwei
Lichtgruppen, wie hier zunächst stand. Genau diese Fehldeutung führte zu dem
oben korrigierten Irrtum, die LEDs seien nicht ansteuerbar.

Zusätzlich abgefragt: `0x38` heißt `GRV`, `0x39` heißt `ROT`, `0x3A` heißt
`POS` — jeweils drei int16-Werte.

### An den Ruck beim Anfahren kommt man über den Sollwert nicht heran

Gemessen am 2026-08-29. Auf einer niedrigen Leistungsstufe fährt das Auto
ruckartig an, und der Sollwert ist dabei nicht der Hebel:

- **Den Sollwert zu glätten beseitigt den Ruck nicht, es tauscht ihn gegen
  Totzeit.** Eine Rampe auf den Sollwert (bei 500 und bei 2000 ms probiert)
  macht den Antritt tatsächlich weicher — sie legt aber eine Verzögerung
  zwischen Trigger und Auto. Man drückt, und einen Moment lang passiert
  nichts; und solange das Auto noch nicht rollt, ändert auch Lenken nichts,
  wodurch das ganze Fahrzeug träge wirkt. Beim Fahren unbrauchbar und
  schlimmer als der Ruck, den sie heilt. Die Rampe wurde deshalb entfernt und
  sollte nicht zurückkommen.
- **Der Ruck hängt nicht von der Höhe des Sollwerts ab.** Ganz ohne Rampe
  erzeugt ihn schon die kleinste Triggerbewegung, die aus der Totzone
  herauskommt. Der Geschwindigkeitsregler der VM antwortet auf jede Abweichung
  aus dem Stand mit Leistung, wie klein die Anforderung auch war.

Zusammen schließen diese beiden den gesamten Sollwert-Weg aus — eine
Expo-Kennlinie auf dem Gastrigger eingeschlossen, denn die ändert nur, welchen
Sollwert eine gegebene Triggerstellung anfordert.

**Port `0x36` hat genau einen Modus.** `requestModeInfo` fragt die Modi 0 bis 7
ab; geantwortet hat allein Modus 0 (`PLAYVM`). Es gibt keinen anderen Modus,
auf den man ausweichen könnte — damit sind die zwei ungenutzten Nutzbytes
weiter unten die einzige verbliebene Spur an diesem Port.

#### Was tatsächlich passiert — beobachtet am 2026-08-30

Es ist kein Anfahr-Ruck, es ist ein **Überschwinger**. Vom Fahrersitz aus: Das
Auto beschleunigt ruckartig, *rollt dann völlig antriebslos weiter*, bis es sich
auf die angeforderte Geschwindigkeit verlangsamt hat. Der Geschwindigkeitsregler
der VM beantwortet die große Abweichung aus dem Stand mit reichlich Leistung,
schießt über das Ziel hinaus, nimmt den Antrieb ganz weg und lässt das Fahrzeug
darauf zurückrollen.

Drei Dinge wurden dabei mitgeklärt:

- **Es hängt von der Richtung ab.** Rückwärts deutlich, vorwärts nur leicht.
- **Es hängt nicht vom Akku ab.** Bei 81 % identisch wie bei 24 %.
- **Eine Sollwert-Rampe beseitigt es nicht.** 250 ms, nur bergauf, und
  nachweislich während der Fahrt aktiv — der Rampenwert wurde genau dafür in die
  Statuszeile aufgenommen und auf allen 90 Zeilen der Aufzeichnung
  zurückgelesen. Der Überschwinger blieb unverändert, und das träge Lenkgefühl,
  an dem schon eine 2000-ms-Rampe gescheitert war, trat auch bei 250 ms auf.
  Warum es bei einer Viertelsekunde auftritt, ist ungeklärt.

**Die Control+-App zeigt denselben Überschwinger rückwärts** und ist vorwärts
merklich weicher; ein plötzliches Vollgas ergibt dort einen allmählichen Zug
ohne Stoß. Der Hub *kann* also weich anfahren — nur reicht nichts von dem, was
hier gefunden wurde, an dieses Verhalten heran.

### Offener Faden: PLAYVM erwartet acht Datensätze

`PLAYVM` deklariert **8 Datensätze**, unser Fahr-Frame füllt aber nur sechs:

```
0D 00 81 36 11 51 00 | 03 00 <speed> <angle> <lights> 00
                       └─ 6 Nutzbytes, deklariert sind 8
```

Die beiden fehlenden Parameter nutzt auch der Referenzcode aus dem
Reverse-Engineering-Repo nicht — ihre Bedeutung ist unbekannt.

**Wie man es untersucht:** Einen 15-Byte-Frame (`0x0F`) mit acht Nutzbytes
senden und die beiden zusätzlichen durchprobieren. Fahrzeug dabei aufbocken.
Achtung: Im Lights-Byte lösen die Bits `0x08` und `0x10` die
Lenkungskalibrierung aus — in den unbekannten Bytes könnten ähnliche
Nebenwirkungen stecken.

Das ist zuerst aufgebockt und danach auf dem Boden wiederholt worden — bei
freien Rädern drehen sie so schnell hoch, dass eine veränderte Beschleunigung
dort kaum zu erkennen wäre. Beide Ergebnisse stehen unten. **Kurzfassung: kein
Effekt.**

#### Probiert am 2026-08-29 und 2026-08-30 — kein Effekt gefunden, ohne Gewähr

> **Das sind Eindrücke, keine Messungen.** Harte Zahlen sind unten allein die
> Frame-Zähler. Der ganze Abschnitt ist als *„kein Ergebnis"* zu lesen, nicht
> als *„ausgeschlossen"* — und am Ende steht eine ungeklärte Beobachtung, die
> jedes subjektive Urteil darin zusätzlich schwächt.

**Der Hub nimmt den Frame in voller Länge an.** Ein 15-Byte-Frame (`0x0F`) mit
beiden Zusatzbytes erzeugte keinen `Generic Error 0x06` und über mehrere
tausend Frames keinen einzigen Fehlschlag. Acht Datensätze zu senden ist
offenbar kein Formatverstoß — der Hub hat sich über sechs allerdings ebenso
wenig beschwert.

**Keine Nebenwirkung im Stand.** Byte 7 wurde über alle acht Einzelbits
(`01` … `80`) und `FF` durchgefahren, Byte 8 über `01`, `08`, `10` und `FF`,
beide zusammen auf `FF FF`. Fahrzeug dabei durchgehend aufgebockt und
entschärft: kein Fehler, kein Verbindungsabriss, keine Lenkungskalibrierung,
nichts an den LEDs.

**Kein klarer Einfluss auf den Hochlauf bei freien Rädern.** Aufgebockt und mit
vollem Trigger fühlte sich `FF` im ersten Byte allenfalls marginal anders an
als der unveränderte Frame. Setzte eines der Bytes eine Beschleunigungszeit,
müssten unbelastete Räder der leichteste Fall sein, das zu sehen — was gegen
diese Deutung spricht, sie aber nicht widerlegt.

**Am 2026-08-30 unter Last geprüft — kein Effekt.** Auf dem Boden wiederholt,
mit vollem Akku, auf der 25-%- und der 50-%-Stufe, `FF` in Byte 7 und `FF` in
Byte 8, Anfahren aus dem Stand mit vollem Trigger in beide Richtungen. Das
Urteil war jedes Mal „alles genauso". Zusammen mit den Durchläufen oben sind
damit sämtliche Einzelbits von Byte 7 und beide Bytes am Anschlag abgedeckt, im
Stand wie unter Last.

**Schluss: Diese zwei Bytes steuern die Beschleunigung nicht.** Der Hub nimmt
sie an und tut nichts Erkennbares damit. Wofür sie da sind, bleibt unbekannt —
wer aber dem Anfahrverhalten nachgeht, kann hier aufhören.

**Eine Beobachtung derselben Sitzung ist ungeklärt.** Nach den Durchläufen
wurde das Fahrzeug als träge gemeldet, *während die Sonde ausgeschaltet war* —
also auf dem normalen 13-Byte-Frame. Im selben Zeitraum war der Akku des Hubs
von 54 % auf 22 % gefallen. Ob das die Ursache war, wurde nie geklärt. Wer das
wiederholt, sollte mit vollem Akku beginnen und den Stand im Auge behalten.

### Beschleunigungssensor — funktioniert und bringt den Hub um

> **Diesen Port am Technic Move Hub nicht abonnieren.** Alles Folgende stimmt
> und ist reproduzierbar, und genau das ist das Problem: Das Abonnement wird
> angenommen, die Werte sind gut, Stöße messen sich sauber — und nach einigen
> Minuten Fahrt bleibt der Hub tot stehen. Status-LED aus, Fahrzeug-LEDs weiter
> an, Verbindung weg mit HCI `0x13`, und nur Aus- und Einschalten hilft.
>
> Belegt durch einen kontrollierten Versuch, nicht durch Überlegung: Dieselbe
> Fahrt läuft mit abgemeldetem Sensor durch und scheitert mit angemeldetem,
> wiederholt. Es ist **keine** Frage der Last — 299 Meldungen in 625 Sekunden,
> weniger als eine pro Sekunde. Dieselbe Art Falle wie `GOPOS` auf dem
> Lenkmotor.
>
> Die Messwerte bleiben hier stehen, weil sie mühsam erarbeitet sind und weil
> jeder, der das noch einmal angeht, davon ausgehen sollte statt bei null
> anzufangen. Die darauf aufgebaute Aufprallerkennung wurde aus der Firmware
> entfernt.

Port `0x38`, Modus `GRV`, drei int16-Werte in Milli-g. Abonniert wird er mit
einem Port Input Format Setup:

```
0A 00 41 38 00 <delta, 4 Byte little endian> 01
```

Achsen, am 42176 durch Anschieben in jede Richtung ausgemessen:

| Schub | dx | dy | Schluss |
|---|---|---|---|
| vorwärts | **+675** | +121 | **x = Längsachse, positiv = vorwärts** |
| rückwärts | **−377** | +44 | bestätigt |
| nach links | −2 | **−465** | **y = Querachse, negativ = links** |
| nach rechts | −108 | **+823** | bestätigt |

Die jeweils andere Achse bleibt nahe null. `z` trägt die Erdbeschleunigung,
im Stand rund −1020, also 1 g ≈ 1000 Zähler.

Die Firmware führt eine langsam nachgeführte Ruhelage mit und nimmt die
Abweichung davon als Stoßstärke. Über eine echte Fahrt mit abgeschalteter
Erkennung gemessen:

| | milli-g |
|---|---|
| im Stand, Rauschen | unter 70 |
| normales Fahren | 255 – 530 |
| Ausreißer im Fahrbetrieb | 713, 999 |
| **echte Einschläge** | **2361, 2655** |

Eine Schwelle gehört in die Lücke zwischen beide Gruppen; 1200 hat sich
bewährt.

Zwei Fallen, die dabei auffielen — falls jemand das auf einem Hub neu baut, der
das Abonnement verträgt:

> **Die Lagenachführung nicht oberhalb der halben Schwelle einfrieren** — der
> naheliegende Weg, damit ein Aufprall nicht „weggelernt" wird. Es entsteht eine
> Sperre: Steht das Fahrzeug danach anders da als vorher, bleibt die Abweichung
> dauerhaft groß, und das nächste Scharfschalten löst sofort wieder einen Crash
> aus. Langsam nachführen, aber *immer* (Zeitkonstante ~1,5 s); ein Aufprall
> dauert etwa 100 ms und verschiebt sie kaum.

> **Jedes Sample auswerten, nicht nur das neueste.** Der Hub meldet
> ereignisgesteuert, nicht getaktet — ein Aufprall kommt deshalb als Schwall
> mehrerer Meldungen innerhalb eines einzigen 20-Hz-Takts an. Wer nur den
> letzten Wert liest, sieht zufällig irgendeinen davon, meist die abklingende
> Flanke. Ein echter Aufprall landete so mit 827 milli-g im Log und blieb unter
> der Schwelle.

### Weitere benutzte Kommandos

| Zweck | Bytes |
|---|---|
| RGB-Statuslicht setzen | `08 00 81 3F 11 51 00 <colorId>` |
| Akkustand abonnieren | `05 00 01 06 02` (Hub Property 0x06, Enable Updates) |
| → Antwort | `06 00 01 06 06 <prozent>` |
| Fehlermeldung des Hubs | `05 00 05 <auslösendes Kommando> <fehlercode>` |
| Motor direkt (ungenutzt) | `08 00 81 <port> 00 51 00 <power>`, `0x7F` = bremsen, `0x00` = ausrollen |

---

## Xbox Series X\|S Controller (Modell 1914)

Der ESP32-C3 ist hier **kein Kompromiss, sondern Voraussetzung**: Der Controller
verlangt BLE 5.0. Auf dem klassischen ESP32 (BLE 4.2) kommt es zu
„HCI packet count mismatch"-Abbrüchen, auf dem C3 nicht.

Ältere Modelle: Xbox One S (1708) funktioniert meist mit aktueller Firmware,
Xbox One (1537/1697) hat gar kein Bluetooth und ist nicht nutzbar.

Werte aus `xbox.xboxNotif` (`XboxControllerNotificationParser`):

| Feld | Typ | Bereich |
|---|---|---|
| `joyLHori`, `joyLVert`, `joyRHori`, `joyRVert` | `uint16_t` | `0 … 0xFFFF`, Mitte ≈ 32768 |
| `trigLT`, `trigRT` | `uint16_t` | `0 … 0x3FF` — **10 Bit, nicht 16** |
| `btnA`, `btnB`, `btnX`, `btnY`, `btnLB`, `btnRB`, `btnLS`, `btnRS` | `bool` | |
| `btnStart`, `btnSelect`, `btnShare`, `btnXbox` | `bool` | |
| `btnDirUp`, `btnDirDown`, `btnDirLeft`, `btnDirRight` | `bool` | D-Pad |

Konstanten: `maxJoy = 0xFFFF`, `maxTrig = 0x3FF`, `expectedDataLen = 16`.

---

## Am Gerät gemessen — zwei Eigenheiten, die den Entwurf verändert haben

### 1. Der Hub meldet sich im Scan mit der Adresse `00:00:00:00:00:00`

Reproduzierbar am ESP32-C3 mit NimBLE 2.5.1: Nach dem **ersten erfolgreichen
Pairing** taucht der Hub im Scan zwar mit korrektem Namen und plausiblem RSSI
(−57 dBm) auf, aber mit einer Null-Adresse und `Typ=0` (public):

```
[Scan] Technic Move   00:00:00:00:00:00  Typ=0  RSSI=-57
```

Andere Geräte im selben Scan liefern gleichzeitig gültige Adressen, es ist also
kein allgemeiner Scan-Fehler. Ein Verbindungsaufbau darauf schlägt zwangsläufig
fehl. Vor dem ersten Pairing liefert der Scan dagegen eine brauchbare Adresse —
sonst wäre die Erstkopplung nie zustande gekommen.

**Lösung:** Die im Bond gespeicherte Adresse ist gültig und wird bevorzugt
benutzt. `firmware.ino` liest beim Verbinden die Bonds aus, filtert den
Controller anhand seiner bekannten Adresse heraus und verbindet direkt auf den
Rest — ganz ohne Scan. Der Scan bleibt nur der Weg für die Erstkopplung.

Nebeneffekt: Der Verbindungsaufbau ist dadurch deutlich schneller.

Ein `x` (alle Bonds löschen) stellt den Auslieferungszustand her; danach greift
wieder der Scan-Weg und koppelt neu.

### 2. Der Xbox-Controller meldet nur bei Änderungen

Gemessen: **0 Meldungen/s** am ruhenden, verbundenen Controller. Er sendet
keine periodischen HID-Reports, sondern nur, wenn sich etwas ändert.

Das macht einen Timeout auf `getReceiveNotificationAt()` als Failsafe
**gefährlich falsch**: Vollgas gegen den mechanischen Anschlag ist ein
konstanter Wert und erzeugt keine Meldungen — das Fahrzeug würde bei
durchgetretenem Gas abschalten.

**Lösung:** Die Failsafe trägt allein auf `xbox.isConnected()`. Unterste Ebene
bleibt der 10-Sekunden-Timeout des Hubs.

### 3. Das Supervision Timeout entscheidet über die Sicherheit

Weil die Failsafe an `isConnected()` hängt, ist die Zeit bis zum Erkennen eines
abgeschalteten Controllers **die** Sicherheitszahl: So lange fährt das Fahrzeug
im schlimmsten Fall führerlos weiter.

Gemessen (Stick wackeln, dabei Controller ausschalten; die letzte Meldung
markiert den Ausschaltzeitpunkt):

| | Abriss erkannt nach |
|---|---|
| Standardwerte des Controllers | **2610 ms** und **3180 ms** |
| nach `updateConnParams(12, 24, 0, 100)` | **963 ms** |

Die Firmware fragt das kürzere Timeout nach jedem Verbinden einmal an
(`tuneXboxLink()` in `firmware.ino`). Der Controller darf ablehnen — was
tatsächlich herauskam, steht als `Abriss:` in der Statuszeile.

Noch kürzer wäre möglich (Untergrenze laut Spezifikation: 60 ms bei 30 ms
Intervall), erhöht aber das Risiko von Fehlauslösungen: Zwei BLE-Verbindungen
teilen sich hier eine Antenne. 1 s ist der Kompromiss.

---

## Die beiden Fallen beim Zusammenbetrieb

### 1. NimBLE hat nur ein einziges globales Scan-Objekt

Zwei gleichzeitige Scans blockieren sich. Entscheidend ist dieser Fund im
Quelltext der Xbox-Bibliothek: **`onLoop()` scannt ausschließlich, solange der
Controller nicht verbunden ist** — der ganze Rumpf steckt in `if (!isConnected())`.

Daraus folgt die harte Regel der Firmware:

> Der Hub-Scan läuft nur, während `xbox.isConnected()` wahr ist.
> Fällt der Controller weg, wird der Hub-Scan sofort gestoppt.

Zusätzlich setzt die Xbox-Bibliothek bei jedem eigenen Scan ihre eigenen
Callbacks. `MoveHub::startScan()` setzt die eigenen deshalb jedes Mal neu.

*Scannen bei bestehender Verbindung* ist dagegen unproblematisch — genau das
passiert im Failsafe, wenn der Controller wegfällt und die Hub-Verbindung steht.

### 2. Die Sicherheitsparameter sind global

`xbox.begin()` setzt `NimBLEDevice::setSecurityAuth(true, false, false)` —
Bonding, kein MITM, **Legacy-Pairing** (`sc = false`). Das gilt dann auch für
den Hub. Level 2 ist mit Legacy-Pairing erreichbar, es sollte also passen.

Falls das Hub-Pairing trotzdem scheitert: `HUB_FORCE_SECURE_CONNECTIONS` in
[Config.h](../firmware/Config.h) auf `1`. Dann werden Secure Connections
gezielt nur für die Hub-Kopplung eingeschaltet und danach zurückgesetzt, damit
die Xbox-Seite bei Legacy bleibt.

Verbindungsintervalle: Der Hub wird bewusst auf 15–30 ms gebremst
(`updateConnParams`), damit er dem schnellen Xbox-Link auf der einen Antenne
nicht in die Quere kommt.

---

## Quellen

- [LEGO Wireless Protocol 3.0.00](https://lego.github.io/lego-ble-wireless-protocol-docs/) — offizielle Spezifikation
- [DanieleBenedettelli/TechnicMoveHub](https://github.com/DanieleBenedettelli/TechnicMoveHub) — Reverse Engineering des 88019, inkl. ESP32- und Xbox-Beispielen
- [pybricks Discussion #1733](https://github.com/orgs/pybricks/discussions/1733) — Hub-Typ `0x84`, Portbelegung, `GOPOS`-Absturz
- [Technic Move Hub Controller](https://move-hub.site/guide) — Fahrmodi, Kalibrierungsverhalten, 10-s-Timeout
- [asukiaaa/arduino-XboxSeriesXControllerESP32](https://github.com/asukiaaa/arduino-XboxSeriesXControllerESP32) — Xbox-BLE-Client
