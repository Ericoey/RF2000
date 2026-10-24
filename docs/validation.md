# Prüfbericht

Stand: 24.10.2026. Dieser Bericht beschreibt die adaptive CNC-Firmware
`20261024-rf2000-cnc-adaptive-spi`. Die Cloud hat keine Platine programmiert
und keinen Motor bewegt. Die Messwerte stammen aus dem Simulator, der das
kompilierte AVR-Programm und dessen GPIO-/SPI-Signale ausführt. Der frühere
Anwenderbericht zu Display, Tasten, Y-Homing und Y-Kalibrierung ersetzt
keinen Hardwaretest der neuen Mikroschritt-Automatik.

## Builds und eindeutig zugeordnete Programme

| Variante | Werkzeugkette | Flash | Statisches SRAM | Ergebnis |
| --- | --- | ---: | ---: | --- |
| Adaptive XYZ-CNC mit LCD | PlatformIO / AVR-GCC 7.3.0 | 50.912 Byte | 3.893 Byte | gebaut und 58 AVR-Fälle bestanden |
| Adaptive XYZ-CNC mit LCD | Make / AVR-GCC 14.2.0 | 52.078 Byte | 3.893 Byte | gebaut und 28 AVR-Fälle bestanden |
| Standard-XYZ-Test ohne LCD | Make / AVR-GCC 14.2.0 | 35.226 Byte | 3.520 Byte | gebaut; `make check` bestanden |
| Separater Y-Test mit LCD | Make / AVR-GCC 14.2.0 | 38.448 Byte | 3.697 Byte | gebaut; HEX unverändert |
| Repetier-Referenz | historischer Build | 218.996 Byte | 6.914 Byte | unverändertes Artefakt, nicht erneut gebaut |

Ein zusätzlicher adaptiver CNC-Build ohne lokales Bedienfeld bestand:
44.474 Byte Flash und 3.708 Byte statisches SRAM. Er wird nicht als eigenes
Standardimage ausgeliefert. Statisches SRAM ist `.data + .bss`; die freie
Kapazität beweist keine maximale Stackreserve aller Betriebsarten.

Die beiden CNC-Werkzeugketten erzeugen unterschiedliche Programme. Ihre
Simulatorergebnisse wurden deshalb unmittelbar am jeweiligen ELF erhoben:

```text
PlatformIO ELF 92e5ecbd0ec07bed5d2c0f3c35d55088b1d6f654611b47e924df46d07a40fc63
PlatformIO HEX d477d766d5b8650f4125237629c57a0580d4afa16d063535ff0953a5b81def56
Make ELF       23cbf1c7958ebbe14c228fa98c3372cf92aca5dccd540ad968af3f5dd6cf56e4
```

PlatformIO verwendet `platformio/atmelavr@5.1.0`,
`toolchain-atmelavr@1.70300.191015`, ATmega2560 und 16 MHz. Der aktuelle
Registry-Zugriff und vollständige PlatformIO-Build bestanden. Die frühere
Registry-Sperre ist keine aktuelle Buildblockade mehr.

Artefakte: `artifacts/platformio-cnc/` bzw. `artifacts/cnc/`.
Buildprotokolle: `artifacts/platformio-cnc-build.log`,
`artifacts/cnc-build.log`. Die jeweiligen Buildmanifeste enthalten
Compiler, Sektionen sowie Quell- und Artefaktprüfsummen.

## Adaptive Bewegung und Fehlerbehandlung

Die adaptive Suite besteht aus **28 Fällen**. Sie wurde mit dem
PlatformIO-ELF und dem Make-ELF jeweils vollständig in einem Lauf
ausgeführt. Alle Läufe endeten mit **Exitstatus 0**. Der neue zusätzliche
Fall bildet die Anstiegszeit der offenen SPI-Leseleitung nach. Protokolle mit Hash und tatsächlichen Aufrufen:

- `artifacts/adaptive-platformio-simulation.log`
- `artifacts/adaptive-simulation.log`

Die Tests setzen Geschwindigkeiten über die öffentlichen UART-Befehle;
sie übergehen keine Einstellgrenzen durch RAM-Patches. Ihre Beschleunigung
von 1000 mm/s² ist ausschließlich eine digitale Belastungsprüfung und
keine Empfehlung für die Maschine. Die Fabrikvorgabe bleibt 10 mm/s².

