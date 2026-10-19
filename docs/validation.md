# Prüfbericht

Stand: 19.10.2026. Alle folgenden Befehle wurden in der Cloudmaschine
ausgeführt. Die Cloudwerkzeuge haben keine Platine programmiert und keinen
Motor bewegt. Der Nutzer hat Display, Tasten, Y-Referenzfahrt und
Referenzschalter mit dem bisherigen Menü als funktionierend bestätigt;
das ist ein Anwenderbericht vom 19.10.2026, kein Messprotokoll der Cloud.

## Build und Wiederholbarkeit

| Variante | Flash | Statisches SRAM | Ergebnis |
| --- | ---: | ---: | --- |
| GRBL RF2000 XYZ-Test | 35.226 / 253.952 Byte | 3.520 / 8.192 Byte | gebaut |
| GRBL separater Y-Test mit Bedienfeld | 38.448 / 253.952 Byte | 3.697 / 8.192 Byte | gebaut |
| XYZ-CNC mit Display und manueller Spindel | 45.258 / 253.952 Byte | 3.710 / 8.192 Byte | gebaut |
| Repetier Community RF2000v2 | 218.996 / 253.952 Byte | 6.914 / 8.192 Byte | gebaut |

Das Standardprofil verwendet `.text=35176`, `.data=50`, `.bss=3470`;
das Y-Profil mit Bedienfeld `.text=38334`, `.data=114`, `.bss=3583`. Für SRAM werden
`.data` und `.bss` addiert. Die neue Binutils-Kurzausgabe zählt initialisierte
Daten bei dieser Zielplattform anders; die Kapazitätsprüfung verwendet
deshalb die einzelnen ELF-Sektionen. Statische SRAM-Reserve belegt keine
maximale Stacktiefe während aller Betriebsarten.

Der Installer wurde aus einem leeren zweiten Werkzeugpräfix ausgeführt:
signierte APT-Metadaten, festgelegte Debian-Paketversionen und der geprüfte
Arduino-Core-Commit. Anschließend entstand ein vollständiger GRBL-Build in
einem zweiten Buildverzeichnis. Sein HEX ist bytegleich zum Hauptbuild.
Das Standardprofil wurde mit der Bedienfelderweiterung erneut in einem
leeren zweiten Buildverzeichnis gebaut; sein HEX ist bytegleich zum
aktuellen Hauptbuild. Gegenüber dem vorherigen Stand verändert sich die
Binäranordnung trotz deaktiviertem Bedienfeld; der neue Hauptbuild wurde
mit `make check` vollständig geprüft. Repetier wurde mehrfach gebaut; HEX, ELF und EEP waren bei Wiederholung
bytegleich. MAP-Dateien können temporäre LTO-Pfade enthalten.

Nach der SFU1605-Anpassung bleibt das XYZ-Test-HEX bytegleich zum zuvor
vollständig getesteten Stand. Dessen früheres Testprotokoll bezieht sich
weiterhin auf den dort genannten ELF-Hash; `rebuilt_elf_sha256` kennzeichnet
den Neubuild. Das separate Y-Profil erhält ebenfalls den unten beschriebenen
LCD-Abstandsfix; seine mechanische Kalibrierung bleibt unverändert. Nach der
aktuellen CNC-Menü-/Freifahrerweiterung wurden Standard und Y erneut gebaut:
Beide HEX-Dateien bleiben bytegleich zu ihren vollständig geprüften Vorgängern.
Ihre ELF-Dateien enthalten neue Debug-/Link-Metadaten. Die alten Prüfprotokolle
behalten ihren tatsächlich simulierten ELF-Hash; die Buildmanifeste nennen
zusätzlich den Hash des jeweiligen Neubuilds.

Firmware-SHA256:

```text
GRBL     fc34c27dd636276c3a94135dc3b27a7b2c52b1c2371deabda7e47d811b62178f
Repetier cffc216c8d5f4c40fb4f7691d6aa3490faa1e21fdb5dfb04bf7d173bd58ca013
Y-Test   3e49a78a7ec6aab11e64dccdc216a7c64c55af04f9141e68e7e8b617ee9dceaa
CNC      d869cd2ba480c6df88d53bca8ad426610f245b44c6aa2454e1e73e871834859a
```

