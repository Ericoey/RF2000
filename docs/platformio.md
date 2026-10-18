# GRBL mit PlatformIO kompilieren

Die Datei `platformio.ini` im Repository-Hauptverzeichnis definiert die
Umgebung **`rf2000_grbl`** für ATmega2560 bei 16 MHz. Die RF2000-Pinbelegung
und sämtliche Sicherheitsparameter stammen weiterhin aus dem GRBL-Quellcode.

## VS Code

1. Die Erweiterung **PlatformIO IDE** von PlatformIO installieren.
2. Das Repository aktualisieren (`git pull`) oder
   `https://github.com/Ericoey/RF2000` klonen.
3. In PlatformIO **Open Project** wählen und den Ordner `RF2000` öffnen,
   in dem `platformio.ini` liegt.
4. Unter **Project Tasks → rf2000_grbl → General → Build** kompilieren.
   Alternativ das Häkchen **Build** in der unteren Statusleiste verwenden.

Beim ersten Build lädt PlatformIO die festgelegte AVR-Plattform und
Werkzeugkette herunter. Für das Kompilieren ist keine angeschlossene Platine
erforderlich. Die normale Build-Aktion überträgt keine Firmware.

## Kommandozeile

Mit einer vorhandenen PlatformIO-Installation im Projektordner:

```bash
pio run -e rf2000_grbl
```

`pio run` verwendet dieselbe Standardumgebung. Ein sauberer Neubuild:

```bash
pio run -e rf2000_grbl -t clean
pio run -e rf2000_grbl
```

In dieser Cloudumgebung liegt PlatformIO in einem beschreibbaren
Python-Virtualenv. Dafür stehen folgende Befehle bereit:

```bash
cd /workspace/RF2000
tools/install-platformio.sh
tools/platformio.sh run -e rf2000_grbl
```

`tools/platformio.sh` setzt das Cloud-Paketverzeichnis auf
`/workspace/.tools/platformio`. VS Code auf dem eigenen Rechner verwendet
seine übliche PlatformIO-Installation; der Cloud-Wrapper ist dafür nicht nötig.

## Ergebnisse und Buildaufbau

Die Dateien liegen nach einem erfolgreichen Build unter:

```text
.pio/build/rf2000_grbl/firmware.hex
.pio/build/rf2000_grbl/firmware.elf
.pio/build/rf2000_grbl/firmware.map
```

Die Konfiguration verwendet Atmel AVR 5.1.0 und die festgelegte
PlatformIO-AVR-Werkzeugkette `1.70300.191015` (AVR-GCC 7.3.0). Es werden nur
die C-Dateien aus `grbl/` gebaut; das Beispielsketch wird ausgeschlossen.
GRBL stellt seine eigene `main()`-Funktion und Interruptbehandlung bereit.
Die Konfiguration bindet deshalb keinen Arduino-Core ein.

Das zusätzliche Buildskript bindet die Mathematikbibliothek ein, erzeugt
die Linker-MAP und prüft die tatsächlichen ELF-Sektionen: höchstens
253.952 Byte Anwendungsflash bei 8 KiB reservierter Bootloaderfläche und
8.192 Byte statisches SRAM. Eine größere Firmware führt zu einem Buildfehler.
Stackbedarf bleibt zusätzlich zu berücksichtigen.

Die bestehende Make-Werkzeugkette verwendet AVR-GCC 14.2.0. Unterschiedliche
Compiler können unterschiedliche HEX-Dateien und Laufzeiten erzeugen.
Die Softwareprüfung muss daher zum tatsächlich verwendeten ELF gehören.
Der Hardwaretestplan gilt auch für ein mit PlatformIO erzeugtes Image.

## Fehler `ValueError: invalid literal for int() with base 10: ':'`

Dieser Fehler stammt aus einer älteren Version der zusätzlichen
Speicherprüfung. Die Überschrift `.pio/build/…/firmware.elf :` wurde als
Speichersektion interpretiert. Der Parser akzeptiert jetzt ausschließlich
Sektionszeilen mit numerischer Größe und Adresse. Acht Regressionstests
prüfen unter anderem relative Pfade, Leerzeichen, Windows-Trennzeichen und
die weiterhin wirksamen Flash-/SRAM-Grenzen.

Nach dem Aktualisieren des Repositorys einen sauberen Build ausführen:

```bash
git pull
pio run -e rf2000_grbl -t clean
pio run -e rf2000_grbl
```

Die Korrektur wurde zusätzlich mit dem echten `avr-size` und einem relativen
`.pio/…/firmware.elf` durch den vollständigen Python-Post-Build-Hook geprüft.

## Hardwaretest

A/B bleiben deaktiviert und Homing bleibt im Testprofil gesperrt. Es wurde
keine Uploadaktion ausgeführt. Die Mega-Boarddefinition enthält allgemeine
Uploadvorgaben; das tatsächliche Programmierverfahren muss vor Ort geprüft
werden. Ein Build bestätigt keine
reale Platinenrevision, Motorströme oder Bewegungsrichtungen. Vor einer
Programmierung und vor Bewegungen den
[kontrollierten Hardwaretest](hardware-test.md) durchführen.

Die Referenz-Repetier-Firmware wird weiterhin über den in
[repetier-build.md](repetier-build.md) dokumentierten Build erzeugt.

## Stand der Cloudprüfung

PlatformIO Core 6.1.19 wurde installiert und die Projektkonfiguration wurde
mit `pio project config` erfolgreich eingelesen. Die offizielle
Atmel-AVR-Plattformquelle und die Mega-Boarddefinition wurden geprüft.
Der vollständige PlatformIO-Build ist in dieser Cloudmaschine derzeit noch
**nicht verifiziert**: Der Netzwerkproxy verweigert den Registry-Abruf mit
HTTP 403, bevor der Compiler installiert werden kann. Die benötigten
Registry-/Download-Domains sind im Umgebungsentwurf eingetragen.
Nach dem Speichern der Netzwerkfreigabe `tools/install-platformio.sh` und
`tools/platformio.sh run -e rf2000_grbl` erneut ausführen und das erzeugte
ELF mit `tests/run-sim.sh .pio/build/rf2000_grbl/firmware.elf` prüfen.
