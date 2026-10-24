# Hardwareabgleich RF2000v2 / I851 CTC V3.0

Stand: 19.10.2026. Dies ist ein Quellenabgleich und ein Prüfprotokoll für die Inbetriebnahme. Es wurde keine reale Platine angeschlossen, keine Spannung gemessen und keine Achse bewegt. Ein erfolgreicher AVR-Build bestätigt weder die tatsächliche Platinenrevision noch Strom, Drehrichtung oder Störfestigkeit.

## Verbindliche Quellenpriorität und ergänzende Quellen

Nach der jüngsten ausdrücklichen Vorgabe des Nutzers wird die Konfiguration der bereitgestellten alten RF2000-Repetier-Firmware als funktionierend angenommen. **Bei widersprüchlichen Hardwarezuordnungen hat der RF2000v2-Block dieser Firmware Vorrang vor `references/rf2000-schematic.pdf`.** Der Schaltplan bleibt die ergänzende Quelle für Beschaltung und Bauteilwerte. Diese Vorgabe ersetzt den zuvor dokumentierten absoluten Schaltplanvorrang. DRV8711-Protokoll und Registersemantik folgen weiterhin dem TI-Datenblatt; die daraus begründeten Anpassungen für GRBL bleiben bestehen. Die Arbeitsannahme ist kein durchgeführter Hardwaretest. Weitere Zuordnungsänderungen werden dem Nutzer mit bisheriger und neuer Zuordnung ausdrücklich mitgeteilt.

1. Bereitgestellter Schaltplan `schema-electrique-1395717-renkforce-rf2000-imprimante-3d-appareil-pret-a-lemploi-banc-dimpression-chauffant.pdf`, sechs Blätter, Projekt **I851 CTC V3.0**, Rev. 1, Datum 23.10.2015. Alle sechs Blätter wurden gerendert und visuell gelesen; die Netzbezeichnungen wurden zusätzlich mit der Textextraktion abgeglichen. PDF-SHA256: `9d4f87760933a603c669d5fe4381cdf342b47a1c18273c9e94a336061eac288c`.
2. Bereitgestelltes Archiv `Repetier-Firmware-community_stable Original.zip`. Der Dateiname enthält „Original“; die enthaltene `README.de_DE.md` bezeichnet den Stand ausdrücklich als **inoffizielle Community-Modifikation**, RF 1.45.Mod. Damit ist keine unveränderte Conrad-Werksfirmware nachgewiesen. Archiv-SHA256: `9303140634c4a542d79d633c66e1d1689473444bdf560bda8706abfd29ed3181`.
3. Im Archiv sind `Repetier/pins.h`, der Block `MOTHERBOARD == DEVICE_TYPE_RF2000v2` ab Zeile 531, `RF2000v2.h`, `Configuration.h`, `RF.cpp`, `Printer.cpp`, `Printer.h` und `HAL.cpp` die maßgeblichen Softwarebelege. `Configuration.h` enthält ausdrücklich eine `#error`-Sperre mit dem Hinweis, dass die Modifikation auf RF2000v2 noch nicht umfassend getestet wurde. Die Referenzdateien bleiben unverändert.
4. Texas Instruments, **DRV8711**, offizielles Datenblatt **SLVSC40H, Rev. H, Mai 2020**, 51 Seiten: <https://www.ti.com/lit/ds/symlink/drv8711.pdf>. PDF-SHA256: `d04e2538bc6e1bea359037ce1e8ab39a36e8100197319951d4f3d19884a60843`. Abgeglichen wurden insbesondere Pin Functions, §6.6/6.7, §7.3.11, §7.4.1 und §7.5/7.6.
5. Bereitgestellter upstream-Stand `grbl-Mega-5X-edge.zip`, Projekt <https://github.com/fra589/grbl-Mega-5X>. Ein separater früherer GRBL-Renkforce-Port wurde nicht mitgeliefert.

