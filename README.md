# RF2000v2: GRBL-Mega-5X für I851 CTC V3.0

Kompilierbarer CNC-Teststand für ATmega2560/16 MHz mit drei logischen Achsen
X/Y/Z und fünf konfigurierten DRV8711. A/B bleiben doppelt gesperrt. PlatformIO
baut standardmäßig das Y-Testprofil mit Display und Tasten. Dieses erlaubt
nach Prüfung von Schalterpolarität und Bewegungsrichtung ausschließlich
Y-Jogging mit Weglimit sowie Y-Homing mit fortlaufender erster Referenzsuche.
Das Y-Profil bietet dafür ein einfaches Menü auf dem originalen 20×4-LCD
und dem vorhandenen Tastenfeld: Freigeben, Y verfahren, Y referenzieren und
Diagnose. Die Verfahrseite nutzt Hoch/Runter für Y+/Y−; OK und die bisherigen
Z-/Extrudertasten stellen Schrittweite und Vorschub ein.
[Bedienung am Gerät](docs/local-panel.md).

Die Grundlage ist das hochgeladene GRBL-Mega-5X-Archiv. Seine Quelldateien
entsprechen dem offiziellen `edge`-Commit
`baaa1be3fe6c33a287695bbd57121da0b2979d9c` von
[fra589/grbl-Mega-5X](https://github.com/fra589/grbl-Mega-5X).
Die hochgeladene Repetier-Referenz ist **Community Mod 1.45.01** und keine
nachgewiesene Hersteller-Stockfirmware. Sie liegt unverändert unter
`references/repetier/` und wird als zweite Firmwarevariante separat gebaut.
Quellen und Prüfsummen stehen in `references/provenance.json`.

**Quellenpriorität bei Abweichungen:** Nach aktueller Nutzervorgabe gilt die
bisherige RF2000-Repetier-Konfiguration als funktionierend. Widerspricht eine
Hardwarezuordnung im Schaltplan der bereitgestellten Repetier-Referenz, hat
deren RF2000v2-Konfigurationsblock Vorrang. Jede übernommene Abweichung wird
dem Nutzer genannt und in [docs/hardware-verification.md](docs/hardware-verification.md)
dokumentiert. DRV8711-Protokoll und Registersemantik folgen dem TI-Datenblatt;
die Festlegung ersetzt keinen Funktionstest des GRBL-Ports.

## Bauen und prüfen

Für **PlatformIO / VS Code** den Repository-Hauptordner mit `platformio.ini`
öffnen und die Umgebung **`rf2000_y_homing_test` → Build** auswählen. Per CLI:

```bash
git pull
pio run
```

Das HEX liegt anschließend unter `.pio/build/rf2000_y_homing_test/firmware.hex`.
Die vollständige Anleitung steht in [docs/platformio.md](docs/platformio.md).

Zum Aufspielen des Y-Testprofils den seriellen Monitor vorher schließen und
den tatsächlichen USB-Port angeben, beispielsweise auf dem Mac:

```bash
pio run -t upload --upload-port /dev/cu.usbserial-AL03P2K9
pio device monitor --port /dev/cu.usbserial-AL03P2K9
```

Die Startmeldung muss **`RF2000 Y-only test`** enthalten. Der Monitor verwendet
115200 Baud und LF; Befehle jeweils mit Enter senden. Dieses Profil hält
X/Z/A/B abgeschaltet, begrenzt Y-Jogs auf 0,5 mm bei höchstens 30 mm/min und
erlaubt nur `$HY` nach `$22=1` in derselben Sitzung. Die erste Y-Referenzsuche
läuft bis zum Schalterkontakt ohne Gesamtweg- oder Zeitbegrenzung; fehlt der
Kontakt, stoppt sie nicht automatisch. Ctrl-X sendet den Software-Abbruch.
Normale G-Code-Bewegungen
und andere Homing-Kommandos bleiben gesperrt. Richtung und Schalter zuerst
mit dem [Y-Testablauf](docs/y-axis-test.md) prüfen, bevor `$HY` gesendet wird.
Das XYZ-Profil `rf2000_grbl` bleibt mit `pio run -e rf2000_grbl` ausdrücklich
wählbar; darin sind Display und Homing deaktiviert. Die Profilwahl im Monitor
ändert keine aufgespielte Firmware. Die Repetier-Referenz bleibt unverändert.

Bestehender Make-Build einschließlich Repetier-Referenz:

```bash
cd /workspace/RF2000
tools/install-toolchain.sh
make -j4
make check
make repetier
```

HEX/ELF/MAP: `artifacts/grbl/rf2000-grbl.*`.
Repetier-Referenz: `artifacts/repetier/`.
Für das Y-Profil im Make-Build: `make y-homing-test`, anschließend optional
`make check-y-homing`; Dateien unter `artifacts/y-homing/rf2000-grbl.*`.
`make check-panel` prüft zusätzlich Display, Tasten und lokale Y-Bedienung.
Die Make-Buildziele führen keine Programmierung und keine Fuse-Änderung aus.
Die Repetier-Referenz enthält weiterhin ihre ursprünglichen Drucker-/Heizfunktionen.

## Implementierter Teststand

- Eigene Plattform `CPU_MAP_2560_RENKFORCE_CTC_V3`, ohne RAMPS-Ausgänge.
- Portmaskierte gemeinsame STEP-Ausgabe auf PF0/PF6/PF3; DIR auf PF1/PF7/PL1.
- Fünfkanaliges HIGH-aktives Software-SPI mit kontrollierter ENBL0-Wake-/Resetsequenz und verpflichtendem Registerreadback.
- Mikroschritte X/Y 1/32, Z 1/16, Reserve A/B 1/32; TORQUE 110/110/105/90/90 bei ISGAIN 20.
- LOW-aktive X-/Y-/Z-Min und Z-Max auf Port C/H, Timer2-Abfrage mit 1 kHz, sofortige Stopentscheidung und fünf freie Samples vor Wiederfreigabe.
- Treiberfehler über INT6 auch bei kurzen FAULTn-Pulsen verriegelt bis zum echten MCU-Neustart; Boot und Softreset erfordern manuelles `$X` bei freien Eingängen.
- Heizungen, P230, Lüfter-/RGB-/sonstige Leistungsfunktionen aus; Probe und physische GRBL-Kontrolltasten unzugeordnet.
- G38.x, M3/M4, M7/M8 und digitale/analoge Ausgangskommandos zurückgewiesen. M5/M9 bleiben sichere Ausschaltkommandos.

**Korrigierte Reservepins:** DRV4 hat STEP=PA4 und DIR=PA6. PA5/PA7 aus der
Auftragsliste sind hierfür falsch. Nach der Repetier-Konfiguration ist
PA7/D29 der Watchdog-Pin, PC0/D37 der Y-Min-Eingang und PC4 der OUT3-Ausgang.
PA7 bleibt Eingang ohne Pullup, entsprechend Repetiers `HAL::stopWatchdog()`;
PC4 bleibt LOW. Der Testbuild erzeugt keine Watchdog-Lebenszeichen.

## Bedienung des XYZ-Profils `rf2000_grbl`

UART0: 115200 Baud, 8N1. `$DRV` liest nur den zwischengespeicherten
Initialisierungszustand und die vier Limitpegel, ohne SPI oder Motorfreigabe.
`?`, `$$` und `$I` sind die üblichen GRBL-Abfragen.

| Einstellung | Testprofil |
| --- | --- |
| Schritte/mm X/Y/Z | 152,38 / 152,38 / 1280 |
| Maximalgeschwindigkeit X/Y/Z | 500 / 500 / 150 mm/min |
| Beschleunigung | 10 mm/s² |
| Pulsbreite `$0` | 10 µs, zulässig 10–20 µs |
| Richtungsvorlauf | mindestens 5 µs softwareseitiger Timerabstand |
| Schrittfrequenzgrenze | 5000 Hz, Feed-Override höchstens 100 % |
| `$2`, `$4`, `$5`, `$32` | fest 0 |
| `$21` | fest 1; physische Limitüberwachung ist immer aktiv |
| `$22`, Homing | 0, `$H` im unverifizierten Build gesperrt |

`ALARM:1` bedeutet Hard Limit, `ALARM:3` Bewegungsabbruch bzw. erkannte
Pulsüberlappung, `ALARM:11` seriellen Empfangsüberlauf und `ALARM:12`
Treiber-/Readbackfehler. Nach einem kritischen Ereignis ist zunächst ein
GRBL-Reset nötig. `$X` übergeht weder aktive Schalter noch einen
Treiberfehler. Die Position kann nach Abschaltung verloren sein; insbesondere
Z muss gegen Absinken gesichert werden.

## Nachweise und Hardwaretest

Die Softwareprüfungen und Buildnachweise stehen in
[docs/validation.md](docs/validation.md). Am 19.10.2026 hat der Nutzer das
funktionierende Display, alle Tasten, Y-Referenzfahrt und Referenzschalter
mit dem bisherigen Testmenü am Gerät bestätigt. Die Cloud hat diese
Hardwareprüfungen nicht selbst durchgeführt. Die neue Menüführung ist
separat am Gerät zu prüfen; reale Motorströme und Stopplatenz bleiben zu messen.

- [Verifizierte Hardwarebelegung und Quellenkonflikte](docs/hardware-verification.md)
- [DRV8711-Protokoll, Register, Stromberechnung](docs/drv8711.md)
- [STEP-/DIR-Timer und Grenzen](docs/step-timing.md)
- [Endschalter und Diagnose](docs/limits.md)
- [Kontrollierter Hardwaretest](docs/hardware-test.md)
- [Separater Y-Achs- und Homingtest](docs/y-axis-test.md)
- [Originaldisplay und Tasten für den Y-Test](docs/local-panel.md)
- [Entwicklungsumgebung](docs/development.md)
- [PlatformIO / VS Code](docs/platformio.md)
- [Repetier-Referenzbuild](docs/repetier-build.md)

Lizenz: GPL-3.0-or-later, siehe `COPYING`; Lizenzhinweise der übernommenen
Dateien und der Repetier-Referenz bleiben erhalten.
