# RF2000v2: GRBL-Mega-5X für I851 CTC V3.0

XYZ-CNC-Firmware für ATmega2560/16 MHz und fünf DRV8711. PlatformIO wählt
standardmäßig **`rf2000_cnc`**: gSender-Jogging, XYZ-Homing, G-Code-Fräsen,
Arbeitsnullpunkte und das originale 20×4-Display mit Tastenfeld. A/B bleiben
gesperrt. **Die Spindel wird manuell bedient**; M3/M4/M5 und S-Werte schalten
keinen Ausgang. Anleitung: [CNC und gSender](docs/cnc-gsender.md).

Y einschließlich der bisherigen Kalibrierung mit 2560 Schritten/mm bei
1/32 wurde vom Nutzer am Gerät bestätigt. XYZ-Homing ist nach dessen
ausdrücklicher Vorgabe mit angenommenen X/Y-Min- und Z-Max-Richtungen
freigegeben; X/Z sind noch nicht physisch geprüft. Der getrennte Y-Test
und das bisherige XYZ-Testprofil bleiben verfügbar.

Das CNC-Profil verwendet die vom Nutzer vorgegebene Fräsmechanik auf allen
drei Achsen: 200 Vollschritte/Umdrehung, SFU1605 mit 5 mm Steigung und
2:1 Motor-zu-Spindel-Übersetzung. Mit festem **X/Y 1/4 und Z 1/16** ergeben
sich **320 / 320 / 1280 Schritte/mm**. Die Factory-Vorgaben sind
**600 / 600 / 300 mm/min** bei 10 mm/s². Gültige alte CNC-Einstellungen
werden migriert; ihre bisherigen 100 / 100 / 150 mm/min werden dabei nicht
automatisch angehoben. Die CNC-Schrittfrequenzgrenze beträgt **18 kHz**.
Die getrennten Testprofile behalten ihre
bisherige Repetier-Kalibrierung. X und Z übernehmen dieselbe Übersetzung;
deren tatsächlichen Fahrweg hat der Nutzer noch nicht durch Messung bestätigt.
Analyse, Grenzen und stufenweiser Hardwaretest:
[Geschwindigkeit optimieren](docs/geschwindigkeit-optimieren.md).

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
öffnen und die Umgebung **`rf2000_cnc` → Build** auswählen. Per CLI:

```bash
git pull
pio run
```

Das HEX liegt anschließend unter `.pio/build/rf2000_cnc/firmware.hex`.
Die vollständige Anleitung steht in [docs/platformio.md](docs/platformio.md).

Zum Aufspielen gSender und seriellen Monitor vorher schließen und
den tatsächlichen USB-Port angeben, beispielsweise auf dem Mac:

```bash
pio run -t upload --upload-port /dev/cu.usbserial-AL03P2K9
pio device monitor --port /dev/cu.usbserial-AL03P2K9
```