Die Zielhardware ist für diesen Auftrag die I851 CTC V3.0 des RF2000v2. Maßgeblich ist der RF2000v2-Konfigurationsblock; andere Geräteblöcke derselben Repetier-Datei dürfen nicht übernommen werden. Die Nutzervorgabe zur funktionierenden Konfiguration ändert nicht die dokumentierte Herkunft als Community-Modifikation.

## Motorzuordnung

CPU-Netze auf Blatt 1, die jeweiligen DRV8711-Eingänge auf Blatt 3–5 und der RF2000v2-Block von `pins.h` stimmen für folgende Belegung überein. Arduino-Nummern oberhalb des Standard-Mega-Pinbereichs sind Repetier-Erweiterungen; der GRBL-Port verwendet AVR-Portregister.

| DRV | Aktuelle Verwendung | STEP | DIR | SLEEPn / Freigabe | SCS | STALLn |
|---|---|---|---|---|---|---|
| 1 | X | PF0 / D54 | PF1 / D55 | PD7 / D38 | PL0 / D49 | PG0 |
| 2 | Y | PF6 / D60 | PF7 / D61 | PF2 / D56 | PL2 / D47 | PG1 |
| 3 | Z | PF3 / D57 | PL1 / D48 | PK0 / D62 | PB6 / D12 | PG2 |
| 4 | A, Reserve | **PA4 / D26** | **PA6 / D28** | PA2 / D24 | PL6 / D43 | PG3 |
| 5 | B, Reserve | PC1 / D36 | PC3 / D34 | PC7 / D30 | PJ7 | PG4 |

**Korrektur zum Auftrag:** DRV4 liegt auf PA4/PA6. PA5 steuert `OUT2`; PA7/D29 ist im maßgeblichen RF2000v2-Block `WATCHDOG_PIN`. PA5/PA7 als A-STEP/DIR würden somit einen Leistungsausgang beziehungsweise den Watchdog-Anschluss ansteuern. Z-STEP ist bei RF2000/v2 **PF3**, während der RF1000-Block PL3 verwendet; die Blöcke dürfen nicht vermischt werden.

`ENA_DRVx` führt ohne Inverter direkt auf **SLEEPn**, Pin 8 des DRV8711. LOW bedeutet Sleep, HIGH bedeutet wach. Die Repetier-Definitionen `X/Y/Z_ENABLE_ON 1` bestätigen die aktive HIGH-Freigabe. Das zusätzliche CTRL-Registerbit ENBL ist unabhängig davon. Reservekanäle werden konfiguriert, bleiben anschließend im Sleep und dürfen keine STEP-Flanken erhalten.

Für den aktuellen Port werden **drei GRBL-Achsen** konfiguriert. Das verhindert, dass A/B über G-Code, Jogging, Homing oder zusätzliche Planerachsen erreichbar sind. Alle fünf Kanäle bleiben im Hardwaremodul abgebildet. Eine spätere Fünfachsenversion benötigt eine bewusste Änderung von Achszahl, Freigabemaske, Planer-/EEPROM-Parametern, Homing und Tests; sie ist keine Laufzeitoption dieses Prüfstands.

## Software-SPI, Reset und Fehler

| Signal | AVR | Bedeutung / belegter Ruhepegel |
|---|---|---|
| SCLK | PD5 | Software-Takt; Ruhe LOW |
| SDATI | PD6 | MCU → Treiber; MSB zuerst, 16 Bit pro Transaktion |
| SDATO | PD4 | Treiber → MCU; Eingang mit Pullup; nur ein Treiber darf beim Lesen selektiert sein |
| SCS1–5 | PL0, PL2, PB6, PL6, PJ7 | **HIGH selektiert**, LOW beendet/deaktiviert |
| RESET_DRV | PE7 | gemeinsamer Reset, **HIGH aktiv**, normal LOW |
| FAULTn | PE6 | gemeinsame Leitung aller fünf Treiber, **LOW Fehler**, Open-Drain / Pullup erforderlich |
| STALLn1–5 | PG0–PG4 | individuelle LOW-aktive Open-Drain-Stallausgänge / Pullups |

