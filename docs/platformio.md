# GRBL mit PlatformIO kompilieren

Die Datei `platformio.ini` im Repository-Hauptverzeichnis definiert die
Standardumgebung **`rf2000_cnc`** mit XYZ-Homing, Display und Tastenfeld.
Die Spindel bleibt manuell. Zusätzlich gibt es `rf2000_y_homing_test` für den
isolierten Y-Test und `rf2000_grbl` als XYZ-Test ohne LCD und Homing.
Alle Profile verwenden ATmega2560 bei 16 MHz. Die RF2000-Pinbelegung
und sämtliche Sicherheitsparameter stammen weiterhin aus dem GRBL-Quellcode.
Die CNC-Bedienung einschließlich gSender steht in [cnc-gsender.md](cnc-gsender.md).
Das aktuelle CNC-Profil verwendet adaptive Hardware-Mikroschritte von
1/256 bis 1/8 bei einem festen virtuellen Raster von nominal
**20480/20480/20480 Einheiten/mm**. Die CNC-Versionen 74 und 75 werden auf
Version 76 umgerechnet. Alte Werte 320, 1280 oder 2560 nicht zurückspielen.
Nach dem Aufspielen `$I`, `$$` und `$MS` prüfen. Betrieb und Testplan:
[Adaptive Mikroschritte](adaptive-mikroschritte.md) und
[Geschwindigkeit](geschwindigkeit-optimieren.md).

## VS Code

1. Die Erweiterung **PlatformIO IDE** von PlatformIO installieren.
2. Das Repository aktualisieren (`git pull`) oder
   `https://github.com/Ericoey/RF2000` klonen.
3. In PlatformIO **Open Project** wählen und den Ordner `RF2000` öffnen,
   in dem `platformio.ini` liegt.
4. Unter **Project Tasks → rf2000_cnc → General → Build** kompilieren.
   Alternativ das Häkchen **Build** in der unteren Statusleiste verwenden;
   bei einer zuvor ausdrücklich gewählten Umgebung dort ebenfalls
   `rf2000_cnc` auswählen.

Beim ersten Build lädt PlatformIO die festgelegte AVR-Plattform und
Werkzeugkette herunter. Für das Kompilieren ist keine angeschlossene Platine
erforderlich. Die normale Build-Aktion überträgt keine Firmware.

## Kommandozeile

Mit einer vorhandenen PlatformIO-Installation im Projektordner:

```bash
git pull
pio run
```

`pio run` ohne `-e` baut das XYZ-CNC-Profil mit LCD. Ein sauberer Neubuild:

```bash
pio run -t clean
pio run
```

Die explizite Auswahl ist ebenfalls möglich:

```bash
pio run -e rf2000_cnc
```

Für das separate Y-Testprofil:

```bash
pio run -e rf2000_y_homing_test
```

In VS Code: **Project Tasks → rf2000_y_homing_test → General → Build**.
Dieses Image liegt unter `.pio/build/rf2000_y_homing_test/firmware.hex`;
ELF und MAP liegen daneben. Für das XYZ-Profil ohne LCD ausdrücklich
`pio run -e rf2000_grbl` verwenden. Unterschiede und Prüfablauf stehen in
[y-axis-test.md](y-axis-test.md).
Das Y-Profil aktiviert auch das originale LCD und Tastenfeld für den
lokalen Test: [Bedienungsanleitung](local-panel.md). Weitere Bibliotheken
oder Änderungen an `platformio.ini` sind dafür nicht nötig.

In dieser Cloudumgebung liegt PlatformIO in einem beschreibbaren
Python-Virtualenv. Dafür stehen folgende Befehle bereit:

```bash
cd /workspace/RF2000
tools/install-platformio.sh
tools/platformio.sh run
```

`tools/platformio.sh` setzt das Cloud-Paketverzeichnis auf
`/workspace/.tools/platformio`. VS Code auf dem eigenen Rechner verwendet
seine übliche PlatformIO-Installation; der Cloud-Wrapper ist dafür nicht nötig.

## Ergebnisse und Buildaufbau

Die Dateien liegen nach einem erfolgreichen Build unter:

```text
.pio/build/rf2000_cnc/firmware.hex
.pio/build/rf2000_cnc/firmware.elf
.pio/build/rf2000_cnc/firmware.map
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
pio run -t clean
pio run
```

Die Korrektur wurde zusätzlich mit dem echten `avr-size` und einem relativen
`.pio/…/firmware.elf` durch den vollständigen Python-Post-Build-Hook geprüft.

## Aufspielen und seriellen Monitor öffnen

Den Monitor vor dem Upload schließen. Für den bekannten Mac-Port lauten die
Befehle nach `git pull`:

```bash
pio run -t upload --upload-port /dev/cu.usbserial-AL03P2K9
pio device monitor --port /dev/cu.usbserial-AL03P2K9
```

Den Port bei Bedarf durch den tatsächlich angezeigten Gerätenamen ersetzen.
Der Upload verwendet ohne `-e` das CNC-Profil. Nach einem Neustart muss die
Startmeldung **`RF2000 XYZ CNC`** enthalten. **`RF2000 XYZ test profile`**
bedeutet, dass noch das Profil ohne LCD aufgespielt ist. Die Auswahl einer
Umgebung beim Monitorstart programmiert keine andere Firmware. Bei weiterhin
schwarzen Kästchen trotz bestätigtem Y-Profil muss die LCD-Ansteuerung am
Gerät weiter geprüft werden; die Profilwahl allein belegt keine Funktion.