Die 64 extrahierten Repetier-Referenzdateien wurden zusätzlich byteweise
gegen das hochgeladene Archiv verglichen. Der dokumentierte RF2000v2-
Kenntnisnahmehinweis wird ausschließlich in der getrennten Buildkopie
ersetzt. Repetier bleibt Druckerfirmware mit ihren ursprünglichen Funktionen.

## Gesamttest `make check`

Ergebnis: **bestanden**, Exitstatus 0. Vollständige Ausgabe:
`artifacts/validation.log`.

| Prüfung | Ausgeführte Nachweise |
| --- | --- |
| Tatsächlicher Limit-Code mit GPIO-Modell | Alle 16 Kombinationen der vier Eingänge; feste Polarität; aktiver Eingang beim Init; sofortige Stopentscheidung; fünf freie Samples; Prellen; Z-Min/Z-Max; Fehlersperre; spätere Homing-Schalterauswahl |
| Tatsächliche DRV8711-Zustandsmaschine | Alle fünf Registerprofile; ENBL0 vor Wake; wirksamer Awake-Reset; UVLO-Löschung; Readbackfehler in allen drei Phasen; XYZ-Maske; Reserve ENBL0; 25-ms-Wake; persistenter Fault; Fehler während Wake; INT6-Flankenkonfiguration |
| Tatsächliche EEPROM-Funktionen mit ASAN/UBSAN | Getrennte Datensätze; Längen-/Indexgrenzen; NUL-Behandlung; Wiederherstellen; SREG-Erhaltung |
| Tatsächlicher serieller Treiber mit Host-UART-Modell | Fünf RX/TX-Konfigurationen, alle Startpositionen, FIFO/Umlauf, Voll/Leer, counts/available, Reset, Echtzeitbefehle, Jog-Abbruch und Überlaufalarm; sechs ungültige Kapazitäten abgewiesen |
| Echter AVR-ELF in simavr | Fünf DRV8711-SPI-Modelle; fabrikleeres EEPROM; Bootalarm; Befehls-/Einstellungsgrenzen; EEPROM über UART; STEP-/DIR-GPIO; bewegter Z-Max-Test; Fault-/Readback-Fehler; zwei gezielte ISR-Fehlerzeitpunkte; kurzer INT6-Puls |

Der ELF-Simulator prüfte genau 32/32/16 XYZ-Schritte vorwärts und dieselbe
Anzahl zurück. Die minimale normale Pulsbreite dieses Laufs betrug
**14,312 µs** bei eingestellten 10 µs. Ein weiterer Z-Lauf erzeugte genau
**6.400 Pulse**, mit **4992,81 Schritten/s** im schnellsten Fenster über
64 Flanken, bei Statusanfragen mit 100 Hz. Fremdbits von PORTF sowie
Reserve-/Lastenausgänge wurden mitbeobachtet.

Fehler wurden beim Eintritt in Timer1 und den verzögerten Timer0-ISR
eingespeist: kein weiterer STEP, alle Treiber schlafen, spezifischer
Treiberalarm 12 bleibt erhalten. Zusätzlich wurde ein nach **1 µs** bereits
beendeter FAULTn-Puls im Ruhezustand injiziert. INT6 verriegelt ihn; Ctrl-X
löscht diese Sperre nicht. Ein korrumpierter SPI-Readback verhindert `$X`
und Bewegungen. PH1/Z-Max beendet eine laufende Bewegung und blockiert die
Freigabe, solange der Eingang aktiv ist.

Der vollständige Lauf von `make check` einschließlich der zusätzlichen
GPIO-Prüfungen ist erfolgreich. Die AVR-Simulation prüft PA7 als Eingang ohne
Pullup, PC4 als LOW-Ausgang für OUT3 und PC0 als Endschaltereingang. Diese
GPIO-Konfiguration entspricht für PA7 Repetiers `HAL::stopWatchdog()`.
Die aktuelle Quellenpriorität bei abweichenden Hardwarezuordnungen liegt
nach Nutzervorgabe bei der Repetier-RF2000v2-Konfiguration. Diese
Quellenpriorität änderte keine GPIO-Pegel. Der aktuelle vollständige
Test nach der Bedienfelderweiterung steht in `artifacts/validation.log`.