Die HIGH-aktive SCS-Auswahl ist die dokumentierte DRV8711-Polarität. `RF.cpp:drv8711Enable()` setzt HIGH, `drv8711Disable()` LOW. Es gibt auf den Schaltplanblättern keinen invertierenden Baustein dazwischen. Übliche LOW-aktive SPI-CS-Routinen wären hier falsch. Reads mit mehreren SCS gleichzeitig sind unzulässig; gemeinsame Broadcast-Writes sind bei der Referenz vorhanden.

Die Referenz sendet SDATI vor der steigenden Taktflanke und setzt SCLK danach wieder LOW. Sie verwendet mindestens 1 µs Datenvorlauf und 5 µs Takt-HIGH. TI §6.6 verlangt mindestens 250 ns Taktperiode, 25 ns HIGH/LOW, 5 ns Setup für SDATI/SCS und 100 ns inaktives SCS zwischen Transaktionen. Der langsame Software-SPI liegt nominell darüber; reale Flanken und Open-Drain-Anstiegszeiten sind dennoch zu messen.

**Notwendige Abweichung von der Referenz:** `drv8711Init()` setzt Reset 5 ms HIGH und wartet nach LOW weitere 5 ms. TI §7.4.1 bestätigt zwar ausdrücklich den Registerzugriff im Sleep, erklärt aber auch: **RESET wirkt bei SLEEPn=LOW nicht**, und **UVLO lässt sich im Sleep nicht löschen**. Ein blindes Kopieren des Repetier-Ablaufs bei schlafenden Treibern wäre deshalb unzureichend. Der Port sichert zunächst die Leistungsfreigabe über CTRL.ENBL=0 ab und liest sie zurück, führt danach die für Reset/UVLO nötige kontrollierte Wake-Phase ohne STEP aus und bereitet erst nach erfolgreicher Konfiguration/Prüfung XYZ zur späteren Freigabe vor. A/B verbleiben über ENBL=0 gesperrt und werden anschließend wieder in Sleep versetzt. Ein fehlgeschlagener Rücklesetest bleibt eine Bewegungssperre.

TI §6.6 nennt maximal 1 ms Wake-up-Zeit; §7.4.1 verlangt nach Reset ungefähr 1 ms bis voller Funktion. Die Repetier-Wartezeit vor ersten Schritten beträgt 25 ms. Die Hardwaremessung muss diese Sequenz, die Registerwerte und SLEEPn/ENBL getrennt bestätigen. Für STEP/DIR fordert TI §6.7 mindestens **1,9 µs HIGH**, **1,9 µs LOW**, **200 ns DIR-Setup** und **200 ns DIR-Hold**, maximal **250 kHz** am IC. Dieser IC-Grenzwert ist keine Freigabe einer solchen Frequenz auf dem AVR.

`RF.cpp` setzt die internen Pullups für SDATO, FAULTn und alle fünf STALLn. Externe Pullups an diesen Leitungen sind in den Treiberblättern nicht dargestellt. FAULTn fasst mehrere Fehlerquellen zusammen. Der Port verriegelt nach LOW die Abschaltung; `$DRV` zeigt den Fault-Grund und gespeicherte Start-STATUS-Werte, keine laufenden SPI-Abfragen. Die genaue Zuordnung eines späteren Treiberfehlers verlangt eine gesonderte kontrollierte Diagnose ohne Bewegung. Die Ursache ist vor jedem erneuten Start zu beheben. Eine automatische wiederholte Fehlerlöschung oder selbsttätige Wiederaufnahme einer Bewegung ist für den Hardwaretest ausgeschlossen. STALLn ersetzt weder Endschalter noch Not-Aus.

## Strom und Mikroschritte

Die zehn Shunts auf Blatt 3–5 sind als **0,033 Ω** beschriftet: DRV1 R80/R82, DRV2 R81/R83, DRV3 R84/R86, DRV4 R85/R87, DRV5 R88/R89. Dieser ergänzend aus dem Plan übernommene Wert ist die Berechnungsgrundlage. Der tatsächlich geregelte Wicklungsstrom und die Erwärmung bleiben Funktionstests.

