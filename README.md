# RF2000v2: GRBL-Mega-5X für I851 CTC V3.0

Kompilierbarer CNC-Teststand für ATmega2560/16 MHz mit drei logischen Achsen
X/Y/Z und fünf konfigurierten DRV8711. A/B bleiben doppelt gesperrt. Homing
bleibt bis zur Prüfung von Schalterpolarität und Bewegungsrichtungen gesperrt.

Die Grundlage ist das hochgeladene GRBL-Mega-5X-Archiv. Seine Quelldateien
entsprechen dem offiziellen `edge`-Commit
`baaa1be3fe6c33a287695bbd57121da0b2979d9c` von
[fra589/grbl-Mega-5X](https://github.com/fra589/grbl-Mega-5X).
Die hochgeladene Repetier-Referenz ist **Community Mod 1.45.01** und keine
nachgewiesene Hersteller-Stockfirmware. Sie liegt unverändert unter
`references/repetier/` und wird als zweite Firmwarevariante separat gebaut.
Quellen und Prüfsummen stehen in `references/provenance.json`.

## Bauen und prüfen

Für **PlatformIO / VS Code** den Repository-Hauptordner mit `platformio.ini`
öffnen und die Umgebung **`rf2000_grbl` → Build** auswählen. Per CLI:

```bash
pio run -e rf2000_grbl
```

Das HEX liegt anschließend unter `.pio/build/rf2000_grbl/firmware.hex`.
Die vollständige Anleitung steht in [docs/platformio.md](docs/platformio.md).

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
Die Buildwerkzeuge führen keine Programmierung und keine Fuse-Änderung aus.
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
Auftragsliste sind hierfür falsch. PA7/WDI/OUT3 und PC0/ES2 weisen außerdem
Unterschiede zwischen Schaltplan und Community-Firmware auf. Diese
Revisionsfragen müssen am realen Board geklärt werden.

## Bedienung des Testprofils

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
[docs/validation.md](docs/validation.md). Kein physischer Hardwaretest wurde
durchgeführt. Elektrische Revision, reale Motorströme, Bewegungsrichtungen,
Schalterverkabelung und Stopplatenz bleiben am Gerät zu messen.

- [Verifizierte Hardwarebelegung und Quellenkonflikte](docs/hardware-verification.md)
- [DRV8711-Protokoll, Register, Stromberechnung](docs/drv8711.md)
- [STEP-/DIR-Timer und Grenzen](docs/step-timing.md)
- [Endschalter und Diagnose](docs/limits.md)
- [Kontrollierter Hardwaretest](docs/hardware-test.md)
- [Entwicklungsumgebung](docs/development.md)
- [PlatformIO / VS Code](docs/platformio.md)
- [Repetier-Referenzbuild](docs/repetier-build.md)

Lizenz: GPL-3.0-or-later, siehe `COPYING`; Lizenzhinweise der übernommenen
Dateien und der Repetier-Referenz bleiben erhalten.
