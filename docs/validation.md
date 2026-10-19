# Prüfbericht

Stand: 19.10.2026. Alle folgenden Befehle wurden in der Cloudmaschine
ausgeführt. Es wurde keine Platine programmiert und kein Motor bewegt.

## Build und Wiederholbarkeit

| Variante | Flash | Statisches SRAM | Ergebnis |
| --- | ---: | ---: | --- |
| GRBL RF2000 XYZ | 35.226 / 253.952 Byte | 3.520 / 8.192 Byte | gebaut |
| GRBL separater Y-Test mit Bedienfeld | 37.080 / 253.952 Byte | 3.702 / 8.192 Byte | gebaut |
| Repetier Community RF2000v2 | 218.996 / 253.952 Byte | 6.914 / 8.192 Byte | gebaut |

Das Standardprofil verwendet `.text=35176`, `.data=50`, `.bss=3470`;
das Y-Profil mit Bedienfeld `.text=36952`, `.data=128`, `.bss=3574`. Für SRAM werden
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

Firmware-SHA256:

```text
GRBL     fc34c27dd636276c3a94135dc3b27a7b2c52b1c2371deabda7e47d811b62178f
Repetier cffc216c8d5f4c40fb4f7691d6aa3490faa1e21fdb5dfb04bf7d173bd58ca013
Y-Test   b362bc6997204063a9d3751f904fcfa8a8ffac31dae3b8aab5d587cd65a2d81d
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

PlatformIO wählt für den aktuellen Hardwaretest jetzt standardmäßig
`rf2000_y_homing_test` mit LCD und Tasten. Die aufgelöste Konfiguration wurde
mit `pio project config --json-output` geprüft: Default, geerbte Buildflags,
`RF2000_Y_HOMING_TEST=1`, C-Quellen und Monitorparameter stimmen. Es wurden
keine Firmware-C-Dateien oder getesteten ELF-/HEX-Programme verändert;
die folgenden Simulationsergebnisse gelten für dieselben Programme.

Ergebnis: **13 Fälle bestanden**, Exitstatus 0 auf dem aktuellen
Bedienfeld-ELF am 19.10.2026. Das vollständige Protokoll steht in
`artifacts/y-homing-simulation.log`; dieses ELF wurde auch für die unten
beschriebenen elf Panel-Fälle verwendet.

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
behält seine bisherige Funktion und wurde auf seinem aktuellen ELF geprüft.

Beide PlatformIO-Umgebungen wurden mit geerbten Buildflags und Monitoroptionen
korrekt eingelesen. Ein tatsächlicher PlatformIO-Build ist in der Cloud
weiterhin am Registry-Proxy blockiert (CONNECT HTTP 403); der belegte
Firmwarebuild verwendet AVR-GCC 14.2.0. Das mit PlatformIO/AVR-GCC 7.3.0
lokal erzeugte Y-ELF ist mit demselben Y-Simulator separat zu prüfen.

## Codeprüfung und Grenzen

### Originaldisplay und lokales Tastenfeld

Ergebnis: **11 Panel-Fälle bestanden**, Exitstatus 0 auf dem aktuellen
Y-ELF `2b385eaf98943fb25c0cce049f2ad7af2d51f3b05c57b4eb8d82dc9f50a8d49d`.
Protokoll: `artifacts/panel-validation.log`. `make check-panel` kompiliert
den Test mit `-Wall -Wextra -Werror` und simuliert das tatsächliche AVR-ELF.

Der HD44780-Test dekodiert die realen GPIO-Nibbles beim fallenden E-Signal:
Initialisierung, vier DDRAM-Zeilen, Anzeigeinhalt, Hintergrundbeleuchtung,
Pullups, E-Pulsbreite und mindestens 320 µs Abstand zwischen ganzen Bytes.
Die Firmware lässt bei Laufzeitübertragungen mindestens 1 ms Abstand durch
zwei Tickdifferenzen des 1-kHz-Timers. Sie wartet nicht im Schrittinterrupt.

Weitere Fälle prüfen Start mit gehaltenem OK, Prellen, Mehrfachtasten,
ignorierte Z-/Extrudertasten, Diagnose, Entriegeln, Y-Jogs mit genau 15+15
Schritten, keine Wiederholung und keine während einer Bewegung gespeicherten
Tastenaktionen. Zwei getrennte OK-Betätigungen führen den vollständigen
Referenzablauf mit zwei Kontakten und zwei Freigaben aus. Ein serieller Jog
verwirft einen geöffneten Bestätigungsdialog; OK öffnet anschließend nur
einen neuen Dialog.

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
Dauerbetrieb. Allgemeines Homing bleibt im Standardprofil gesperrt; der
gesonderte Y-Test verlangt die Prüfungen aus [y-axis-test.md](y-axis-test.md).
Die Hardwareprüfung folgt
[hardware-test.md](hardware-test.md); Softwaretests erteilen keine
automatische Freigabe für eine Referenzfahrt oder den Fräsbetrieb.