Die Referenz erzeugt CTRL aus `0x0E01 | (MODE << 3)`: DTIME=`11` (850 ns), ISGAIN=`10` (**20**), EXSTALL=0, RDIR=0, RSTEP=0, ENBL=1. Der unveränderte GAIN-Wert ist entscheidend. TORQUE wird mit `0x1100 + level` geschrieben: TORQUE=`level`, SMPLTH=`001`; `110` bedeutet weder 110 mA noch 1,10 A.

Für den Soll-Spitzenstrom bei voller Sinusaussteuerung gilt laut DRV8711 §7.3.4, Gleichung 2:

`I_FS = (2,75 V × TORQUE / 256) / (ISGAIN × R_SENSE)`

Die folgende Tabelle dokumentiert das ursprüngliche Repetier-Mikroprofil,
das die getrennten Standard-/Y-Testprofile weiterhin verwenden:

| Kanal | TORQUE | ISGAIN | Shunt | Rechnerischer I_FS | Mikroschritte | CTRL bei ENBL=1 |
|---|---|---|---|---|---|---|
| X | 110 | 20 | 0,033 Ω | ca. 1,790 A | 1/32 | 0x0E29 |
| Y | 110 | 20 | 0,033 Ω | ca. 1,790 A | 1/32 | 0x0E29 |
| Z | 105 | 20 | 0,033 Ω | ca. 1,709 A | 1/16 | 0x0E21 |
| Reserve A | 90 | 20 | 0,033 Ω | ca. 1,465 A | 1/32, Repetier-E-Wert | 0x0E29 |
| Reserve B | 90 | 20 | 0,033 Ω | ca. 1,465 A | 1/32, Repetier-E-Wert | 0x0E29 |

Diese Zahlen sind nominale Wicklungs-Spitzenstrom-Sollwerte, keine gemessenen Ströme und keine Netzteil-Gesamtströme. Motor- und Platinenstromgrenzen, Widerstandstoleranz, Verstärkerfehler, Decay, Mikroschrittposition und Temperatur bleiben zu prüfen. Die in Repetier vorhandene grobe Bemerkung „126 ≈ 2 A“ ist keine genauere Berechnungsgrundlage. Für schlafende beziehungsweise über ENBL gesperrte A/B wird kein Motorstrom freigegeben.

Die Tabelle zeigt den Referenz-CTRL mit ENBL=1; Reserve A/B muss im Port mit gelöschtem ENBL, also CTRL `0x0E28`, abgelegt werden. Die kurze Wake-Phase während des Starts ist damit keine Freigabe der Motorbrücke. Weitere aus der Referenz übernommene SPI-Wörter: OFF `0x2097`, BLANK `0x31D7`, DECAY `0x4430`, STALL `0x583C`, DRIVE `0x60F0`, STATUS-Schreibwert `0x7000`. Das oberste Adressnibble ist nicht Teil des jeweiligen 12-Bit-Registerinhalts. TORQUE.SMPLTH Bit 10 ist laut TI §7.6.3 nur schreibbar und muss vom Readback-Vergleich ausgenommen werden. Diese Werte sind keine beliebig sicheren Defaults für andere MOSFETs, Motoren oder Shunts.

Das aktuelle CNC-Profil `20261024-rf2000-cnc-adaptive-spi` startet XYZ mit
1/256 und CTRL `0x0E41` bei ENBL=1. Im Betrieb wechselt MODE zwischen
1/256 und 1/8 an pulsfreien Abschnittsgrenzen mit Readback. A/B bleiben
1/32 und gesperrt. Pins und Stromwerte ändern sich nicht. GRBL verwendet
bei der vorgegebenen SFU1605-/2:1-Mechanik ein festes virtuelles Raster
mit nominal **20480/20480/20480 Einheiten/mm**. Dieses Raster darf nicht
mit der momentanen Hardware-STEP-Zahl verwechselt werden.
[Phasenführung und Migration](adaptive-mikroschritte.md).