## Separates Y-Testprofil

Das Y-Testprofil bleibt explizit als `rf2000_y_homing_test` verfügbar;
PlatformIO wählt jetzt standardmäßig `rf2000_cnc`. Die aufgelöste Konfiguration wurde
mit `pio project config --json-output` geprüft: Default, geerbte Buildflags,
`RF2000_Y_HOMING_TEST=1`, C-Quellen und Monitorparameter stimmen. Bei dieser
früheren Umstellung wurde nur die Auswahl geändert. Die damalige Y-Menüführung
wurde separat gebaut und mit dem im Prüfprotokoll genannten ELF geprüft.

Ergebnis: **13 Fälle bestanden**, Exitstatus 0 auf dem damaligen
Y-Bedienfeld-ELF am 19.10.2026. Das vollständige Protokoll steht in
`artifacts/y-homing-simulation.log`; dieses ELF wurde auch für die unten
beschriebenen 14 Panel-Fälle verwendet.

`make check-y-homing` baut `RF2000_Y_HOMING_TEST=1` in einem getrennten
Verzeichnis und führt `tests/run-y-homing-sim.sh` mit diesem tatsächlichen
AVR-ELF aus. Die Testfälle umfassen den vollständigen Referenzablauf, Kontakte
bei Schritt 761/762/763 an der 5-mm-Abschnittsgrenze, eine erste Suche über
mehr als zwei Abschnitte ohne Schalterkontakt sowie die Fehler- und
Abbruchpfade. Protokoll: `artifacts/y-homing-simulation.log`.

Dabei werden Y als einziger freigegebener Treiber, die gesperrten STEP-Ausgänge
von X/Z/A/B, zwei Kontakte und zwei Freigaben, Such-/Rückzugsrichtung,
60/20-mm/min-Testgeschwindigkeiten und 1-mm-Rückzüge geprüft. Weitere Fälle
prüfen eine fehlende zweite Betätigung, einen dauerhaft aktiven Schalter,
einen bereits aktiven Schalter beim Start, andere Endschalter, einen
1-µs-FAULT-Puls und Ctrl-X einschließlich erneuter `$22=0`-Sperre. Feste
Einstellungen, abgewiesene Bewegungsbefehle und Y-Jogs mit genau 15+15
Schritten werden ebenfalls kontrolliert. Alle 4096 EEPROM-Bytes werden vor
jedem Lauf mit einem Muster vorbelegt und auf unveränderten Inhalt geprüft.

Die erste Suche ist nach Nutzervorgabe fortlaufend und hat keine Gesamtweg-
oder Zeitbegrenzung. Wiederholte 5-mm-Planerblöcke begrenzen die Zählergröße;
ein fehlender erster Kontakt beendet die Suche nicht. Strg-X, Treiberfehler
und andere Endschalter brechen sie ab. Die zweite Annäherung bleibt auf 5 mm
begrenzt. Beim Phasenwechsel wird ein eventuell noch gesetztes CYCLE_STOP
verworfen, ohne RESET- oder Fehlerflags zu löschen. Das Standardprofil
behält seine bisherige Funktion; dessen vollständiger Prüflauf ist oben belegt.

Beide PlatformIO-Umgebungen wurden mit geerbten Buildflags und Monitoroptionen
korrekt eingelesen. Ein tatsächlicher PlatformIO-Build ist in der Cloud
weiterhin am Registry-Proxy blockiert (CONNECT HTTP 403); der belegte
Firmwarebuild verwendet AVR-GCC 14.2.0. Das mit PlatformIO/AVR-GCC 7.3.0
lokal erzeugte Y-ELF ist mit demselben Y-Simulator separat zu prüfen.

## XYZ-CNC und gSender-Protokoll

