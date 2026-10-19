# Entwicklung und Build

Die Cloudumgebung verwendet den vorhandenen Checkout `/workspace/RF2000`.
Jede Cloudaufgabe ist bereits isoliert; ohne ausdrücklichen Auftrag keinen
weiteren Git-Worktree anlegen. Es sind keine Hintergrunddienste und keine
Zugangsschlüssel erforderlich. Hardware ist in dieser Cloudmaschine nicht
angeschlossen.

## Werkzeuge installieren

```bash
cd /workspace/RF2000
tools/install-toolchain.sh
make -j4
make check
make repetier
```

Der Installer funktioniert ohne Rootrechte auf Debian 13/x86_64. Er entpackt
AVR-GCC 14.2.0, AVR-Binutils 2.43.50.20250108, AVR-libc 2.2.1 und simavr 1.6
nach `/workspace/.tools/avr`. APT prüft signierte Debian-Metadaten und die
Paketprüfsummen. Arduino AVR Core 1.8.6 wird über HTTPS von GitHub geladen und
gegen den festgelegten Commit geprüft. Es wird keine Systemkonfiguration
ersetzt. Wiederholte Aufrufe verwenden die vorhandenen Werkzeuge; fremde
Änderungen am Arduino-Core werden nicht überschrieben.

Die vollständige Installation wurde zusätzlich in einem leeren Präfix
`/workspace/.tools/repro-verified` ausgeführt. Die Liveumgebung und die
gespeicherten Installationsanweisungen sind getrennt: Änderungen am Entwurf
müssen in den Umgebungseinstellungen gespeichert und anschließend
veröffentlicht werden. Eine neue Cloudaufgabe wurde hier nicht gestartet.

Netzwerkziele: `deb.debian.org`, `github.com` und für den offiziellen
Datenblattabruf `www.ti.com`. Die Referenz-PDFs liegen bereits unter
`references/`. Der Build benötigt keinen Arduino-CLI-Download.

## Ergebnisse

PlatformIO baut mit `pio run` standardmäßig das Y-Testprofil einschließlich
Originaldisplay und Tastenfeld. Das XYZ-Profil ohne LCD wird ausdrücklich
mit `pio run -e rf2000_grbl` gewählt. In der Cloud zuerst
`tools/install-platformio.sh` und anschließend
`tools/platformio.sh run` verwenden. Anleitung und
Ausgabepfade stehen in [platformio.md](platformio.md).

`make` erzeugt `build/main.elf` sowie HEX, ELF und MAP unter `artifacts/grbl/`.
`make disasm` erzeugt das Disassembly `build/rf2000-grbl.lst`.
Die Speicherprüfung reserviert 8 KiB für einen möglichen Mega-Bootloader;
das tatsächliche Programmierverfahren ist separat am Gerät festzulegen.

`make check` führt Prüfungen am tatsächlichen C-Code und am kompilierten
ATmega2560-ELF mit einem DRV8711-/GPIO-Modell aus. Der Simulator ersetzt weder
Motoren noch elektrische Messungen. Ein Hosttest kann keine reale
Schalterpolarität, Netzzuordnung oder Motorstrom bestätigen.

Die unveränderte Community-Repetier-Referenz und ihre getrennte Buildkopie
sind in [repetier-build.md](repetier-build.md) beschrieben. Repetier-HEX ist
weiterhin Druckerfirmware mit Heizfunktionen und kein CNC-Testimage.

## Firmwareauswahl und spätere Änderungen

Das Testprofil plant drei logische Achsen. Alle fünf Hardwaretreiber werden
konfiguriert und zurückgelesen. A/B bleiben mit SLEEPn LOW, CTRL.ENBL=0 und
STEP LOW gesperrt. Fünf logische Achsen würden versehentliche A/B-Kommandos
zulassen; drei Achsen vermeiden dies auch im Parser und in den Statusdaten.
Für eine spätere Aktivierung müssen Achsenzahl, Pinmap, Schrittgenerator,
Planerparameter, EEPROM-Version und Testabdeckung bewusst erweitert werden.
Das fünfkanalige SPI-/Freigabemodul kann dabei wiederverwendet werden.

Im Standardprofil ist Homing durch `RF2000_HOMING_VERIFIED=0` zusätzlich zur normalen
GRBL-Einstellung gesperrt. Die Kandidaten aus dem Repetier-Fräsmodus sind
X/Y Richtung MIN und Z Richtung MAX. Die Hardwareprüfung muss diese
Zuordnung und die tatsächlichen Richtungen erst bestätigen. Auch eine spätere
Änderung von `$3`, `$23`, Schalterverkabelung oder Mechanik erfordert erneute
Verifikation. Eine Compileroption allein ist kein Prüfnachweis.

Das gesonderte Profil `rf2000_y_homing_test` wird mit `make y-homing-test`
oder `pio run -e rf2000_y_homing_test` gebaut. `make check-y-homing` prüft
dessen tatsächliches AVR-ELF. Es erlaubt nur Y-Jogs und nach `$22=1` nur
`$HY`; die erste Suche läuft nach Nutzervorgabe bis zum Schalterkontakt
ohne Gesamtweg- oder Zeitlimit. Reset löscht die Freigabe. Dieses Profil
übernimmt keine EEPROM-Einstellungen und schreibt kein EEPROM. Ablauf und
Testwerte stehen in [y-axis-test.md](y-axis-test.md).
Dasselbe Y-Profil aktiviert das Original-LCD und Tastenfeld ohne zusätzliche
Bibliothek. `make check-panel` prüft die lokale Bedienung am tatsächlichen
AVR-ELF. Menü, Abbruchtasten und zweifache Referenzbestätigung stehen in
[local-panel.md](local-panel.md). Das Standardprofil aktiviert diese UI nicht.

`make flash`, `make fuse`, `make install` und `make load` brechen bewusst ab.
Quellcode und Artefakte werden bereitgestellt; es wurde nichts geflasht.
Das kontrollierte Vorgehen steht in [hardware-test.md](hardware-test.md).

Für die Hardwarezuordnung gilt nach jüngster ausdrücklicher Nutzervorgabe die
Konfiguration der bereitgestellten alten RF2000-Repetier-Firmware als
funktionierend. Bei Abweichungen zum ergänzenden Schaltplan hat ihr
RF2000v2-Block Vorrang. Das bereitgestellte Archiv ist laut eigener README die
Community-Modifikation RF 1.45.Mod; eine unveränderte Herstellerfassung ist
damit nicht nachgewiesen. Die Vorgabe ersetzt den früheren absoluten
Schaltplanvorrang und ist kein Hardwareprüfnachweis. TI bleibt für
DRV8711-Protokoll und Registersemantik maßgeblich.

PA7/D29 ist `WATCHDOG_PIN`, PC4 ist OUT3 und PC0/D37 bleibt ES2/Y-Min. PA7 und PC4
werden nicht als gemeinsames OUT3 behandelt. Die elektrischen Pegel bleiben
unverändert: PC4 LOW und PA7 Eingang ohne Pullup, entsprechend
`HAL::stopWatchdog()` der Referenz. GRBL erzeugt keinen aktiven Watchdog-Takt.
Das gleichlautende PDF-Netzlabel `PC0` bei WDI belegt für sich keine Verbindung
zum CPU-Portpin PC0. Weitere Änderungen der Hardwarezuordnung werden dem
Nutzer mit bisheriger und neuer Zuordnung ausdrücklich mitgeteilt.