Die nominelle Mechanik aus `RF2000v2.h` und `Configuration.h` ergibt X/Y `4,761875 × 32 = 152,38 steps/mm`, Z `80 × 16 = 1280 steps/mm`. Das gilt nur bei unveränderter Mechanik. Eine kurze Messfahrt muss den realen Weg bestätigen, bevor größere Wege erlaubt werden.

## Endschalter und Referenzrichtung

| Eingang | AVR / Mega | Plan-Stecker | Im Plan sichtbare Beschaltung |
|---|---|---|---|
| X-Min / ES1 | PC2 / D35 | X13 | 1 kΩ Pullup an P5V (R42), 100 Ω Serie (R48), 100 nF nach VSS (C53) |
| Y-Min / ES2 | PC0 / D37 | X14 | 1 kΩ Pullup (R44), 100 Ω Serie (R49), 100 nF (C54) |
| Z-Min / ES3 | PC6 / D31 | X15, 4-polig | P5V an Pin 1, Signal Pin 3, VSS an Pins 2/4; kein gleichartiger externer RC/Pullup sichtbar |
| Z-Max / ES4 | PH1 / D16 | X36 | 1 kΩ Pullup (R101), 100 Ω Serie (R102), 100 nF (C104) |

Die Endschalterzuordnung ist zusätzlich handschriftlich im bereitgestellten Blatt 2 bezeichnet. `RF2000v2.h` aktiviert alle genannten internen Pullups und setzt `ENDSTOP_*_INVERTING true`. `Printer.h` wertet `READ(pin) != true` als Treffer: **LOW bedeutet betätigt**. Der Kontakt- oder Sensortyp am freien Stecker sowie seine NO/NC-Eigenschaft ist damit nicht nachgewiesen. Kabelbrucherkennung ist bei dieser Pegellogik nicht zugesichert; ein offenes Kabel kann HIGH und damit „frei“ ergeben. Insbesondere Z-Min kann ein aktiver Sensor sein. Vor dem Anlegen externer Pegel Signalspannung, Massebezug und Pinreihenfolge messen.

PC0/PC2/PC6 und PH1 besitzen am ATmega2560 keine geeigneten PCINT-Eingänge. Der Port verwendet daher **Timer2-CTC mit 1 kHz** statt eines RAMPS-PCINT-Vektors. Beide physischen Z-Schalter werden zur logischen Z-Grenze zusammengefasst; die Diagnose unterscheidet sie. LOW wird beim ersten erkannten Abtastwert verriegelt; Freigabe erst nach fünf aufeinanderfolgenden freien Samples. Zusätzlich prüfen Bewegungsstart und `$X` die rohen Pegel. Der Abtastabstand ist nominell 1 ms, hinzu kommen Interruptsperrzeiten; eine garantierte physische Stopplatenz wurde nicht gemessen. Die tatsächlich erzielte Abtastrate, ISR-Sperrzeit und Stopplatenz müssen mit STEP und Schaltsignal gemeinsam gemessen werden; Software-Endschalter und Hard-Limits ersetzen keine unabhängige Abschaltung.

Die Quellwerte `INVERT_X_DIR=true`, `INVERT_Y_DIR=false`, `INVERT_Z_DIR=false` sind zunächst elektrische Kandidaten. Repetier und GRBL verwenden unterschiedliche DIR-Bedeutungen: Eine positive Repetier-Bewegung setzt hier X=LOW, Y/Z=HIGH; deshalb ist die entsprechende GRBL-Invertmaske **Y|Z**, nicht X. Die Implementierung verwendet diese Umrechnung, trotzdem bleibt die physische Richtung zu prüfen. Repetier definiert X/Y/Z `HOME_DIR=-1`; **im Fräsmodus kehrt `Printer::anyHomeDir(Z_AXIS)` Z auf +1 um und benutzt Z-Max**. Deshalb ist ein unkritisches Übernehmen des Drucker-Z-Min-Homings falsch. Mechanische Orientierung, Z-Bewegung relativ zu Werkzeug/Werkstück und Schalterlage müssen am umgebauten Gerät bestätigt werden.