Ergebnis: **zehn CNC-Fälle bestanden**,
alle mit ELF `449731eb040e8e1833e64088ccc9cc5becd1b18fbe460189bfa109e2ade57446`.
Protokoll: `artifacts/cnc-simulation.log`. Nach der neuen Menüführung und
dem begrenzten Endschalter-Freifahren wurden alle neun Funktionsfälle und
der Timingfall zusammen erneut ausgeführt. Der CNC-Build verwendet
`.text=45140`, `.data=118`, `.bss=3592`; Buildprotokoll: `artifacts/cnc-build.log`.

Die `$I`-Antwort dieses AVR-Laufs wurde außerdem durch die tatsächliche
`Connection`-Klasse von [gSender v1.6.4](https://github.com/Sienci-Labs/gsender/blob/eb2df2d2073d9dd1a762c0ed150d9be19360cf49/src/server/lib/Connection.js)
geleitet. Ohne Startbanner und trotz Standardtyp grblHAL erkennt sie jetzt
**Grbl**. Die Gegenprobe ohne die neue Namenszeile reproduziert den früheren
Rückfall auf grblHAL. Externe Abhängigkeiten/serieller Port sind dabei
ersetzt; die Erkennungslogik selbst bleibt unverändert. Ergebnis und
Quellhash: `artifacts/gsender-detection.log`. Reproduktion mit dem genannten
gSender-Checkout und Node.js:

```bash
RF2000_HANDSHAKE_LOG="$PWD/build/tests/cnc-handshake.txt" tests/run-cnc-sim.sh build/cnc/main.elf connect
node tools/test_gsender_detection.cjs /tmp/rf2000-gsender-1.6.4 build/tests/cnc-handshake.txt
```

Geprüft wurden GRBL-Handshake und Status für gSender (`$I`, `$$`, `$G`, `$#`,
`?`), XYZ-Jogging am Tastenfeld, Arbeitskoordinaten auf dem tatsächlich
dekodierten LCD, bestätigtes Nullsetzen ohne Bewegung und Motorhaltung mit
`$1=255`. `$H` referenziert Z-Max vor X/Y-Min; jede Achse erreicht zwei Kontakte
und zwei Freigaben in vier Phasen. Einzelbefehle `$HX/$HY/$HZ` bewegen nur die
gewählte Achse und ergänzen die Referenzanzeige. G54-Rechteck, Z-Zustellung,
Kreisbahnen G2/G3 sowie Echtzeit-Pause `!` und Fortsetzen `~` bestehen. M3/M4/S
werden für die manuelle Spindel angenommen, ohne einen Ausgang einzuschalten.
A/B bleiben ohne STEP und Freigabe. Z-Min, lokale Pause/Reset-Taste und ein 1-µs-FAULT-Puls
schalten ab; Alarm 12 bleibt auf dem Display sichtbar.

Ein zusätzlicher Kalibrierungsfall startet mit einem gültigen alten
EEPROM-74-Datensatz: X/Y 152,38 Schritte/mm bei 500 mm/min. Das neue Programm
behält ihn beim Update. Eine verfrühte Umstellung auf 2560 Schritte/mm wird
wegen der 5-kHz-Grenze abgewiesen. Die dokumentierte Reihenfolge mit zuerst
100 mm/min funktioniert; die neuen Werte bleiben nach einem weiteren
MCU-Neustart erhalten, ebenso G54. Anschließende einzelne 1-mm-Jogs erzeugen
genau **2560 / 2560 / 1280 reale GPIO-Schrittimpulse** im Simulator.

Mit der höheren X/Y-Schrittzahl fand der Homing-/Alarmfall einen zu kurzen
LCD-Byteabstand von 206,938 µs. Der Service hatte seinen Zeitstempel vor
der Formatierung gesetzt; unter Schrittinterruptlast konnte diese bereits
mehrere Timer-Ticks dauern. Der Zeitstempel wird nun zusätzlich unmittelbar
nach dem vollständigen LCD-Transfer gesetzt. Die vorhandene Prüfung auf
mindestens 320 µs Abstand bleibt unverändert aktiv. Es gibt keine neue
Warteschleife im Schrittinterrupt.

Nach abgeschlossenen EEPROM-Schreibzugriffen wurde der vollständige Inhalt
in eine frische MCU-Instanz übernommen: Kalibrierung, Rückzug und G54 bleiben
erhalten; der Neustart bleibt verriegelt und bewegungslos. Stock-simavr 1.6
ignoriert die EEPM-Bits und behandelt Erase-only fälschlich wie normales
Schreiben aus EEDR. Das CNC-Testmodell ergänzt ausschließlich die AVR-
Programmiermodi Erase-only und Write-only um ihre Datenblattsemantik;
EEMPE-Fenster, Read-Pfad und vorhandene Simulator-Timer werden weiter benutzt.
Der Firmware-EEPROM-Treiber wurde dafür nicht verändert.

Mit aktivem CNC-LCD und 100-Hz-UART-Status entstehen genau **6400 Z-Pulse**,
bei **4994,59 Schritten/s** im schnellsten 64-Flanken-Fenster und minimal
**14,312 µs Pulsbreite**. Hierfür wurde nur im Simulator `$112=234` eingestellt;
die Firmwarevorgabe für Z bleibt 150 mm/min.

Dies ist ein Test der gSender-relevanten seriellen Befehle und des tatsächlichen
AVR-Programms, kein Lauf der gSender-Desktopanwendung an realer Hardware. Der
Nutzer hat die spätere physische Prüfung von X/Z ausdrücklich vorbehalten und
die angenommene Zuordnung zur Implementierung freigegeben. Es wird keine
vollständig gemessene Hardwarefreigabe behauptet. Bedienung und Grenzen:
[cnc-gsender.md](cnc-gsender.md).

## Neues CNC-Menü und Tastensteuerung

Ergebnis: **acht Gruppen bestanden**, Exitstatus 0 mit dem oben genannten
finalen CNC-ELF. Protokoll: `artifacts/menu-simulation.log`.
Reproduktion: `make check-menu`. Die Tests bedienen die elf realen GPIO-
Eingänge im Simulator und dekodieren den LCD-Bus; sie lesen keine privaten
Menüvariablen der Firmware.

Geprüft sind Navigation und Zurück ohne Bewegung, Start-/Freigabesperre für
Play, Jogschritt- und Tempogrenzen, alle sechs Achsrichtungstasten mit
256/256/128 Pulsen für 0,1 mm, Einzelnull nur für Y mit getrenntem
Bestätigungsdruck und die vier Homingphasen mit Z vor X+Y. Normale Fräsfahrt
zeigt ihren tatsächlich ausgeführten Achsblock; II bremst ab, ein zu früher
Play-Druck wird verworfen, Play setzt erst nach Stillstand fort. Vorschub-
korrekturen wirken von 10 bis 100 Prozent. II beendet Jog ohne Reset oder
Fortsetzung; langes II und Play+II brechen ab. II während des 25-ms-Wake
bricht ohne Schritte ab. Eine neue II-Flanke wirkt auch bei weiterhin
gehaltener Play-Taste nach einem früheren Reset.

Weitere Fälle prüfen gehaltenes II beim Einschalten und zur Laufzeit,
verworfene Tastendrücke kurz vor Bewegungsende, veraltete lokale
Bestätigungen bei UART-Fahrten sowie neue und wiederholte Alarme in
ruhenden Untermenüs. Diagnose bleibt nach Reset erreichbar. Diese neue
CNC-Belegung ist noch am Gerät zu prüfen; der frühere Anwenderbericht
bestätigt die Hardwareeingänge, nicht diesen neuen Bedienablauf.

## Begrenztes Endschalter-Freifahren

Ergebnis: **20 Fälle bestanden** auf demselben finalen CNC-ELF.
Protokoll mit Hash und Kommando: `artifacts/recovery-simulation.log`.
Reproduktion: `make check-recovery`.

Die realen STEP-/DIR-Ausgänge werden für X-Min, Y-Min, Z-Min und Z-Max
geprüft: genau 2 mm in der jeweiligen Freifahrrichtung bei angefordertem
F20, ohne Bewegung anderer Achsen oder A/B. Der Erfolg bleibt im Alarm und
löscht den Referenzstatus. Ein beim Einschalten aktiver Schalter und kurze
Kontaktpreller werden behandelt; ein voriger Fräsjob und während des
Rückzugs empfangene Zeilen werden nicht fortgesetzt.

Kein/mehrere aktive Schalter, falscher Betriebszustand, gehaltener Stop,
Treiberfehler und unbrauchbare Kalibrierungen werden ohne Bewegung
abgewiesen. Ein dauerhaft aktiver Schalter endet exakt nach 2 mm mit
Alarm 8. Wiederkontakt nach fünf freien Samples, ein anderer Endschalter
und ein 1-µs-FAULT-Puls stoppen mit ihrem spezifischen Alarm. Ctrl-X, `!`,
Jog-Abbruch `0x85` und lokale II brechen ab; Links/Play starten keine andere
Aktion und verändern den begrenzten Rückzug nicht. Die Displayfunktion
verlangt einen getrennten Bestätigungsdruck und zeigt den laufenden Rückzug.
Die elektrische Polarität und mechanische Freifahrrichtung an realer
Hardware werden damit nicht gemessen.

## Codeprüfung und Grenzen

### Originaldisplay und lokales Tastenfeld

Ergebnis: **14 Panel-Fälle bestanden**, Exitstatus 0 auf dem damals geprüften
Y-ELF `f6f7eacb846587b9dc51a3036407e32d31aea0dca15ebf397c3c2510e93883f8`.
Protokoll: `artifacts/panel-validation.log`. `make check-panel` kompiliert
den Test mit `-Wall -Wextra -Werror` und simuliert das tatsächliche AVR-ELF.

Der HD44780-Test dekodiert die realen GPIO-Nibbles beim fallenden E-Signal:
Initialisierung, vier DDRAM-Zeilen, Anzeigeinhalt, Hintergrundbeleuchtung,
Pullups, E-Pulsbreite und mindestens 320 µs Abstand zwischen ganzen Bytes.
Die Firmware lässt bei Laufzeitübertragungen mindestens 1 ms Abstand durch
zwei Tickdifferenzen des 1-kHz-Timers. Sie wartet nicht im Schrittinterrupt.

Weitere Fälle prüfen Start mit gehaltenem OK, Prellen, Mehrfachtasten,
außerhalb der Verfahrseite ignorierte Z-/Extrudertasten, Diagnose, Freigeben,
Y-Jogs mit genau 15+15
Schritten, keine Wiederholung und keine während einer Bewegung gespeicherten
Tastenaktionen. Zwei getrennte OK-Betätigungen führen den vollständigen
Referenzablauf mit zwei Kontakten und zwei Freigaben aus. Ein serieller Jog
verwirft einen geöffneten Bestätigungsdialog; OK öffnet anschließend nur
einen neuen Dialog.

Die neue Fahrseite wird ohne Bewegung geöffnet. Alle Schrittweiten
0,01/0,10/0,50 mm, Vorschübe 10/20/30 mm/min, begrenzte Zusatztasten und
Rücksetzen auf 0,10/F20 werden geprüft. Wirkliche Schrittzahlen, Zeitverhältnis
der Vorschübe und die Position gegenüber dem UART-MPos-Bericht stimmen.
Erfolgreiches Homing zeigt R:JA und die negative Maschinenposition; ein Alarm
löscht den Referenzstatus. Fehlerquittierung mit OK/Rechts löst keine Aktion aus.
Ein kurz vor Jog-Ende begonnener Tastendruck bleibt auch nach fertiger
Entprellung im Leerlauf verworfen; erst Loslassen und erneutes Drücken fährt.

Links, PH7 und PH2 wurden als Abbruch während aller vier Homingphasen und
während Jogging geprüft: Abschaltung innerhalb eines Timer2-Abfrageintervalls
(Testgrenze 1,2 ms), keine weiteren STEP-Pulse. Die Anzeige aktualisiert den
Alarm auch in der kritischen Warteschleife. Ein 1-µs-FAULT-Puls bleibt als
Alarm 12 sichtbar. Gehaltene STOP-Tasten bei Boot und nach Reset blockieren
auch serielle Entriegelung und Bewegungsstarts, ohne Y aufzuwecken. Nach
Loslassen funktioniert der begrenzte Jog wieder. X/Z/A/B bleiben gesperrt;
alle 4096 vorbesetzten EEPROM-Bytes bleiben unverändert. Dies sind
Simulatorbefunde, keine gemessenen Latenzen oder Displaytests an Hardware.

### Bisherige Firmwareprüfungen

Die unabhängigen Prüfungen fanden und korrigierten unter anderem die
DRV4-Pinabweichung, RESET/UVLO im Sleep, A/B-ENBL, das Überschreiben geteilter
STEP-Portbits, einen undefinierten Timerstart, die Alarmreihenfolge nach
`mc_reset`, EEPROM-Überlappung und den unbedingten Interrupt-Reenable im
EEPROM-Treiber. Serieller RX-Überlauf erzwingt nun einen Bewegungsreset und
die Quittierung eines kritischen Alarms. Der neue serielle Hosttest injiziert
gezielt RX-Überläufe und prüft Resetaufruf, Alarm und unveränderte FIFO-Daten.
Eine gezielte RX-Überlaufinjektion am tatsächlichen AVR-ELF war nicht Teil
des beschriebenen Simulatorlaufs.

Die gemeldeten GRBL-Warnungen wurden am 19.10.2026 behoben. Sechs
beabsichtigte Switch-Übergänge sind mit `__attribute__((fallthrough))`
ausdrücklich markiert; diese Schreibweise unterstützt auch das festgelegte
PlatformIO-AVR-GCC 7.3.0. Die vier Ringindexvergleiche werden nur bei
Ringen kleiner als 256 Byte übersetzt. Bei 256 Byte erfolgt der definierte
uint8-Umlauf automatisch. Eine fehlerhafte RX-Belegungszählung im Wrap-Fall
verwendet nun die Ringgröße statt der nutzbaren Kapazität. Die normale
Statusmeldung verwendet bereits die davon unabhängige verfügbare Kapazität.

`tools/test_serial_ring.py` kompiliert den tatsächlichen `serial.c` mit
`-Wall -Wextra -Werror` für RX/TX-Kapazitäten 255/255, 128/64, 1/1, 254/255
und 255/2. Alle Ringstartpositionen, Umlauf und FIFO-Reihenfolge, Voll/Leer,
Belegung/Freiplatz, Echtzeitkommandos am vollen RX, Resetabbruch am vollen TX,
Jog-Abbruch und RX-Überlaufalarm werden geprüft. Kapazitäten außerhalb 1–255
werden beim Kompilieren zurückgewiesen. Der Test ist Bestandteil von `make check`.

Zur damaligen Warnungsbereinigung wurden beide GRBL-Profile vollständig mit AVR-GCC 14.2.0 neu übersetzt:
keine Compilerwarnungen oder Fehler, unveränderte Flash-/SRAM-Werte und
bytegleiche HEX-Dateien gegenüber den zuvor simulierten Profilen. Damit sind
Motor-, Limit- und Homingprogramm unverändert. Warnungen wurden nicht global
abgeschaltet. Die unveränderte Repetier-Referenz enthält weiterhin die
ursprünglichen fehlenden Return-Pfade in `Printer.h`.

Die bisherige Repetier-Konfiguration wird nach Nutzervorgabe als
funktionierend zugrunde gelegt; abweichende Hardwarezuordnungen im Plan
werden anhand dieser Referenz entschieden und dem Nutzer mitgeteilt.
Nicht geprüft am GRBL-Port: elektrische
Signalqualität, Reset-/Watchdog-Laufverhalten, Endschalter-NO/NC und
Kabelbruchverhalten, tatsächliche Wicklungsströme, mechanische
Bewegungsrichtungen, reale Anhaltewege und
Dauerbetrieb. Im bisherigen XYZ-Testprofil bleibt Homing gesperrt; das CNC-Profil erlaubt es nach Nutzeranweisung mit noch angenommenen X/Z-Richtungen. Der
gesonderte Y-Test verlangt die Prüfungen aus [y-axis-test.md](y-axis-test.md).
Die Hardwareprüfung folgt
[hardware-test.md](hardware-test.md); Softwaretests erteilen keine
automatische Freigabe für eine Referenzfahrt oder den Fräsbetrieb.