| Prüfung | Nachweis am ausgeführten AVR-Programm |
| --- | --- |
| Start und Einstellungen | XYZ-Registerprofil 1/256, Ströme unverändert, A/B gesperrt; festes virtuelles Raster 20480 XYZ; Achs-/Homingwerte über 1000 abgewiesen; `$MS` und `$I` |
| Sechs Auflösungsstufen | Öffentliche Vorschübe F20/50/100/200/400/800/1000 erzeugen die vorgesehenen MODE-Stufen 256/128/64/32/16/8 |
| Positionsführung | Unabhängiges TI-Indexer-Modell mit 1024 Zuständen und Resetphase 128; erster Teil-Schritt nach nicht ausgerichtetem MODE-Wechsel; gewichtete reale STEP-Wege stimmen mit `sys_position` überein |
| Geometrie und Restwege | Ungerade Feinraster-Endpunkte, ungleiche XYZ-Anteile, Richtungswechsel, sehr kurze Zeilen sowie G2/G3; korrekte Endpositionen und Rückkehr |
| Referenzieren | X/Y/Z einzeln und Z vor gemeinsamem XY; je zwei Kontakte/Freigaben; Maskierung bereits betätigter Achsen; anschließender Fein-Jog nach Koordinatenneusetzen |
| Pause und Fortsetzen | Kontrollierter Stillstand, keine weiteren Pulse im Halt, Fortsetzung desselben Wegs zum exakten Endpunkt |
| Limits und Stop | Vier physische Hard Limits, Soft Limits, Ctrl-X und lokale Pause/Reset; A/B und abgeschaltete Lasten bleiben inaktiv |
| Umschaltfehler | 1-µs-FAULT, falsches Readback und Reset während MODE-SPI; keine STEP-Flanke während der Transaktion, keine Freigabe mit nur teilweise bestätigten Registern |
| ISR-Fehlerzeitpunkte | FAULT beim Timer1- und verzögerten Timer0-Eintritt unterdrückt den noch nicht ausgegebenen Puls einschließlich seiner Positionszählung |
| Freifahren | `$LR` bewegt im Alarm ausschließlich die gewählte Achse genau 2 mm mit 1/256; anschließend weiter Alarm, keine automatische Jobfortsetzung |
| Tatsächliches EEPROM | Gültiger Version-75-Datensatz migriert einmalig auf 76; G54, Startzeile, Buildtext und 100/100/150 bleiben erhalten; frische MCU erzeugt bei 1-mm-XYZ-Jogs je 20480 gewichtete Einheiten |

Der physische Indexer wird im Test aus SPI-Registern und GPIO-Flanken
berechnet, nicht aus den internen Firmwarezählern übernommen. Das bestätigt
die digitale Konsistenz. Motor-Schrittverluste oder echte mechanische
Bewegung werden damit nicht gemessen.

## Korrektur nach gemeldetem Alarm 12

Der Nutzer meldete am 24.10.2026 Alarm 12 beim Referenzstart. Die später
übermittelten Diagnosewerte zeigen einen erfolgreichen Starttest mit
`READY=1`, `FAULT=0`, `READBACK=0` und `MICRO=256`; sie dokumentieren keinen
verriegelten Laufzeitfehler dieser MCU-Sitzung. Der konkrete elektrische
Auslöser an der Maschine ist damit noch nicht abschließend gemessen.

Ein gezielter neuer Test verzögert ausschließlich nach dem Start die
freigegebene HIGH-Flanke des Open-Drain-SDATO-Signals um 1 µs. Der alte
offizielle PlatformIO-ELF aus Commit `929f552` reproduziert damit Alarm 12
beim ersten MODE32-Readback vor der Referenzbewegung. Der korrigierte
PlatformIO-ELF besteht denselben Test, einschließlich vollständigem
Homing und anschließender MODE8-Fahrt samt Rückkehr.

Der negative alte Lauf endet **erwartet mit Exitstatus 1**. Er wird getrennt
von den erfolgreichen Releaseprüfungen in
`artifacts/spi-readback-regression.log` dokumentiert. Dieses Protokoll
enthält ausdrücklich einen erwarteten Fehler, keinen fehlgeschlagenen
Prüflauf der neuen Firmware.

