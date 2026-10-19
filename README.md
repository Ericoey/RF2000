# RF2000v2: GRBL-Mega-5X für I851 CTC V3.0

XYZ-CNC-Firmware für ATmega2560/16 MHz und fünf DRV8711. PlatformIO wählt
standardmäßig **`rf2000_cnc`**: gSender-Jogging, XYZ-Homing, G-Code-Fräsen,
Arbeitsnullpunkte und das originale 20×4-Display mit Tastenfeld. A/B bleiben
gesperrt. **Die Spindel wird manuell bedient**; M3/M4/M5 und S-Werte schalten
keinen Ausgang. Anleitung: [CNC und gSender](docs/cnc-gsender.md).

Y wurde vom Nutzer am Gerät bestätigt. XYZ-Homing ist nach dessen neuer
ausdrücklicher Vorgabe mit angenommenen X/Y-Min- und Z-Max-Richtungen
freigegeben; X/Z sind noch nicht physisch geprüft. Der getrennte Y-Test
und das bisherige XYZ-Testprofil bleiben verfügbar.

Das CNC-Profil verwendet die vom Nutzer angenommene Fräsmechanik auf allen
drei Achsen: 200 Vollschritte/Umdrehung, SFU1605 mit 5 mm Steigung und
2:1 Motor-zu-Spindel-Übersetzung. Mit X/Y 1/32 und Z 1/16 ergeben sich
**2560 / 2560 / 1280 Schritte/mm**. Die CNC-Maximalgeschwindigkeiten sind
**100 / 100 / 150 mm/min**. Die getrennten Testprofile behalten ihre
bisherige Repetier-Kalibrierung.

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
115200 Baud** verbinden. `$I` enthält `20261019-rf2000-cnc-sfu1605`.
**Bei bestehender CNC-Firmware bleiben die bisherigen EEPROM-Werte erhalten.**
Vor der ersten Bewegung die [geordneten Einstellbefehle](docs/cnc-gsender.md#bestehende-cnc-einstellungen-umstellen)
anwenden: zuerst X/Y-Maximaltempo senken, dann Schritte/mm ändern. Dafür
ist kein EEPROM-Reset nötig; Arbeitskoordinaten bleiben gespeichert.
Anschließend Referenz und Werkstücknull neu prüfen.

Home / `$H` referenziert Z-Max, danach X/Y-Min;
`$HX`, `$HY`, `$HZ` funktionieren einzeln. XYZ-Jogs und normale GRBL-Dateien
werden angenommen. Das Display zeigt XYZ-Arbeitspositionen, G54–G59,
Referenzstatus, Zustand und Vorschub. Arbeitsnull am Werkstück setzen und
Spindel vor dem Job manuell starten. [Vollständiger Ablauf](docs/cnc-gsender.md).

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
- Mikroschritte X/Y 1/32, Z 1/16, Reserve A/B 1/32; TORQUE 110/110/105/90/90 bei ISGAIN 20.
- LOW-aktive X-/Y-/Z-Min und Z-Max auf Port C/H, Timer2-Abfrage mit 1 kHz, sofortige Stopentscheidung und fünf freie Samples vor Wiederfreigabe.
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
