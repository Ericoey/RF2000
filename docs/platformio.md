# GRBL mit PlatformIO kompilieren

Die Datei `platformio.ini` im Repository-Hauptverzeichnis definiert die
Standardumgebung **`rf2000_cnc`** mit XYZ-Homing, Display und Tastenfeld.
Die Spindel bleibt manuell. Zusätzlich gibt es `rf2000_y_homing_test` für den
isolierten Y-Test und `rf2000_grbl` als XYZ-Test ohne LCD und Homing.
Alle Profile verwenden ATmega2560 bei 16 MHz. Die RF2000-Pinbelegung
und sämtliche Sicherheitsparameter stammen weiterhin aus dem GRBL-Quellcode.
Die CNC-Bedienung einschließlich gSender steht in [cnc-gsender.md](cnc-gsender.md).
Das aktuelle CNC-Profil verwendet fest X/Y 1/4 und Z 1/16. Beim Update von
CNC-Version 74 auf 75 werden alte X/Y-Schrittwerte passend umgerechnet;
die nominalen Werte sind danach **320/320/1280 Schritte/mm**. Alte X/Y-Werte
2560 anschließend nicht aus einer Sicherung zurückspielen. Vor Bewegungen
`$I` und `$$` prüfen; Ablauf und Geschwindigkeitsstufen stehen in
[geschwindigkeit-optimieren.md](geschwindigkeit-optimieren.md).

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

PlatformIO Core 6.1.19 wurde installiert und die Projektkonfiguration wurde
mit `pio project config` erfolgreich eingelesen. Die offizielle
Atmel-AVR-Plattformquelle und die Mega-Boarddefinition wurden geprüft.
Der vollständige PlatformIO-Build ist in dieser Cloudmaschine derzeit noch
**nicht verifiziert**: Der Netzwerkproxy verweigert den Registry-Abruf mit
HTTP 403, bevor der Compiler installiert werden kann. Die benötigten
Registry-/Download-Domains sind in der Umgebungskonfiguration eingetragen;
der erneute Abruf für das Y-Profil scheitert weiterhin an diesem Proxy.
Die Konfiguration aller drei Umgebungen einschließlich geerbter Buildflags wurde
erfolgreich eingelesen. Der Make-Build mit AVR-GCC 14.2.0 ist davon getrennt.
Wenn der Registry-Zugriff verfügbar ist, die gewünschte PlatformIO-Umgebung
bauen und deren tatsächliches ELF prüfen: für das XYZ-Profil mit
`tests/run-sim.sh .pio/build/rf2000_grbl/firmware.elf`, für das Y-Profil mit
`tests/run-y-homing-sim.sh .pio/build/rf2000_y_homing_test/firmware.elf`.
Die lokale Bedienung wird zusätzlich mit
`tests/run-panel-sim.sh .pio/build/rf2000_y_homing_test/firmware.elf` geprüft.
Für CNC einschließlich Display, XYZ-Homing und Programmausführung:
`tests/run-cnc-sim.sh .pio/build/rf2000_cnc/firmware.elf`.
