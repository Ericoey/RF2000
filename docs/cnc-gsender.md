# XYZ fräsen mit gSender und Originaldisplay

**`rf2000_cnc` ist das Standardprofil in PlatformIO.** Es unterstützt XYZ-
Jogging, G0/G1, G2/G3, Arbeitskoordinaten und GRBL-Programmübertragung. Alle
fünf DRV8711 werden initialisiert; A/B bleiben gesperrt. Heizungen, P230,
Kühlmittel, Probe und sonstige Leistungsausgänge bleiben ausgeschaltet.

Die Spindel wird entsprechend der Nutzervorgabe **manuell an Spindel bzw.
Frequenzumrichter** eingeschaltet, ausgerichtet und auf Drehzahl gebracht.
M3/M4/M5 und S-Werte werden als Programmbefehle angenommen, schalten aber
keinen Ausgang. **M5, gSender-Stopp und Firmware-Reset schalten diese manuelle
Spindel nicht aus.** Das muss am Spindelgerät erfolgen.

## Stand der Hardwareprüfung

Der Nutzer hat Display, Tastenerkennung, Y-Motor, Y-Referenzfahrt und
Y-Referenzschalter am 19.10.2026 als funktionierend bestätigt. Anschließend
wurde XYZ-Homing ausdrücklich auf Basis der Annahme freigegeben, dass die
Repetier-Zuordnung funktioniert: **X/Y nach MIN, Z nach MAX**. X-/Z-Richtung
und Schalter wurden dabei ausdrücklich noch nicht physisch geprüft. Das
CNC-Profil setzt `RF2000_CNC=1`; es behauptet keine vollständige
Hardwareverifikation durch `RF2000_HOMING_VERIFIED`.

## Aktualisieren und aufspielen

gSender und seriellen Monitor schließen. Im Repository-Hauptordner:

```bash
git pull
pio run -e rf2000_cnc -t upload --upload-port /dev/cu.usbserial-AL03P2K9
```

Den Port gegebenenfalls durch den aktuellen Gerätenamen ersetzen. In VS Code
**Project Tasks → rf2000_cnc → Upload** auswählen; eine vorher ausdrücklich
gewählte Y-Umgebung kann in der Statusleiste weiter ausgewählt sein.
`pio run` baut ebenfalls CNC, überträgt ohne `-t upload` aber nichts.
HEX/ELF/MAP liegen unter `.pio/build/rf2000_cnc/`.
Alternativ: `make cnc`, Ergebnisse unter `artifacts/cnc/rf2000-grbl.*`.

Die Startmeldung lautet **`RF2000 XYZ CNC; spindle manual; ...`**.
`$I` enthält `20261019-rf2000-cnc`. Das Y-Testprofil bleibt mit
`-e rf2000_y_homing_test` wählbar und hat weiterhin seine erste endlose Suche.

## gSender verwenden

1. Serielle USB-Verbindung mit **GRBL und 115200 Baud** öffnen. Nur ein
   Programm darf den Port gleichzeitig verwenden. Der Startalarm ist
   beabsichtigt.
2. Die Eingänge über `$DRV` oder die Displaydiagnose kontrollieren. Betätigte
   Schalter oder ein Treiberfehler verhindern die Freigabe. Ein anfangs
   betätigter Schalter wird nicht automatisch freigefahren.
3. Zum Referenzieren **Home** in gSender wählen oder **`$H`** senden.
   Z fährt nach Z-Max, danach X/Y nach ihren Min-Schaltern. Jede Achse führt
   Anfahren, Rückzug, langsames Wiederanfahren und abschließenden Rückzug aus.
   **`$HX`, `$HY`, `$HZ`** referenzieren einzelne Achsen. Im CNC-Profil ist
   auch die erste Suche auf den konfigurierten Suchweg begrenzt.
4. Die XYZ-Jogfelder in gSender fahren die gewählte Achse. **`$X`** entriegelt
   bei freien Eingängen auch für manuelle Einrichtung ohne vorheriges Home;
   dadurch entsteht noch kein gesicherter Maschinenbezug.
5. Werkzeug am gewünschten Werkstücknullpunkt positionieren und die
   gewünschten Achsen in gSender auf null setzen. Die Firmware unterstützt
   die üblichen G10-/G54-Kommandos. Am Display ist dasselbe für alle drei
   Achsen über **Arbeitsnull setzen**, gefolgt von einer zweiten Bestätigung,
   möglich. Es wird das gerade aktive System G54–G59 gesetzt.
