# XYZ fräsen mit gSender und Originaldisplay

Alle verfügbaren Konsolenbefehle mit Beispielen stehen in der
[RF2000-Befehlsübersicht](befehle.md).

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
Y-Referenzschalter am 19.10.2026 als funktionierend bestätigt. Auch der
Y-Fahrweg mit **2560 Schritten/mm bei bisher 1/32** wurde durch den Nutzer
bestätigt. Am 22.10.2026 zeigte sein CNC-EEPROM tatsächlich
2560 / 2560 / 1280 Schritte/mm und 100 / 100 / 150 mm/min.
Das adaptive Mikroprofil benötigt eine erneute langsame Wegprüfung.
XYZ-Homing wurde ausdrücklich auf Basis der Annahme freigegeben, dass die
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
`$I` enthält `20261024-rf2000-cnc-adaptive-spi`. Das Y-Testprofil bleibt mit
`-e rf2000_y_homing_test` wählbar und hat weiterhin seine erste endlose Suche.

**Nach einem Update zuerst die unten beschriebene
[EEPROM-Migration prüfen](#bestehende-cnc-einstellungen-umstellen).**
Alte Schritte/mm dürfen bei dem neuen virtuellen Raster nicht zurückgespielt werden.

## gSender verwenden

1. Serielle USB-Verbindung mit **GRBL und 115200 Baud** öffnen. Nur ein
   Programm darf den Port gleichzeitig verwenden. gSender 1.6.4 erkennt den
   Typ automatisch; oben links soll **Grbl** erscheinen. Der Startalarm ist
   beabsichtigt.
2. Die Eingänge über `$DRV` oder die Displaydiagnose kontrollieren. Betätigte
   Schalter oder ein Treiberfehler verhindern die Freigabe. Ein anfangs
   betätigter Schalter wird nicht automatisch freigefahren. Für einen einzelnen
   betätigten Endschalter gibt es den ausdrücklich anzufordernden
   [Freifahrablauf `$LR`](#nach-einem-hard-limit-freifahren).
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
   die üblichen G10-/G54-Kommandos. Am Display unter **Arbeitsnull** alle
   oder einzelne Achsen auswählen und mit einer zweiten Betätigung bestätigen.
   Es wird das gerade aktive System G54–G59 gesetzt.
6. Fräsdatei mit einem **GRBL-Postprozessor** laden, Spindel manuell starten
   und den Job senden. Kühlmittelbefehle M7/M8, G38-Probing, automatische
   Werkzeugwechsel M6 und GRBL-fremde Bohrzyklen sind nicht eingerichtet.
   Kühlmittel und automatische Werkzeugwechsel im Postprozessor abwählen.

gSender-Pause sendet `!` und hält normale Fräsbewegungen kontrolliert an;
Fortsetzen sendet `~`. Während einer Referenzfahrt wirkt `!` nicht als Stopp:
dafür Reset/Ctrl-X oder die lokale **Pause-II-Taste** verwenden. Die
manuelle Spindel bleibt auch während Pause und Abbruch separat zu bedienen.

### gSender zeigt automatisch grblHAL oder „Alarm()“

gSender 1.6.4 fragt beim Verbinden `$I` ab und sucht in den Antworten nach
dem Firmwarenamen. Ohne passende Kennung verwendet es den gespeicherten
Standardtyp. Wurde die einmalige Startmeldung verpasst, konnte unsere alte
Antwort so als grblHAL eingeordnet werden. Seit der CNC-Version
`20261019-rf2000-cnc-gsender` antwortet die Firmware zusätzlich mit
**`[MSG:Grbl-Mega-5X RF2000]`**. Die echte Versionsnummer 1.2i bleibt erhalten.
Nach dem Aufspielen gSender trennen und neu verbinden; oben links muss
**Grbl** stehen. Eine manuelle Typauswahl ist dafür nicht erforderlich.

Die Startsperre nach Einschalten/Reset ist beabsichtigt. Eine leere
Alarmnummer allein unterscheidet sie nicht von einem Hardwarefehler.
In der gSender-Konsole nacheinander `$I`, `$DRV` und `?` senden. Bei freien
Eingängen und funktionsfähigen Treibern zeigt `$DRV`
`READY=1|FAULT=0|READBACK=0|LIMITS=0`. Danach mit `$H` referenzieren oder mit
`$X` für die manuelle Einrichtung entriegeln. Ein echter `ALARM:n` bzw.
abweichende Diagnosewerte müssen anhand der Konsolenausgabe geklärt werden.

## Nach einem Hard Limit freifahren

Ein im normalen Betrieb betätigter Endschalter stoppt die Bewegung sofort
mit `ALARM:1`. Der abrupte Halt kann Schritte und damit den Maschinenbezug
verlieren lassen. Die Firmware fährt deshalb nach dem Anschlag nicht
selbsttätig zurück und setzt den unterbrochenen Fräsjob nicht automatisch fort.

Das CNC-Profil bietet dafür **`$LR`** (Limit Release). Der Befehl bewegt nur
die Achse des aktiven Schalters vom Schalter weg. Er ist auch bei einem
bereits beim Einschalten betätigten Schalter verwendbar, sofern genau ein
physischer Endschalter aktiv ist und kein Treiberfehler oder STOP vorliegt.

1. Den unterbrochenen Job in gSender beenden und die manuelle Spindel am
   Spindelgerät ausschalten.
2. Nach dem Hard-Limit-Alarm zunächst **Reset** in gSender bzw. **Ctrl-X**
   senden. Erst dieser Reset beendet den kritischen Alarm-Wartezustand.
   Die lokale **Pause-II-Taste** führt im Alarmzustand ebenfalls sofort einen
   Reset aus; alternativ **Play und Pause zusammen**. `$X` ist für diesen
   Ablauf weder nötig noch bei aktivem Schalter zulässig.
3. Mit `$DRV` oder der Displaydiagnose prüfen, welcher Schalter aktiv ist.
   Bei mehreren aktiven Schaltern wird `$LR` abgewiesen; die Ursache muss
   zuerst geprüft werden.
4. In der gSender-Konsole senden:

   ```text
   $LR
   ```

   Alternativ im CNC-Hauptmenü **Endschalter frei** auswählen und mit einer
   zweiten, getrennten OK-Betätigung bestätigen. Die Menüfunktion verwendet
   denselben Freifahrbefehl. Während der Bewegung zeigt das Display
   **FREIFAHREN** und **2mm max, F20**.
5. Die Achse fährt einen festen Rückzug von **2 mm mit höchstens 20 mm/min**;
   ein niedriger eingestelltes Achsmaximum begrenzt das Tempo zusätzlich. X-Min, Y-Min und Z-Min
   werden in positiver Richtung verlassen, Z-Max in negativer Richtung.
   Sobald der ausgewählte Schalter für fünf aufeinanderfolgende Abfragen
   frei ist, ist dessen Überwachung wieder aktiv: Eine erneute Betätigung
   während des verbleibenden Rückzugs löst sofort wieder einen Hard Limit aus.
   Die Fahrt endet regulär nach den 2 mm; dann muss der Schalter frei sein.
6. Die Steuerung bleibt nach dem Rückzug im Alarmzustand; die
   Referenzanzeige ist ungültig. Danach mit **`$HY`** nur Y oder **`$H`**
   alle Achsen neu referenzieren und den Werkstücknullpunkt prüfen bzw.
   neu setzen. Den alten Fräsjob nicht aus der unterbrochenen Position
   fortsetzen.

Während `$LR` kann die serielle `MPos`-Anzeige vorübergehend bei null
beginnen: Sie bezieht sich dann auf diesen Rückzug. Daraus keinen neuen
Werkstücknullpunkt ableiten. Die alte Zählerposition wird anschließend um
den tatsächlichen Rückzug ergänzt, bleibt nach dem Hard Limit aber ungesichert.

Wird der Schalter innerhalb des Rückzugswegs nicht frei, endet der Versuch
mit **`ALARM:8`**. Ein anderer betätigter Endschalter, Treiberfehler, lokale
**Pause-II-Taste**, **`!`** oder **Ctrl-X/Reset** bricht das Freifahren ab. Nur der
gewählte Schalter wird während des begrenzten Rückzugs gesondert behandelt;
die anderen Endschalter und der Treiberschutz bleiben wirksam. Bei einem
Abbruch zunächst die Ursache klären und gegebenenfalls erneut zurücksetzen.

Die Freifahrrichtungen beruhen auf derselben Repetier-Zuordnung wie das
Homing. Y ist am Gerät bestätigt; X und Z bleiben bis zum eigenen
Fahrweg- und Schaltertest Annahmen.

## Display und Tasten

Die Hauptanzeige zeigt Zustand, aktives Arbeitskoordinatensystem,
**XYZ-Arbeitspositionen in mm**, tatsächlichen Vorschub und bewegte Achsen.
`XYZ`, teilweise `X--` oder `---` kennzeichnet den Referenzstatus;
Arbeitsnull und Maschinenreferenz sind unterschiedliche Dinge. Während
der Referenzfahrt erscheinen die konkrete Achse bzw. Achsgruppe, deren
Schalter und die Phase: **SUCHEN, RUECKZUG, FEINANFAHRT, FREIFAHREN**.
Bei `$H` steht zuerst `REF Z`, anschließend `REF X+Y` auf dem Display.

Mit **OK oder Rechts** auf der Startseite öffnet sich das Hauptmenü:
**Freigeben, Verfahren XYZ, Referenzieren, Arbeitsnull, Diagnose,
Endschalter frei, Jogtempo, Abbruch/Reset**. Hoch/Runter navigiert,
OK/Rechts wählt und Links geht zurück. Referenzieren und Arbeitsnull bieten
**alle Achsen oder nur X, Y bzw. Z**. Referenzfahrt, Arbeitsnull und
Freifahren benötigen eine zweite, getrennte Bestätigung.

Unter **Verfahren XYZ** fährt das Steuerkreuz **X−/X+ und Y+/Y−**;
die obere rechte Tastenreihe fährt **Z+/Z−**. Die mittleren **−/+** wählen
die Schrittweite **0,01 / 0,10 / 0,50 mm**. **OK** kehrt zum Menü zurück.
Das lokale Tempo wird separat unter **Jogtempo** auf **10/20/30 mm/min**
gestellt. Jede Achsfahrt erfordert einen getrennten Tastendruck; Halten
wiederholt keine Fahrt. Seitenwechsel und Parameterwahl bewegen keine Achse.

**Pause II** hält normale Fräsfahrten kontrolliert an und bricht eine
Jogfahrt ab. Während Referenzieren oder Endschalter-Freifahren sowie bei
Stillstand oder Alarm führt sie sofort einen Software-Reset aus.
**Eine Sekunde Pause halten oder Play+Pause** führt ebenfalls zum Reset.
**Play** setzt ausschließlich eine vollständig angehaltene normale Fahrt
fort; es startet keinen Job und löst weder Referenzfahrt noch Freigabe aus.
Während normaler Fahrten verändern die mittleren **−/+** den Feed-Override
in Schritten von zehn Prozentpunkten, höchstens bis **100 %**.

Die vollständige Tastenanordnung und Menüführung stehen in
[Bedienfeld und Menü](bedienfeld-menue.md). Die neue Belegung ist im Code
implementiert und noch am Gerät zu prüfen; die bisher bestätigte
Tastenerkennung ersetzt diesen Bedienungstest nicht.

## Einstellungen und Speicherung

Die CNC-Vorgaben beruhen auf der vorgegebenen Mechanik aller drei Achsen:
200 Vollschritte je Motorumdrehung, SFU1605 mit 5 mm Steigung und 2:1
zwischen Motor und Spindel. GRBL behält ein festes virtuelles 1/256-Raster;
die Hardware verwendet automatisch Stufen von 1/256 bis 1/8. Deshalb
bleiben `$100`–`$102` während der Fahrt konstant. Der frühere Y-Fahrweg war
bei festem 1/32 bestätigt; nach dem Update alle Achsen langsam vermessen.
[Funktionsweise der Automatik](adaptive-mikroschritte.md).

| Einstellung | Vorgabe |
| --- | --- |
| Virtuelle Einheiten/mm X/Y/Z | 20480 / 20480 / 20480 |
| Hardware-Mikroschritte | automatisch 1/256 bis 1/8 |
| Firmware-Maximum | 1000 mm/min für Achs- und Bahnfeed |
| Factory-Maximaltempo X/Y/Z | 600 / 600 / 300 mm/min |
| Beschleunigung | 10 mm/s² |
| Referenzrichtung `$23` | 3: X/Y-Min, Z-Max |
| Homing `$22`, Hard Limits `$21` | jeweils 1 |
| Such-/Feintempo `$25` / `$24` | 100 / 20 mm/min |
| Rückzug `$27` | 2 mm |
| Verfahrbereich X/Y/Z | 200 / 290 / 185 mm |
| Motorhaltung `$1` | 255: Haltekraft bis Reset/Abschaltung |
| Soft Limits `$20` | 0; nach bestätigtem Referenzbezug und passendem Verfahrbereich auf 1 einstellbar |
| Pulsbreite | 10 µs; einstellbar 10–20 µs |

Das CNC-Profil speichert Einstellungen und Arbeitsnullpunkte im EEPROM mit
**Profilversion 76**. Gültige Vorgänger der CNC-Versionen 74 und 75 werden
automatisch umgerechnet. Die getrennten XYZ-/Y-Testprofile behalten ihre
bisherige Kalibrierung und 5-kHz-Grenze; ihre EEPROM-Belegung ist nicht
mit dem CNC-Profil austauschbar.

Die Firmware begrenzt Achs- und Bahnfeed auf höchstens **1000 mm/min**.
Ein gemeinsames XYZ-Mikroprofil richtet sich nach der schnellsten Achse
im tatsächlich vorbereiteten Abschnitt. Das physische Pulsbudget ist
12 kHz; Verfeinerung verwendet Hysterese. Referenzphasen verwenden
standardmäßig fest 1/32; erhöhte Such-/Feintempi können vor Phasenbeginn
1/16 oder 1/8 erfordern. Innerhalb einer Phase bleibt das Profil konstant. `$MS` zeigt das virtuelle Raster und das
aktuelle Hardware-Mikroprofil, ohne die Einstellung zu ändern. Das Display
zeigt weiterhin die konkrete Achse und Aktion. Die Grenzen belegen keine
mechanische Freigabe: zuerst den [Hardwaretest](geschwindigkeit-optimieren.md)
durchführen.

### Bestehende CNC-Einstellungen umstellen

Vor dem Aufspielen `$I`, `$$`, `$#` und `$N` sichern. Danach im Stillstand
`$I`, `$$` und `$MS` lesen. Die neue Kennung enthält `cnc-adaptive` und die
nominalen Werte für `$100`/`$101`/`$102` sind **20480/20480/20480**.

- Version 75 mit nominal 320/320/1280: X/Y ×64, Z ×16.
- Version 74 mit nominal 2560/2560/1280: X/Y ×8, Z ×16.
- Der überholte X/Y-Mechanikwert 152,380 in Version 74 wird gesondert auf
  nominal 20480 korrigiert und gemeldet.

Ein gemessener Feinabgleich bleibt proportional erhalten. G54–G59,
G28/G30, Startzeilen und eigener Buildtext bleiben bei erkannten CNC-
Datensätzen erhalten. Niedrigere Tempi wie 100/100/150 werden nicht erhöht;
höhere alte Werte und Beschleunigungen werden konservativ begrenzt.

Der globale Schreibvorgang markiert zuerst eine laufende Transaktion,
schreibt Daten und Prüfsumme und setzt zuletzt Version 76. Ein erkannter
unterbrochener oder beschädigter CNC-Datensatz stellt nur globale Vorgaben
wieder her; die getrennten Koordinaten-/Startdatensätze bleiben erhalten.
Ein leeres oder fremdes Profil wird vollständig initialisiert. Deshalb
vor der ersten Bewegung die tatsächlich gemeldeten Werte prüfen.

**Alte Schritte/mm 320, 1280 oder 2560 nicht zurückspielen.** Die Werte
beschreiben nun das feste virtuelle Raster und nicht die gerade gewählte
Hardware-Stufe. Für das normale Update ist kein `$RST=*` erforderlich;
dieser Befehl würde Werkstücknullpunkte löschen.

Zuerst mit den langsamen 100/100/150-Werten und kleinen F20-Jogs den Weg
prüfen und neu referenzieren. Nach bestandenen Messungen können im
Stillstand die neuen Startvorgaben gewählt werden:

```text
$110=600
$111=600
$112=300
$120=10
$121=10
$122=10
$$
```

Diese Befehle ändern Einstellungen, bewegen aber keine Achse. Höhere
Tempi erst nach dem [stufenweisen Hardwaretest](geschwindigkeit-optimieren.md#hardwaretest-in-acht-stufen)
ermitteln; mehr als 1000 mm/min ist in diesem Profil nicht vorgesehen. Nach jeder
Kalibrierungsänderung Referenz und Werkstücknull prüfen.

Die lokale Auswahl von Schrittweite und Jogtempo bleibt im RAM und
startet nach Reset wieder bei 0,10 mm und 20 mm/min. Nach Änderungen von
Kalibrierung, Richtung oder Mechanik erneut referenzieren.

Die Softwareprüfungen stehen in [validation.md](validation.md).
`make check-adaptive` bzw. `make check-cnc` führt die adaptive CNC-AVR-Simulation
aus. Ein lokal mit PlatformIO erzeugtes ELF kann mit
`tests/run-adaptive-sim.sh .pio/build/rf2000_cnc/firmware.elf` geprüft werden.
