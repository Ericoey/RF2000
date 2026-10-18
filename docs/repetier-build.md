# Repetier-Referenz für RF2000v2 bauen

Die hochgeladene ZIP enthält **Repetier Community Mod 1.45.01**, keine unveränderte Herstellerfirmware. `README.md` der Referenz nennt ausdrücklich den inoffiziellen Community-Mod. Diese Variante wird als zweite Entwicklungs- und Hardwarevergleichsbasis gebaut. Das Ergebnis bleibt Druckerfirmware mit ursprünglichen Heizungen, Temperaturmessung, LCD und Wägezellen. Es ist **kein CNC-Testimage** und wird durch die Buildwerkzeuge niemals geflasht.

## Voraussetzungen und Quelle

- Linux x86_64, Bash, Python 3, Git und Make.
- ATmega2560 bei 16 MHz; Arduino-Mega2560-Variante.
- AVR-GCC 14.2.0, AVR-Binutils 2.43.50 und AVR-libc 2.2.1, lokal unter `/workspace/.tools/avr` aus signierten Debian-Paketquellen entpackt. Die gemeinsame Umgebungsinstallation verwaltet diese Toolchain.
- Offizieller [Arduino AVR Core 1.8.6](https://github.com/arduino/ArduinoCore-avr/tree/1.8.6), fest auf Commit `42fa4a1ea1b1b11d1cc0a60298e529d37f9d14bd` gesetzt. SPI und Wire stammen aus diesem Core; zusätzliche Libraries werden nicht benötigt.
- Repetier-Referenz unter `references/repetier/` im Projekt oder im aktuellen Snapshot unter `/workspace/references/repetier`. `REPETIER_SOURCE` kann den Basispfad explizit setzen.

Der Arduino-Core wurde über das vorhandene HTTPS-Git-Proxy von `github.com` geladen. Der anfängliche Abruf des Arduino-CLI von `downloads.arduino.cc` lieferte HTTP 403; die funktionierende Lösung benötigt keinen CLI-Download und keine Arduino-IDE.

Core einmalig installieren, wenn er im Snapshot fehlt:

```bash
mkdir -p /workspace/.tools/repetier
git clone --depth 1 --branch 1.8.6 https://github.com/arduino/ArduinoCore-avr.git /workspace/.tools/repetier/ArduinoCore-avr
test "$(git -C /workspace/.tools/repetier/ArduinoCore-avr rev-parse HEAD)" = 42fa4a1ea1b1b11d1cc0a60298e529d37f9d14bd
```

Es werden TLS-, Signatur- oder Prüfsummenprüfungen nicht abgeschaltet. Benötigte Netzwerkziele sind `github.com` und für die gemeinsame Debian-Toolchain `deb.debian.org`.

## Kompilierung

```bash
cd /workspace/RF2000
./tools/build-repetier.sh
```

Das Skript verwendet die C/C++/Assembler-Rezepte von Arduino AVR Core 1.8.6 mit `-Os`, LTO und `--gc-sections`, erzeugt ein Core-Archiv und bindet SPI/Wire ein. Der Sketch benötigt keine automatisch erzeugten Funktionsprototypen: Er enthält nur die bereits in Arduino deklarierten Funktionen `setup()` und `loop()`. Maximal vier Compilerprozesse sind möglich; Standard sind zwei. Beispiele für unterstützte lokale Overrides:

```bash
BUILD_JOBS=4 ./tools/build-repetier.sh
AVR_PREFIX=/other/prefix/avr- ARDUINO_AVR_CORE=/other/ArduinoCore-avr ./tools/build-repetier.sh
```

Die Plattformauswahl erfolgt über den von der Referenz unterstützten Compilerparameter `-DMOTHERBOARD=DEVICE_TYPE_RF2000v2`. `FEATURE_MILLING_MODE` bleibt beim unveränderten RF2000v2-Standardwert `0`.

Die hochgeladene Community-Version enthält zusätzlich ein bedingungsloses `#error` für RF2000v2: Der Mod sei auf diesem Modell noch nicht umfassend getestet und der Hinweis solle nach Kenntnisnahme entfernt werden. Das Skript ersetzt **nur diese eine Zeile in einer wegwerfbaren Buildkopie** unter `build/repetier/source/Repetier/Configuration.h` durch einen entsprechenden Kommentar. Es prüft den exakten ursprünglichen Text und bricht bei Abweichungen ab. Die Originalreferenz bleibt unverändert; vor und nach dem Build werden ihre SHA256-Werte verglichen. Das Buildmanifest hält diese eine Abweichung fest.

## Verifizierte Ergebnisse

Der reale RF2000v2-Build wurde in dieser Cloudmaschine zweimal erfolgreich ausgeführt. ELF, HEX und EEPROM-Datei waren bytegleich. Die Linker-MAP-Datei enthält wechselnde Namen temporärer LTO-Dateien und ist deshalb nicht bytegleich; ihre jeweils aktuelle Prüfsumme wird gespeichert. Alle Artefaktprüfsummen wurden mit `sha256sum -c SHA256SUMS` geprüft. Das Skript prüft außerdem die Kapazitätsgrenzen einschließlich des reservierten 8-KiB-Mega-Bootloaders:

| Ressource | Belegt | Verfügbar |
|---|---:|---:|
| Flash (`.text` + `.data`) | 218996 Byte | 253952 Byte |
| Statisches SRAM (`.data` + `.bss`) | 6914 Byte | 8192 Byte |

Verbleibendes SRAM ist noch kein Nachweis ausreichenden Stacks unter allen Betriebsbedingungen. Hardwarelauf, Heizungen, Homing und Druckbetrieb wurden nicht getestet.

Vorhandene Compilerwarnungen der Referenz bleiben sichtbar: `Printer::setAxisDirection()` hat keinen Rückgabewert; `Printer::getAxisDirectionIsPos()` deckt nicht jeden Rückgabepfad ab. Sie sind im hochgeladenen Original vorhanden und werden durch das Setup nicht verändert. Der Linker meldet außerdem die serielle Ausführung seiner LTO-Partitionen; dies beeinträchtigt den erfolgreichen Build nicht.

Artefakte liegen unter `artifacts/repetier/`:

- `repetier-rf2000v2-reference.hex`: Anwendungsflash ohne Bootloader.
- `repetier-rf2000v2-reference.elf` und `.map`: Symbole, Debugdaten und Speicherzuordnung.
- `repetier-rf2000v2-reference.eep`: Intel-HEX-EEPROMdatei; die Referenz besitzt keine initialisierten `.eeprom`-Inhalte, daher nur EOF.
- `size.txt`, `build-manifest.json` und `SHA256SUMS`: Speicherbelegung, vollständige Compilerbefehle, Eingangsdatei-Hashes und Artefaktprüfsummen.
- `build.log`: Protokoll des ausgeführten Referenzbuilds.

`SOURCE_DATE_EPOCH=1767225600` ist als reproduzierbares Builddatum voreingestellt und kann bewusst überschrieben werden. Es beeinflusst den ursprünglichen Firmware-Builddatumsstring. Die Referenzdateien selbst werden dafür nicht verändert.

Für spätere Aufgaben den vorhandenen Checkout verwenden; die Cloudmaschine ist bereits isoliert und benötigt keinen Git-Worktree. Flashen ist keine Setupaktion und wurde nicht durchgeführt.