PlatformIO bringt den benötigten Monitor mit. Für das Y-Testprofil:

```bash
pio device monitor -e rf2000_y_homing_test
```

Bei Bedarf mit `pio device list` den Port bestimmen und beispielsweise auf
dem Mac `--port /dev/cu.usbserial-...` mit dem tatsächlichen Gerätenamen
anhängen. Beide Umgebungen konfigurieren 115200 Baud, LF-Zeilenende und
lokale Eingabeanzeige. Befehle wie `$DRV`, `$X`, `$22=1` und `$HY` jeweils
eingeben und mit Enter senden. Bewegungsbefehle erst nach den Vorprüfungen
aus [y-axis-test.md](y-axis-test.md) senden. Öffnen des Ports kann einen
Reset auslösen; die Y-Homing-Freigabe `$22=1` gilt nur bis zum nächsten Reset.

Ctrl-X sendet den Echtzeit-Reset (Byte `0x18` ohne Enter). Ctrl-C beendet nur
den Monitor und stoppt keine Bewegung. Während Homing stoppt `!` nicht;
Software-Abbruch mit Ctrl-X und unabhängige Hardware-Abschaltung bereithalten.

## Hardwaretest

A/B bleiben deaktiviert. Das CNC-Profil ermöglicht nach ausdrücklicher
Nutzervorgabe XYZ-Homing mit angenommenen X/Y-Min- und Z-Max-Richtungen;
physisch bestätigt ist bisher Y. Die Spindel wird manuell bedient. Details:
[CNC und gSender](cnc-gsender.md). Homing bleibt im **XYZ-Testprofil
`rf2000_grbl`** gesperrt. Das separat auswählbare Y-Profil hält X/Z/A/B abgeschaltet und erlaubt ausschließlich
begrenztes Y-Jogging sowie `$HY` nach bewusstem `$22=1` und bestandener
Richtungs-/Endschalterprüfung. Die erste Referenzsuche läuft ohne Gesamtweg-
oder Zeitbegrenzung bis zum Y-Min-Kontakt. Ein fehlender Kontakt führt dabei
nicht zu einem automatischen Stopp; Ctrl-X sendet den Software-Abbruch.
Nach dem ersten Kontakt folgen Rückzug, langsame zweite Annäherung und
abschließender Rückzug. Es wurde
keine Uploadaktion ausgeführt. Die Mega-Boarddefinition enthält allgemeine
Uploadvorgaben; das tatsächliche Programmierverfahren muss vor Ort geprüft
werden. Ein Build bestätigt keine
reale Platinenrevision, Motorströme oder Bewegungsrichtungen. Vor einer
Programmierung und vor Bewegungen den
[kontrollierten Hardwaretest](hardware-test.md) durchführen.

Die Referenz-Repetier-Firmware wird weiterhin über den in
[repetier-build.md](repetier-build.md) dokumentierten Build erzeugt.

## Stand der Cloudprüfung

Am 22.10.2026 wurde das adaptive CNC-Profil mit der festgelegten
PlatformIO-Plattform **Atmel AVR 5.1.0** und **AVR-GCC 7.3.0** vollständig
gebaut. Flash: **50.912 Byte**, statisches SRAM: **3.893 Byte**. Auch die
zusätzliche Sektions-/Kapazitätsprüfung und die HEX-Erzeugung liefen
mit Exitstatus 0 durch. Der frühere Registry-403 verhinderte diesen
aktuellen Build nicht; nach einem verweigerten Spiegel wurde ein anderer
Paketspiegel erfolgreich verwendet.

Der dabei erzeugte ELF-SHA256 lautet:

```text
92e5ecbd0ec07bed5d2c0f3c35d55088b1d6f654611b47e924df46d07a40fc63
```

HEX-SHA256:

```text
d477d766d5b8650f4125237629c57a0580d4afa16d063535ff0953a5b81def56
```

Der separate Make-Build mit AVR-GCC 14.2.0 benötigt 52.078 Byte Flash und
3.893 Byte statisches SRAM. Die unterschiedlichen Compiler erzeugen
verschiedene Programme; Prüfergebnisse müssen deshalb dem jeweiligen ELF
zugeordnet werden. Ein erfolgreicher Build allein belegt noch keinen
bestandenen Bewegungs-/Fehlerfall. Das erzeugte PlatformIO-ELF hat die vollständige adaptive Suite mit
**28 AVR-Simulatorfällen** bestanden, Exitstatus 0. Genaue Kommandos,
Messwerte und Hashzuordnung stehen im [Prüfbericht](validation.md).

Reproduktion der adaptiven AVR-Prüfung mit einem lokal gebauten Image:

```bash
tests/run-adaptive-sim.sh .pio/build/rf2000_cnc/firmware.elf
```

`make check-adaptive`, `make check-cnc` und `make check-speed` verwenden
dieselbe adaptive Suite mit dem Make-ELF. Die getrennten Standard-/Y-Profile
haben weiterhin eigene Testprogramme:

```bash
tests/run-sim.sh .pio/build/rf2000_grbl/firmware.elf
tests/run-y-homing-sim.sh .pio/build/rf2000_y_homing_test/firmware.elf
tests/run-panel-sim.sh .pio/build/rf2000_y_homing_test/firmware.elf
```

Dazu die jeweilige Umgebung vorher ausdrücklich bauen. Die erfolgreiche
CNC-Kompilierung ist keine Behauptung, dass alle drei PlatformIO-Images in
diesem Lauf geprüft wurden. Es wurde keine Platine programmiert.
