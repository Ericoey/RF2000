# GRBL mit PlatformIO kompilieren

Die Datei `platformio.ini` im Repository-Hauptverzeichnis definiert die
Standardumgebung **`rf2000_y_homing_test`** mit originalem Display und Tastenfeld
und die ausdrücklich wählbare XYZ-Umgebung **`rf2000_grbl`** für ATmega2560
bei 16 MHz. Im XYZ-Profil sind Display und Homing deaktiviert. Die RF2000-Pinbelegung
und sämtliche Sicherheitsparameter stammen weiterhin aus dem GRBL-Quellcode.

## VS Code

1. Die Erweiterung **PlatformIO IDE** von PlatformIO installieren.
2. Das Repository aktualisieren (`git pull`) oder
   `https://github.com/Ericoey/RF2000` klonen.
3. In PlatformIO **Open Project** wählen und den Ordner `RF2000` öffnen,
   in dem `platformio.ini` liegt.
4. Unter **Project Tasks → rf2000_y_homing_test → General → Build** kompilieren.
   Alternativ das Häkchen **Build** in der unteren Statusleiste verwenden;
   bei einer zuvor ausdrücklich gewählten Umgebung dort ebenfalls
   `rf2000_y_homing_test` auswählen.

Beim ersten Build lädt PlatformIO die festgelegte AVR-Plattform und
Werkzeugkette herunter. Für das Kompilieren ist keine angeschlossene Platine
erforderlich. Die normale Build-Aktion überträgt keine Firmware.

## Kommandozeile

Mit einer vorhandenen PlatformIO-Installation im Projektordner:

```bash
git pull
pio run
```

`pio run` ohne `-e` baut das Y-Testprofil mit LCD. Ein sauberer Neubuild:

```bash
pio run -t clean
pio run
```

Die explizite Auswahl desselben Y-Profils ist weiterhin möglich:

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
.pio/build/rf2000_y_homing_test/firmware.hex
.pio/build/rf2000_y_homing_test/firmware.elf
.pio/build/rf2000_y_homing_test/firmware.map
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
Der Upload verwendet ohne `-e` das Y-Testprofil. Nach einem Neustart muss die
Startmeldung **`RF2000 Y-only test`** enthalten. **`RF2000 XYZ test profile`**
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

A/B bleiben deaktiviert und Homing bleibt im **XYZ-Profil `rf2000_grbl`** gesperrt.
Das standardmäßig ausgewählte Y-Profil hält X/Z/A/B abgeschaltet und erlaubt ausschließlich
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
Die Konfiguration beider Umgebungen einschließlich geerbter Buildflags wurde
erfolgreich eingelesen. Der Make-Build mit AVR-GCC 14.2.0 ist davon getrennt.
Wenn der Registry-Zugriff verfügbar ist, die gewünschte PlatformIO-Umgebung
bauen und deren tatsächliches ELF prüfen: für das XYZ-Profil mit
`tests/run-sim.sh .pio/build/rf2000_grbl/firmware.elf`, für das Y-Profil mit
`tests/run-y-homing-sim.sh .pio/build/rf2000_y_homing_test/firmware.elf`.
Die lokale Bedienung wird zusätzlich mit
`tests/run-panel-sim.sh .pio/build/rf2000_y_homing_test/firmware.elf` geprüft.