Im Standardprofil bleibt Homing per Compile-Gate `RF2000_HOMING_VERIFIED=0` gesperrt; `$22=1` und `$H` dürfen es nicht umgehen. Eine spätere Freigabe verlangt dokumentierte Richtungs- und Pegelprüfung, belegte Z-Max-Zuordnung, getestetes Rückziehen vom Schalter und erneuten Build. Ein bereits aktiver Endschalter soll einen Alarm auslösen; kein automatisches Losfahren zur vermeintlichen Freifahrt. Tests an solchen Zuständen erfolgen zuerst mit abgetrennten Motoren.

Das getrennte Y-Profil `rf2000_y_homing_test` ist die ausdrücklich angeforderte
Ausnahme für den Motorprüfstand: nur DRV2 erhält CTRL.ENBL=1, nur Y darf
STEP ausgeben. `$HY` verlangt die Sitzungsfreigabe `$22=1`. Die erste Suche
läuft ohne Gesamtweg- oder Zeitlimit bis zum Y-Min-Kontakt; Rückzug und
langsame zweite Annäherung folgen anschließend. Testwerte und Abweichungen
zu Repetier stehen in [y-axis-test.md](y-axis-test.md). Eine physische
Richtungs-/Pegelprüfung muss vor dem Start erfolgen.

## Neueste CNC-Freigabe nach Nutzervorgabe

Am 19.10.2026 bestätigte der Nutzer die Funktionen von Display, Tasten, Y-Motor, Y-Referenzfahrt und Referenzschalter. Er wünschte anschließend XYZ-Fräsbetrieb und erklärte ausdrücklich, dass X/Z noch nicht geprüft seien, aber zunächst als funktionierend angenommen werden sollen. Deshalb erlaubt das neue Profil `rf2000_cnc` mit `RF2000_CNC=1` die Referenzfolge Z-Max, danach X/Y-Min. Dies ersetzt für dieses Profil die frühere Voraussetzung einer vollständigen physischen Homing-Prüfung. `RF2000_HOMING_VERIFIED` bleibt 0; die Annahme wird nicht als Messnachweis ausgegeben. Die Spindel bleibt auf ausdrückliche Nutzervorgabe manuell; M3/M4/M5 schalten keine vorhandene Last. Details: [cnc-gsender.md](cnc-gsender.md).

## Originaldisplay und Tasten im Y-Test- und CNC-Profil

Das getrennte Y-Testprofil und das CNC-Profil verwenden das originale **20×4-HD44780-Display im 4-Bit-Modus** und die elf direkten Taster. Die übrigen Druckermenüs und Druckerfunktionen werden nicht importiert. Im bisherigen XYZ-Testprofil `rf2000_grbl` bleiben Display und Tasten unbenutzt. Maßgeblich sind `references/repetier/Repetier/pins.h:680–702` für die Taster und `pins.h:740–757` für das Display. Displaytyp, Größe und ursprüngliche Bytewartezeit stehen in `ui.h:257–265`; die Ansteuerung übernimmt die Initialisierungsfolge aus `ui.cpp:438–500`. Die Dateien unter `references/repetier/` bleiben unverändert.

| LCD-Signal | AVR / Mega | Displaystecker X21 |
|---|---|---|
| RS | PK1 / D63 | Pin 13 |
| E | PK3 / D65 | Pin 9 |
| D4 | PF5 / D59 | Pin 7 |
| D5 | PK2 / D64 | Pin 5 |
| D6 | PA1 / D23 | Pin 3 |
| D7 | PJ3 / Repetier-Sonderpin 98 | Pin 1 |
| R/W | VSS, kein GPIO | Pin 11 |