Die Korrektur liest CTRL bei Laufzeitwechseln über den langsameren
Starttransport zurück, statt mit dem schnellen Schreibtakt abzutasten.
Registervergleich, unbekannter Teilzustand, FAULT-Verriegelung und
Pulsbarriere bleiben aktiv. Hosttests injizieren weiterhin nach jedem der
sechs Frames Reset/Alarm/FAULT, nun über schnelles Schreiben und langsames
Lesen gemeinsam. Es gibt keine Abschaltung oder Umgehung der Fehlerprüfung.

Der Test belegt die zeitliche Schwachstelle im Modell. Die tatsächliche
Anstiegszeit und das Verhalten der Nutzerplatine nach dem Update müssen
noch am Gerät geprüft werden. Bedienung und Diagnose:
[diagnose-alarm12.md](diagnose-alarm12.md).

## Geschwindigkeit und Pulszeiten unter Last

Das LCD bleibt aktiv; die Tests fragen UART-Status mit 100 Hz ab.
Bei diagonaler XYZ-Fahrt ist F1000 der Bahnfeed. Die ideale Geschwindigkeit
jeder Achse beträgt dabei etwa 577,35 mm/min, nicht 1000.

| ELF / Bewegung | Angefordert | Gemessener Vorschub | Min. HIGH | Min. LOW |
| --- | ---: | ---: | ---: | ---: |
| PlatformIO, nur X, `$0=10` | F1000 | 993,026 mm/min | 14,438 µs | 59,375 µs |
| Make, nur X, `$0=10` | F1000 | 993,365 mm/min | 14,312 µs | 60,062 µs |
| PlatformIO, gleiche XYZ, `$0=10` | Bahn F1000 | 569,578 mm/min je Achse | 20,188 µs | 76,062 µs |
| Make, gleiche XYZ, `$0=10` | Bahn F1000 | 569,817 mm/min je Achse | 20,062 µs | 79,062 µs |
| PlatformIO, gleiche XYZ, `$0=20` | Bahn F1000 | 569,577 mm/min je Achse | 30,250 µs | 67,312 µs |
| Make, gleiche XYZ, `$0=20` | Bahn F1000 | 569,816 mm/min je Achse | 30,125 µs | 68,312 µs |

Die Zeitprüfung verlangt ausreichende Puls-HIGH-/LOW-Zeiten und
Richtungsvorlauf; kurze Interrupt-/Abschnittslatenzen können Pulse
verlängern und den tatsächlichen Vorschub etwas vermindern. MODE-Barrieren,
Rücklesen und Feinabschlüsse benötigen ebenfalls Zeit. Die Firmwaregrenze
1000 mm/min ist keine Zusage eines exakt konstanten realen Vorschubs.

Die Quantisierung führt nominal höchstens 31 virtuelle Einheiten Restweg
je Achse **an den Abschnitts-Endpunkten** mit, entsprechend etwa 1,514 µm
bei der vorgegebenen Mechanik. Das ist keine maximale geometrische
Abweichung der gesamten interpolierten Bahn. Zeitliche Verteilung,
DDA-Treppenform und Mechanik erfordern eine getrennte Genauigkeitsmessung.

## Bedienfeld, Freifahren und gestaffeltes Homing

Zusätzlich bestanden auf dem PlatformIO-ELF:

- **Acht Menügruppen**, `artifacts/menu-simulation.log`: elf reale
  GPIO-Tasten, LCD-Navigation, Freigaben, bestätigtes Arbeitsnullsetzen,
  alle sechs Achsrichtungen mit 2048 Fein-Pulsen je 0,1 mm bei F20,
  Referenzphasen, Pause/Fortsetzen, Jog-Abbruch, Override-Grenzen,
  Wake-Abbruch, gehaltene Stopptasten und verworfene alte Tastendrücke.
- **20 Freifahrfälle**, `artifacts/recovery-simulation.log`: alle vier
  Kontakte, je 40960 Fein-Pulse für 2 mm bei F20; Kontaktprellen,
  Wiederkontakt, fremder Schalter, dauerhaft aktiver Sensor, kurze
  Treiberfehler, Zählergrenzen sowie serielle und lokale Abbrüche.
- **Zwei gestaffelte XY-Fälle**, `artifacts/homing-wait-simulation.log`:
  zuerst Y bzw. X am Schalter; die erste Achse wartet 500 ms ohne weitere
  STEP-Flanke, während die andere sucht. Danach vollständiger Rückzug,
  Feinannäherung und letzter Rückzug; `$H` endet mit freien Schaltern.