Die Startmeldung muss **`RF2000 XYZ CNC`** enthalten. In gSender **GRBL,
115200 Baud** verbinden. `$I` enthält `20261022-rf2000-cnc-speed`.
**Vor der ersten Bewegung die [EEPROM-Migration prüfen](docs/cnc-gsender.md#bestehende-cnc-einstellungen-umstellen).**
Version 74 wird auf 75 umgestellt: X/Y-Schritte/mm werden durch acht geteilt,
Z bleibt unverändert. G54–G59, Startzeilen und eigener Buildtext bleiben bei
dieser Migration gespeichert. **Nach dem Update niemals die alten X/Y-Werte
2560 zurückschreiben:** Bei 1/4 würden damit achtfach zu große Wege entstehen.
Mit `$$` die neue Kalibrierung prüfen, dann langsam neu referenzieren und
Fahrweg sowie Werkstücknull kontrollieren.

Home / `$H` referenziert Z-Max, danach X/Y-Min;
`$HX`, `$HY`, `$HZ` funktionieren einzeln. XYZ-Jogs und normale GRBL-Dateien
werden angenommen. Das Display zeigt XYZ-Arbeitspositionen, G54–G59,
Referenzstatus, Zustand und Vorschub sowie die gerade bewegten Achsen.
Beim Referenzieren zeigt es die konkrete Achse und Phase: zuerst Z-Max,
danach gemeinsam X/Y-Min. Arbeitsnull am Werkstück setzen und Spindel vor
dem Job manuell starten. [Vollständiger Ablauf](docs/cnc-gsender.md).

**OK/Rechts** öffnet das Hauptmenü mit Freigeben, Verfahren XYZ,
Referenzieren, Arbeitsnull, Diagnose, Endschalter frei, Jogtempo und
Abbruch/Reset. Referenzieren und Arbeitsnull erlauben alle oder einzelne
Achsen. Im Menü dient das Steuerkreuz der Navigation; beim Verfahren fährt
es X/Y, die oberen rechten Tasten fahren Z+/Z− und die mittleren −/+ ändern
die Schrittweite. **Pause II** hält normale Fahrten an und bricht Jogfahrten
ab; **Play** setzt nur eine vollständig angehaltene normale Fahrt fort.
Während Referenzieren/Freifahren sowie bei Stillstand/Alarm führt Pause
sofort einen Reset aus. Eine Sekunde Pause halten oder Play+Pause setzt
ebenfalls zurück. [Bedienfeld und Menü im Detail](docs/bedienfeld-menue.md).

Nach versehentlichem Anfahren eines Endschalters stoppt der Hard Limit den
Job. Im CNC-Profil lässt sich die betroffene Achse anschließend gezielt
freifahren: zuerst **Reset**, dann **`$LR`** in gSender senden, ohne vorheriges
`$X`. Genau ein Schalter muss aktiv sein. Der Rückzug erfolgt vom Schalter weg,
mit 20 mm/min und höchstens 2 mm; danach bleibt die Steuerung verriegelt.
Anschließend neu referenzieren und Werkstücknull prüfen. Am Display löst
**Endschalter frei** nach zwei getrennten OK-Betätigungen dieselbe Funktion aus.
Es gibt kein selbsttätiges Zurückfahren nach einem Anschlag, weil der
Maschinenbezug durch den abrupten Stopp unsicher sein kann.
[Ablauf und Abbruchbedingungen](docs/cnc-gsender.md#nach-einem-hard-limit-freifahren).

Weitere Profile ausdrücklich wählen: `-e rf2000_y_homing_test` für den
isolierten Y-Test mit endloser erster Suche und `-e rf2000_grbl` für den
XYZ-Test ohne LCD und ohne Homing. Die Profilwahl im Monitor programmiert
keine Firmware. Die Repetier-Referenz bleibt unverändert.

Bestehender Make-Build einschließlich Repetier-Referenz:

```bash
cd /workspace/RF2000
tools/install-toolchain.sh
make cnc
make check-cnc
make check
make repetier
```

HEX/ELF/MAP CNC: `artifacts/cnc/rf2000-grbl.*`.
Das unveränderte Make-Standardziel baut den XYZ-Test unter `artifacts/grbl/`.
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
- CNC: feste Mikroschritte X/Y 1/4, Z 1/16; Testprofile X/Y 1/32. Reserve A/B 1/32; TORQUE 110/110/105/90/90 bei ISGAIN-Verstärkung 20.
- CNC: 18-kHz-Grenze, unveränderte Richtung wird zwischengespeichert; erster STEP nach Richtungswechsel behält mindestens 5 µs Vorlauf, Pulsende bleibt timergeführt.
- LOW-aktive X-/Y-/Z-Min und Z-Max auf Port C/H, Timer2-Abfrage mit 1 kHz, sofortige Stopentscheidung und fünf freie Samples vor Wiederfreigabe.
- CNC: ausdrücklich angefordertes `$LR` zum begrenzten Freifahren eines einzelnen aktiven Endschalters; danach erneutes Referenzieren erforderlich.
- Treiberfehler über INT6 auch bei kurzen FAULTn-Pulsen verriegelt bis zum echten MCU-Neustart; Boot und Softreset starten verriegelt, Freigabe über Homing oder `$X` bei freien Eingängen.
- Heizungen, P230, Lüfter-/RGB-/sonstige Leistungsfunktionen aus; Probe und physische GRBL-Kontrolltasten unzugeordnet.
- G38.x, M7/M8 und digitale/analoge Ausgangskommandos zurückgewiesen. CNC nimmt M3/M4/M5 nur als manuelle Spindel-Programmmodalität an; es schaltet keine Spindel.

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
- [XYZ-CNC mit gSender und Display](docs/cnc-gsender.md)
- [CNC-Bedienfeld und Menü](docs/bedienfeld-menue.md)
- [Befehlsübersicht für gSender: Referenzieren, Joggen, Nullpunkte und Einstellungen](docs/befehle.md)
- [Geschwindigkeitsanalyse, EEPROM-Umstellung und Hardwaretestplan](docs/geschwindigkeit-optimieren.md)
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