6. Fräsdatei mit einem **GRBL-Postprozessor** laden, Spindel manuell starten
   und den Job senden. Kühlmittelbefehle M7/M8, G38-Probing, automatische
   Werkzeugwechsel M6 und GRBL-fremde Bohrzyklen sind nicht eingerichtet.
   Kühlmittel und automatische Werkzeugwechsel im Postprozessor abwählen.

gSender-Pause sendet `!` und hält normale Fräsbewegungen kontrolliert an;
Fortsetzen sendet `~`. Während einer Referenzfahrt wirkt `!` nicht als Stopp:
dafür Reset/Ctrl-X oder eine der lokalen Abbruchtasten verwenden. Die
manuelle Spindel bleibt auch während Pause und Abbruch separat zu bedienen.

## Display und Tasten

Die Hauptanzeige enthält Zustand, aktives Arbeitskoordinatensystem,
**XYZ-Arbeitspositionen in mm** und tatsächlichen Vorschub. Die letzten drei
Zeichen der ersten Zeile zeigen referenzierte Achsen: `XYZ`, teilweise
`X--`, oder `---` nach Neustart/Alarm. G54-Null und Maschinenreferenz sind
unterschiedliche Dinge. Während der Referenzfahrt erscheinen Phase und
betroffene Achsen; während eines Jobs steht unten `SPINDEL: MANUELL`.

| Taste | Funktion |
| --- | --- |
| Hoch / Runter im Menü | Menüpunkt wählen |
| OK im Menü | Auswählen; Referenzfahrt und Arbeitsnull benötigen eine zweite Betätigung |
| Rechts | Diagnose öffnen bzw. aus Unterseiten zum Hauptmenü zurück |
| Hoch / Runter unter Verfahren | Gewählte Achse positiv / negativ bewegen |
| OK unter Verfahren | Achse X → Y → Z → X wählen |
| Bisherige Z+ / Z−, PJ2 / PJ4 | Schrittweite 0,01 / 0,10 / 0,50 mm größer / kleiner |
| Bisherige Extrudertasten, PJ5 / PJ6 | Lokalen Jog-Vorschub 10 / 20 / 30 mm/min größer / kleiner |
| Links, Pause, Continue | Alle drei sofortiger Software-Abbruch und Reset |

Die beiden bisherigen Sondertasten bleiben bewusst Abbruch; zum pausierten
Fortsetzen eines Fräsprogramms die entsprechenden gSender-Schaltflächen
verwenden. Links ist auf jeder Seite Abbruch. Die alte PAUSE/CONTINUE-
Beschriftungsabweichung ist in [hardware-verification.md](hardware-verification.md)+dokumentiert. Gehaltene STOP-Tasten sperren auch neue serielle Bewegungen.
Andere gehaltene Tasten wiederholen keine Aktion und werden während einer
Bewegung nicht als spätere Bewegung gespeichert. Parameteränderung und
Seitenwechsel verursachen keine Bewegung.

## Einstellungen und Speicherung

| Einstellung | Vorgabe |
| --- | --- |
| Schritte/mm X/Y/Z | 152,38 / 152,38 / 1280 |
| Maximaltempo X/Y/Z | 500 / 500 / 150 mm/min |
| Beschleunigung | 10 mm/s² |
| Referenzrichtung `$23` | 3: X/Y-Min, Z-Max |
| Homing `$22`, Hard Limits `$21` | jeweils 1 |
| Such-/Feintempo `$25` / `$24` | 100 / 20 mm/min |
| Rückzug `$27` | 2 mm |
| Verfahrbereich X/Y/Z | 200 / 290 / 185 mm |
| Motorhaltung `$1` | 255: Haltekraft bis Reset/Abschaltung |
| Soft Limits `$20` | 0; nach bestätigtem Referenzbezug und passendem Verfahrbereich auf 1 einstellbar |
| Pulsbreite | 10 µs; einstellbar 10–20 µs |

Das CNC-Profil speichert GRBL-Einstellungen und Arbeitsnullpunkte im EEPROM
(Profilversion 74). Beim ersten Start werden CNC-Standardwerte initialisiert;
die EEPROM-Belegung ist nicht mit Repetier bzw. dem XYZ-Testprofil kompatibel.
Die lokale Auswahl von Achse, Schrittweite und Jogtempo bleibt im RAM und
startet nach Reset wieder bei X, 0,10 mm und 20 mm/min. Nach Änderungen von
Kalibrierung, Richtung oder Mechanik erneut referenzieren.

Die Softwareprüfungen stehen in [validation.md](validation.md).
`make check-cnc` führt die CNC-AVR-Simulation aus. Ein lokal mit PlatformIO
erzeugtes ELF kann mit
`tests/run-cnc-sim.sh .pio/build/rf2000_cnc/firmware.elf` geprüft werden.