Damit bestanden **58 Fälle auf demselben PlatformIO-ELF** und die adaptive
**28-Fälle-Suite auf dem separaten Make-ELF**. Zusätzliche
Wiederholungen einzelner Fälle werden nicht als neue Fälle gezählt.

Reproduktion:

```bash
make check-adaptive
make check-menu
make check-recovery
make check-homing-wait
tests/run-adaptive-sim.sh .pio/build/rf2000_cnc/firmware.elf
```

## Produktionscode auf dem Host und Standardprofil

`artifacts/adaptive-host-validation.log` enthält:

- **897036** Kombinationen von Phase, Richtung und Auflösung am tatsächlich
  eingebundenen Rechenheader, dazu Hysterese, Restwege, Verfeinerung,
  Richtungswechsel, Feinabschluss und Überlaufgrenzen mit ASAN/UBSAN.
- EEPROM-Versionen 74/75 → 76, proportionale Feinabgleiche,
  korrigierte alte 152,380-Werte, getrennte Datensätze und **264**
  Schreibunterbrechungen: je 88 vor Migration von 74, Migration von 75
  und erneutem Schreiben von 76. Keine doppelte Skalierung; bekannte
  beschädigte Datensätze setzen nur globale Einstellungen zurück.
  Das simuliert keine analogen Brownout-/Teilbytefehler.
- Tatsächliche Einstell- und nominale Planerfunktionen: 1000-mm/min-Grenze
  auch nach Overrides, bei Eilgang und bei Diagonalen; niedrigere
  Achsgrenzen bleiben wirksam.
- Tatsächlicher DRV8711-Treiber: alle Register, Wake/UVLO/Reset,
  Readbackfehler, alle sechs Laufzeitmodi und Abbruch nach jedem der
  sechs SPI-Frames einer XYZ-Umstellung; Teilzustand bleibt unbekannt,
  bis alle XYZ-Register erneut bestätigt sind.

`make check` bestand erneut am aktuellen Standard-ELF. Zusätzlich zu den
Hostprüfungen führt es die Standard-AVR-Suite sowie Tests der 16
Endschalterkombinationen, EEPROM-Zugriffe, Größenprüfung und seriellen
Ringpuffer aus. Protokoll: `artifacts/validation.log`.

Standard- und Y-HEX sind weiterhin bytegleich zu den Vorgängern:

```text
Standard fc34c27dd636276c3a94135dc3b27a7b2c52b1c2371deabda7e47d811b62178f
Y-Test   3e49a78a7ec6aab11e64dccdc216a7c64c55af04f9141e68e7e8b617ee9dceaa
```

Die historischen Y-Protokolle mit 13 Homing- und 14 Panel-Fällen behalten
ihren tatsächlich getesteten ELF-Hash. Diese Y-Suiten wurden für den
aktuellen Debug-Neubuild nicht erneut ausgeführt. Seine HEX-Datei ist
identisch; der neue ELF-Hash steht im Y-Buildmanifest.

Die früheren CNC-/18-kHz-Protokolle unter `cnc-simulation.log`,
`speed-simulation.log`, `speed-exploration.tsv` und
`speed-host-validation.log` dokumentieren die vorherige feste
1/4-Version. Sie gelten nicht als Prüfung des neuen adaptiven Ausgabepfads.

## gSender und Grenzen der Nachweise

Die tatsächliche `$I`-Antwort des adaptiven Make-ELF wurde durch die
unveränderte `Connection`-Erkennung von gSender 1.6.4 geleitet. Ergebnis:
**Grbl erkannt**, auch ohne Startbanner und trotz grblHAL-Standardtyp;
die negative Gegenprobe reproduziert den alten Rückfall auf grblHAL.
Quell- und Transkripthash: `artifacts/gsender-detection.log`.

Es lief keine gSender-Desktopanwendung an einer realen Maschine.
Elektrische Signalqualität, schnelle SPI-Flanken an der Platine,
Wicklungsstrom, Motorlast, Resonanzen, mechanische Richtungen,
Anhaltewege, Endschalterverdrahtung und Fräsgenauigkeit bleiben am Gerät
zu prüfen. Die Spindel wird manuell bedient; M5 oder Reset schalten sie
nicht physisch aus. Der Ablauf steht in
[adaptive-mikroschritte.md](adaptive-mikroschritte.md) und
[hardware-test.md](hardware-test.md).