Blatt 2 zeigt 51-Ω-Serienwiderstände in den sechs Daten-/Steuerleitungen. R/W liegt fest an Masse; der Port schreibt ausschließlich und liest kein Busy-Bit. Die vier DDRAM-Zeilen beginnen bei `0x00`, `0x40`, `0x14`, `0x54`, entsprechend `ui.h:672–679`. Die Startfolge wartet zunächst 500 ms und überträgt die Nibbles `3, 3, 3, 2`, anschließend die Befehle `0x28`, `0x01`, `0x06`, `0x0C`, mit den ursprünglichen Initialisierungswartezeiten. Laufende Displayausgaben werden im Vordergrund zeitlich verteilt. Kurze atomare, maskierte Portzugriffe erhalten insbesondere die STEP-Bits auf PORTF, Z-SLEEP auf PK0 und DRV5-SCS auf PJ7; Wartezeiten und LCD-Pulse sperren die Schrittinterrupts nicht über einen gesamten Zeichentransfer.

Die Tasten sind aktive LOW-Eingänge mit internen Pullups gemäß `ui.h:318–320` und `ui.cpp:566–593`. Blatt 2 ergänzt externe 3,3-kΩ-Pullups nach P5V. Die Zuordnung lautet:

| Tasterfunktion der alten Firmware | AVR / Mega | Tastenstecker X23 |
|---|---|---|
| OK | PG5 / D4 | Pin 2 |
| Abwärts / NEXT | PB7 / D13 | Pin 3 |
| Rechts | PH0 / D17 | Pin 1 |
| Links / BACK | PB5 / D11 | Pin 4 |
| Aufwärts / PREVIOUS | PL7 / D42 | Pin 6 |
| Z auf | PJ2 / Repetier-Sonderpin 80 | Pin 14 |
| Z ab | PJ4 / Repetier-Sonderpin 81 | Pin 8 |
| Filament zurück | PJ5 / Repetier-Sonderpin 82 | Pin 11 |
| Filament vor | PJ6 / Repetier-Sonderpin 83 | Pin 13 |
| CONTINUE | PH7 / Repetier-Sonderpin 85 | Pin 7 |
| PAUSE | PH2 / Repetier-Sonderpin 86 | Pin 9 |

**Weitere belegte Abweichung:** Auf Blatt 2 ist X23 Pin 7 als „PAUSE“ und Pin 9 als „CONT“ beschriftet. Die Repetier-Funktionen sind dazu vertauscht: `pins.h:689–692` verbindet E5 mit PH7 und E6 mit PH2; `ui.cpp:592–593` ordnet E5 `CONTINUE` und E6 `PAUSE` zu. Die elektrische GPIO-Zuordnung folgt weiterhin der alten Firmware. Für den neuen Y-Test dienen **beide Leitungen PH7 und PH2 bewusst als Abbruch**, ebenso Links. Diese neue Testfunktion ist keine unverändert übernommene Pause-/Fortsetzen-Funktion; keine der beiden Sondertasten setzt eine abgebrochene Bewegung fort. Die früheren Z-/Extrudertasten aktivieren keine Z- oder Extruderbewegungen.

**Ergänzung zur alten Firmware:** Die Hintergrundbeleuchtung wird im Y- und CNC-Profil über **PA0 / D22 (`D_LED`) HIGH** eingeschaltet. Blatt 2 zeigt den BC847-Masseschalter Q8, zwei 10-kΩ-Widerstände R76/R78 und die LED-Versorgung über R53 mit 22 Ω / 0,5 W. Die bereitgestellte Repetier-Firmware konfiguriert PA0 nicht ausdrücklich; diese Aktivierung folgt ergänzend dem Schaltplan. PA0 ist kein Motorpin. Die Display-/Tastenintegration und diese beiden bewussten Änderungen sind Quellen- und Softwarebefunde, keine bestätigten Messungen am Originalbedienteil.

## Unbenutzte Leistungsausgänge und Watchdog-Verhalten

Blatt 6 zeigt aktive HIGH-MOSFET-Ausgänge. HZ1/HZ2/HZ3 und OUT1–5 haben 100-kΩ-Pulldowns (R121–R128); diese wirken auch während Reset hochohmiger MCU-Pins. Für den CNC-Test werden HZ1/PB4, HZ2/PH6, HZ3/PE2, P230/PE5, OUT1/PA3, OUT2/PA5, OUT3/PC4, OUT4/PC5, OUT5/PF4, RGB/PH3–PH5 und Buzzer/PE3 inaktiv gehalten. Der Port bietet keine Heizer-, Extruder-, Temperatur-, Wägezellen-, SD- oder Servo-Funktion. LCD und Tasten bleiben im XYZ-Testprofil unbenutzt; Y-Test- und CNC-Profil verwenden das oben beschriebene lokale Bedienteil. M3/M4/M7/M8 dürfen diese Verbraucher nicht freigeben. P230 ist eine potenziell netzspannungsführende Steckdose; der Port ist keine fertige Spindelschnittstelle.

Die aktuelle Entscheidung zur abweichenden Zuordnung lautet:

| Anschluss | Bisherige Interpretation | Jetzt maßgebliche RF2000v2-Konfiguration |
|---|---|---|
| PA7 / D29 | Mit PC4 als gemeinsames `OUT3` behandelt | `WATCHDOG_PIN`, getrennt von OUT3 |
| PC4 / D33 | OUT3 LOW | OUT3 LOW, unverändert |
| PC0 / D37 | ES2 / Y-Min | ES2 / Y-Min, unverändert |

PA7 bleibt **Eingang ohne Pullup**. Das entspricht `HAL::stopWatchdog()` der Referenz: `WRITE(WATCHDOG_PIN, LOW)` und anschließend `SET_INPUT(WATCHDOG_PIN)`. GRBL erzeugt keinen aktiven Watchdog-Takt. PC4 bleibt als Leistungsausgang LOW und PC0 als Endschaltereingang mit Pullup konfiguriert. Diese Entscheidung ändert die Interpretation von PA7 und die Quellenpriorität; die bereits implementierten elektrischen GPIO-Pegel bleiben gleich.

Die im PDF bei N2/WDI über R11 sichtbare Netzbezeichnung `PC0` ist nicht allein aufgrund des gleichen Namens mit dem CPU-Portpin PC0 gleichzusetzen. Der CPU-Portpin PC0 führt über das Netz `ES2` zum Y-Endschalter X14. Eine direkte Verbindung dieses Endschaltereingangs zum Watchdog wird daraus nicht behauptet. Das reale RESET-/Watchdog-Verhalten und die Wirkung des hochohmigen PA7 sind Teil des Logiktests; unerwartete Neustarts werden vor Bewegungen untersucht.

Die unabhängige Not-Aus-Verschaltung, Netzteilabschaltung und vorhandene Relais-/Triacschaltung auf Blatt 6 müssen vor einem Test elektrisch geprüft werden. Die Firmware bestätigt deren Funktion nicht. Sie kann weder ein verschweißtes Relais noch einen durchgeschlagenen MOSFET absichern. Für erste Tests bleiben sämtliche Heizer, Netzsteckdose, Spindel und Leistungslasten physisch abgetrennt.

## Noch am Gerät nachzuweisen

- Stabiles Reset-/Watchdog-Verhalten im Logikbetrieb, ohne Takten eines Limit- oder Leistungsausgangs.
- Motordaten, tatsächlicher Wicklungsstrom auf Basis der 0,033-Ω-Shunts und thermisches Verhalten.
- Registerlesbarkeit aller fünf DRV8711 mit verifizierter Versorgung/Wake-Sequenz, keine unerkannte offene SDATO-Leitung.
- Inaktive Ausgänge über Bootloader, Reset, Initialisierung, Alarm und Kommunikationsverlust; A/B permanent ohne Schrittflanken und ohne Freigabe.
- Endschalterpegel, Sensorart, Kontaktzustände, Kabelbruchverhalten, Entprellung und tatsächliche Stopplatenz.
- STEP-Breite, DIR-Vorlauf/-Nachlauf, Kanalgleichzeitigkeit und Interruptreserve unter serieller Last.
- Sichere Bewegungsrichtung und Referenzposition des umgebauten Geräts; Homing bleibt bis dahin gesperrt.

Der schrittweise Ablauf und die Abbruchkriterien stehen in [hardware-test.md](hardware-test.md).
