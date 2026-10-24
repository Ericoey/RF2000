# RF2000-CNC: Befehlsübersicht für gSender

Diese Übersicht gilt für das Projektprofil **`rf2000_cnc`**, Stand
22.10.2026, ab Version `20261024-rf2000-cnc-adaptive-spi`. Sie beschreibt die
im RF2000-Code vorhandenen Befehle. Das separate Y-Testprofil hat andere
Beschränkungen. [Bedienfeld und Menü](bedienfeld-menue.md) ·
[Einrichten und Fräsen mit gSender](cnc-gsender.md)

## Befehle eingeben

In gSender **Konsole** öffnen, einen Befehl eingeben und **Run/Enter**
drücken. Verbindung: **Grbl, 115200 Baud**. Zahlen mit **Dezimalpunkt**
schreiben, beispielsweise `0.5`, nicht `0,5`. Bei mehreren Beispielen jede
Zeile einzeln senden. Bewegungsbeispiele setzen freie Fahrwege voraus.

**`$$` sind zwei Dollarzeichen, keine Paragraphenzeichen `§§`.** Zum Lesen
der Einstellungen `$$` senden. `$101` allein ist kein gültiger Lesebefehl;
`$101=20480` schreibt einen neuen Wert. Im adaptiven Profil beschreiben
die Schritte/mm ein festes virtuelles 1/256-Raster. Alte Werte 320, 1280
oder 2560 nicht zurückspielen; die Firmware skaliert sie beim Update.

`ok` bedeutet angenommen bzw. gepuffert, nicht zwingend fertig gefahren.
Der Echtzeitstatus `?` zeigt den Zustand; nach normalen Fahrten steht dort
wieder `Idle`. Die Spindel wird manuell bedient: **`M5`, Pause und Reset
schalten die physische Spindel nicht aus.**

## Die wichtigsten Befehle

| Befehl | Wirkung |
| --- | --- |
| `$HY` | **Nur Y referenzieren**, zum Y-Min-Schalter |
| `$MS` | Virtuelles Raster, aktuelles Hardware-Mikroprofil und Geschwindigkeits-/Pulsbudget lesen; keine Bewegung |
| `$HX` | Nur X referenzieren, zum X-Min-Schalter |
| `$HZ` | Nur Z referenzieren, zum Z-Max-Schalter |
| `$H` | Alle referenzieren: erst Z-Max, danach X-Min und Y-Min gemeinsam |
| `$X` | Startsperre/Alarm für manuelle Einrichtung lösen, wenn Schalter frei und Treiber bereit sind; stellt keine Referenz her |
| `$LR` | Im Alarm genau einen aktiven Endschalter begrenzt freifahren; Ablauf unten beachten |
| `?` | Zustand, Position und Vorschub abfragen, ohne Bewegung |
| `$DRV` | RF2000-Treiber und vier Endschaltereingänge diagnostizieren |
| `$$` | Alle aktuellen Einstellungen anzeigen |
| `$I` | Firmwareversion und Buildinformation anzeigen |
| `$G` | Aktive G-Code-Modi anzeigen, z. B. G21/G90/G54 |
| `$#` | Arbeitsnullpunkte G54–G59, G28/G30, G92 und Werkzeuglängenoffset anzeigen |
| `$` | Kurze eingebaute Hilfe anzeigen |

`$H` und die Einzelachsenbefehle benötigen `$22=1` und freie Eingänge.
Die Fahrtrichtungen oben gelten für die Projektvorgabe `$23=3`. Im CNC-Profil
ist die Suche wegbegrenzt; das getrennte Y-Testprofil sucht zunächst fortlaufend.
Y ist am Gerät bestätigt; X/Z-Richtung, Fahrweg und Schalter sind noch zu prüfen.

Bei freien Eingängen und bereiten Treibern zeigt `$DRV` unter anderem
`READY=1|FAULT=0|READBACK=0|LIMITS=0`. Bei `LIMITS` bedeutet 1=X-Min,
2=Y-Min, 4=Z-Min, 8=Z-Max; mehrere betätigte Eingänge addieren ihre Werte.
`STATUS` enthält die beim Treiberstart gelesenen Registerwerte, keine laufende
SPI-Abfrage.

`$MS` antwortet beispielsweise mit
`[MS:VIRTUAL=256|MICRO=256|MAX=1000|PULSE_MAX=12000]` und `ok`.
`MICRO` gibt den Nenner der aktuell bestätigten Hardware-Stufe an, also
256 für 1/256 oder 8 für 1/8; 0 bedeutet unbekannt. `VIRTUAL` bleibt 256,
`MAX` ist die Feedgrenze in mm/min und `PULSE_MAX` das Pulsbudget in STEP/s.
Die Abfrage startet weder SPI noch eine Bewegung.

## Achsen einzeln fahren: Joggen

Für manuelle Fahrten bevorzugt **`$J=`** verwenden. Diese Befehle verändern
die normalen G-Code-Modi nicht. `G21` bedeutet Millimeter, `G91` bedeutet
relativer Weg und `F20` bedeutet 20 mm/min.

| Befehl | Bewegung |
| --- | --- |
| `$J=G21G91X1F20` | X um 1 mm positiv |
| `$J=G21G91X-1F20` | X um 1 mm negativ |
| `$J=G21G91Y1F20` | Y um 1 mm positiv |
| `$J=G21G91Y-1F20` | Y um 1 mm negativ |
| `$J=G21G91Z0.5F20` | Z um 0,5 mm positiv |
| `$J=G21G91Z-0.5F20` | Z um 0,5 mm negativ |
| `$J=G21G90X10F20` | X zur absoluten Arbeitsposition 10 mm |

Jede Jogzeile benötigt ein `F`. Zulässig sind XYZ, G20/G21, G90/G91 und
optional G53; **kein G1 in die `$J=`-Zeile einfügen**. Die Achsmaxima begrenzen
das Tempo zusätzlich. Beispiel `Y3000` bedeutet einen Weg von **3000 mm**,
nicht eine Geschwindigkeit. A/B sind Reserve und nicht fahrbar.

## Pause, Fortsetzen und Abbruch

Echtzeitbefehle wirken direkt; die Firmware wartet dafür nicht auf ein
Zeilenende. In gSender können die entsprechenden Bedienknöpfe verwendet werden.

| Befehl / Bedienung | Wirkung |
| --- | --- |
| `!` / Pause | Normale Programmmotion kontrolliert anhalten; beim Joggen verbleibende Jogfahrt abbrechen |
| `~` / Fortsetzen | Angehaltene normale Programmmotion fortsetzen; abgebrochener Jog wird nicht fortgesetzt |
| **Reset** / Ctrl-X / Byte `0x18` | Bewegung abbrechen, Puffer verwerfen, Software neu starten; Referenz danach neu prüfen |
| Jog-Abbruch / Byte `0x85` | Jog abbremsen und restliche Jogbewegungen verwerfen; bricht auch `$LR` ab |
| `?` | Echtzeitstatus abfragen |

**Eine Referenzfahrt mit Reset abbrechen.** `!` und Jog-Abbruch stoppen die
normale Referenzfahrt nicht. Beim Freifahren `$LR` bricht dagegen auch `!` ab.
Am Bedienfeld wirkt Pause II beim Referenzieren/Freifahren sofort als Reset.

`Ctrl-X` ist ein Steuerzeichen, kein ausgeschriebener Konsolentext. Ebenso
bedeutet `0x85` **ein einzelnes Rohbyte**, nicht die Zeichenfolge `0x85`.
Für diese Funktionen die gSender-Schaltflächen oder eine Software verwenden,
die die betreffenden Bytes senden kann.

## Arbeitsnullpunkte und Koordinaten

Referenzieren bestimmt den Maschinenbezug. Arbeitsnull legt fest, wo sich
das Werkstück befindet. Das sind unterschiedliche Vorgänge.
Für die Millimeterbeispiele zuerst `G21` senden; mit `$G` lassen sich die
aktiven Modi prüfen.

| Befehl | Wirkung ohne Achsfahrt |
| --- | --- |
| `G54` bis `G59` | Eines der sechs Arbeitskoordinatensysteme wählen |
| `G10L20P1X0Y0Z0` | Aktuelle XYZ-Position als Nullpunkt von **G54** speichern |
| `G10L20P1Y0` | Nur Y in G54 an der aktuellen Position auf null setzen |
| `G10L20P1Z0` | Nur Z in G54 an der aktuellen Position auf null setzen |
| `G10L20P0X0Y0Z0` | Aktuelle Position im **gerade aktiven** Arbeitssystem nullen |
| `G10L2P1X10Y20Z0` | G54-Offset direkt vorgeben; Werte sind Offsets, keine Fahrziele |
| `G92X0Y0Z0` | Zusätzlichen temporären Koordinatenversatz setzen |
| `G92.1` | Temporären G92-Versatz löschen |
| `G43.1Z10` | Dynamischen Werkzeuglängenoffset für Z auf 10 mm setzen |
| `G49` | Werkzeuglängenoffset löschen |

Bei G10 bedeutet P1=G54, P2=G55, …, P6=G59 und P0=aktives System.
G54–G59 und gespeicherte G28/G30-Positionen bleiben im EEPROM erhalten.
G92 und G43.1 sind temporär. G92 und Werkzeuglängenoffset beeinflussen die
angezeigte Arbeitsposition zusätzlich zum gewählten Arbeitssystem.

## Normale G- und M-Befehle

Diese Befehle werden bei normalen Fräsprogrammen verwendet. G-Code-Modi
bleiben wirksam, bis sie geändert oder zurückgesetzt werden. Bei manuellem
Joggen ist `$J=` deshalb meist einfacher.
Die Bewegungsbeispiele dieser Tabelle setzen `G21 G90 G94` voraus:
Millimeter, absolute Ziele und Vorschub pro Minute.

| Befehl | Bedeutung / Beispiel |
| --- | --- |
| `G0` | Eilfahrt mit den eingestellten Achsmaxima; `G0X10` fährt zur X-Zielposition |
| `G1` | Gerade mit Vorschub; `G1X10F20` |
| `G2` / `G3` | Kreisbogen im / gegen den Uhrzeigersinn; Mittelpunktversatz I/J/K oder Radius R |
| `G4P1` | Eine Sekunde warten; P wird in Sekunden angegeben |
| `G17` / `G18` / `G19` | Kreisbogenebene XY / XZ / YZ |
| `G20` / `G21` | Zoll / Millimeter als Programmeinheit |
| `G90` / `G91` | Absolute Zielpositionen / relative Wege |
| `G91.1` | Relative Kreismittelpunktversätze; I/J/K beziehen sich auf den Bogenstart |
| `G93` / `G94` | Inverse Zeitvorgabe / üblicher Vorschub pro Minute; für G93 benötigt jede Fahrzeile F |
| `G53` | Nur diese Zeile in Maschinenkoordinaten fahren; nur zusammen mit G0/G1 |
| `G28.1` / `G30.1` | Aktuelle Maschinenposition als erste / zweite Parkposition speichern |
| `G28` / `G30` | Gespeicherte Position anfahren; mit Achswörtern entsteht zunächst eine Zwischenfahrt |
| `G40` | Fräserradiuskorrektur aus; G41/G42 sind nicht verfügbar |
| `G61` | Unterstützte Bahnsteuerungsangabe; übliche GRBL-Bahnplanung bleibt wirksam |
| `G80` | Modale Fahrfunktion abwählen; kein Bohrzyklus |
| `M0` | Programmpause; Fortsetzen über `~` bzw. gSender |
| `M1` | Wird angenommen, aber als optionaler Halt **ignoriert** |
| `M2` / `M30` | Programmende; mehrere modale Zustände werden auf Vorgaben zurückgesetzt |
| `M3` / `M4` / `M5` | Spindelzustand nur im Programm führen; **keine physische Spindelschaltung** |
| `M9` | Kühlmittel aus wird angenommen; keine Kühlmittelausgänge zugeordnet |
| `F20` | Vorschub 20 mm/min bei G21/G94 |
| `S1000` | Logischer Spindel-Sollwert; verändert die manuelle Spindeldrehzahl nicht |
| `T1` | Werkzeugnummer nur merken, Bereich 0–255; kein Werkzeugwechsel |
| `N10` | Optionale Zeilennummer, Bereich 0–10000000 |

G10, G43.1/G49, G54–G59 und G92/G92.1 stehen in der Koordinatentabelle oben.
**G28/G30 sind keine Referenzbefehle.** Sie fahren zu gespeicherten
Positionen und können mehrere Achsen bewegen. Zum Referenzieren `$H` bzw.
`$HX/$HY/$HZ` verwenden.

Kommentare sind mit `; Kommentar` oder `(Kommentar)` möglich. Für Fräsdateien
einen GRBL-Postprozessor mit XYZ verwenden. Nicht eingerichtet sind unter
anderem G38-Probing, G41/G42, G64, G90.1, Bohrzyklen G81 usw., M6,
M56, M7/M8, M62–M65, M67/M68, Makros und Unterprogramme. Heizungs-/Extruder-
befehle aus Repetier/Marlin sind keine Befehle dieser CNC-Firmware.

## Einstellungen lesen und ändern

**`$$` zeigt die tatsächlich gespeicherten Werte.** Die folgenden Werte
sind die CNC-Factory-Vorgaben. Die Migration von Version 74 oder 75 rechnet
auf das virtuelle 1/256-Raster um und erhält niedrigere Tempi; sie erhöht sie
nicht automatisch.
Schreiben mit `$Nummer=Wert`, z. B. `$111=100`. Nur im Stillstand
ändern; diese Befehle schreiben ins EEPROM.

| Einstellung | Vorgabe | Bedeutung / Einschränkung |
| --- | ---: | --- |
| `$0` | 10 | STEP-Pulsdauer in µs, nur ganze Werte 10–20 |
| `$1` | 255 | Motorhaltezeit; 255 hält dauerhaft, 0–254 sind ms bis Abschalten |
| `$2` | 0 | STEP-Invertierung, auf dieser Platine fest 0 |
| `$3` | 6 | DIR-Invertierung; Bitmaske X=1, Y=2, Z=4 |
| `$4` | 0 | Enable-Invertierung, fest 0 |
| `$5` | 0 | Endschalterinvertierung, fest 0; Hardwareeingänge sind LOW-aktiv |
| `$6` | 0 | Probe-Invertierung; mangels zugeordnetem Probe-Eingang ohne Nutzfunktion |
| `$10` | 1 | Statusmaske: Bit 0 (Wert 1) wählt MPos statt WPos; Bit 1 (Wert 2) ergänzt Pufferdaten; z. B. 3=MPos mit Pufferdaten |
| `$11` | 0.010 | Junction Deviation in mm, beeinflusst Übergänge zwischen Fahrsegmenten |
| `$12` | 0.002 | Kreisbogen-Toleranz in mm |
| `$13` | 0 | Positionsausgabe in Zoll: 0=mm, 1=Zoll; ändert nicht die Programmeinheit G20/G21 |
| `$20` | 0 | Soft Limits 0/1; erst nach geprüftem Maschinenbezug und korrekten Fahrwegen verwenden |
| `$21` | 1 | Hard Limits, auf dieser Platine fest eingeschaltet |
| `$22` | 1 | Referenzfahrt freigeben 0/1; Abschalten deaktiviert auch Soft Limits |
| `$23` | 3 | Homing-Richtungsmaske X=1, Y=2, Z=4; 3 bedeutet hier X/Y-Min, Z-Max |
| `$24` | 20 | Langsames Referenztempo in mm/min, höchstens 1000 |
| `$25` | 100 | Suchtempo beim Referenzieren in mm/min, höchstens 1000 |
| `$26` | 250 | Referenzschalter-Entprellpause in ms; diese Implementierung übernimmt 0–255 |
| `$27` | 2 | Rückzug vom Referenzschalter in mm |
| `$30` | 1000 | Logischer maximaler Spindel-Sollwert; keine physische Drehzahlsteuerung |
| `$31` | 0 | Logischer minimaler Spindel-Sollwert |
| `$32` | 0 | Lasermodus, fest ausgeschaltet |
| `$100` / `$101` / `$102` | 20480 / 20480 / 20480 | Virtuelle Einheiten/mm im festen 1/256-Raster; Hardware-Stufe wechselt automatisch |
| `$110` / `$111` / `$112` | 600 / 600 / 300 | Achsmaximum in mm/min, höchstens 1000; niedrigere migrierte Werte bleiben erhalten |
| `$120` / `$121` / `$122` | 10 / 10 / 10 | Beschleunigung X / Y / Z in mm/s² |
| `$130` / `$131` / `$132` | 200 / 290 / 185 | Verfahrweg X / Y / Z in mm; keine automatische Vermessung |

Für Achseinstellungen sind positive Werte erforderlich. Es gibt keine
Achseinstellungen für A/B. Hardwarefeste Werte mit anderen Werten werden
abgewiesen. Die Richtungswerte `$3`/`$23` nur nach Prüfung am Gerät ändern.

Das adaptive CNC-Profil begrenzt Achs- und Bahnfeed auf **1000 mm/min**.
Die Hardware-Stufe 1/256 bis 1/8 wird mit einem physischen Pulsbudget von
12 kHz gewählt; bei 1000 mm/min und 1/8 sind nominal 10.666,7 STEP/s nötig.
`$100`–`$102` bleiben dabei konstant. `$MS` liest die aktuelle Auswahl.
Die getrennten Standard-/Y-Testprofile behalten ihre feste Auflösung und
5-kHz-Grenze. [Automatik und Grenzen](adaptive-mikroschritte.md).

Nach dem Update `$I`, `$$` und `$MS` prüfen. CNC-Version 75 skaliert X/Y ×64,
Z ×16; Version 74 skaliert X/Y ×8 und Z ×16. Das ergibt nominal
**20480/20480/20480**. Feinabgleiche werden proportional erhalten; bekannte
überholte X/Y-Werte 152,380 werden gesondert korrigiert. Die neue Version 76
wird erst nach vollständigem globalem Schreibvorgang gültig. Erkannte
unterbrochene CNC-Datensätze stellen globale Vorgaben wieder her und
bewahren getrennte Koordinaten-/Startdatensätze.

**Alte Werte 320, 1280 oder 2560 nicht zurückspielen.** Vollständiger Ablauf:
[EEPROM-Migration](cnc-gsender.md#bestehende-cnc-einstellungen-umstellen).
Die bestätigten alten 100/100/150-Tempi bleiben beim gültigen Update erhalten.
Nach langsamer Wegprüfung können diese neuen Startvorgaben gewählt werden:

```text
$110=600
$111=600
$112=300
$120=10
$121=10
$122=10
$$
```

Für die allerersten Messungen 100/100/150 und kleine F20-Jogs verwenden;
höhere Werte nur nach dem [Hardwaretestplan](geschwindigkeit-optimieren.md).
Die Hardware-Mikroauflösung wird automatisch von der Firmware gewählt.
`$100`–`$102` beschreiben die feste virtuelle Kalibrierung; sie werden nicht
pro Hardware-Stufe neu gesetzt.

## Weitere Systembefehle

| Befehl | Wirkung |
| --- | --- |
| `$D` | Digitale I/O-Statusabfrage; hier keine nutzbaren Zusatz-I/Os zugeordnet, `$DRV` ist für die RF2000-Diagnose maßgeblich |
| `$C` | G-Code-Prüfmodus ein-/ausschalten; prüft normale G-Code-Zeilen ohne Achsfahrt; beim Ausschalten erfolgt Reset |
| `$SLP` | Schlafmodus aus Stillstand/Alarm; Aufwecken durch Reset |
| `$N` | Beide gespeicherten Startzeilen anzeigen |
| `$N0=G21G90G54` | Startzeile 0 speichern; wird zur Prüfung sofort ausgeführt |
| `$N1=...` | Startzeile 1 entsprechend speichern |
| `$N0=` / `$N1=` | Jeweilige Startzeile löschen |
| `$I=RF2000` | Eigenen Build-Infotext speichern, maximal 79 Zeichen |
| `$RST=$` | Einstellungen auf Firmwarevorgaben zurücksetzen; danach Reset |
| `$RST=#` | Gespeicherte Koordinatenparameter zurücksetzen; danach Reset |
| `$RST=*` | Alle GRBL-Daten zurücksetzen, einschließlich Einstellungen, Nullpunkten, Startzeilen und eigenem Build-Infotext |

**`$RST`-Befehle löschen die genannten Daten**, deshalb vorher `$$`, `$#`,
`$N` und `$I` sichern. Zum üblichen Firmwareupdate sind sie nicht nötig.
Startzeilen nur für Modi verwenden, nicht für Bewegungen oder EEPROM-
Schreibbefehle. Nach erfolgreichem vollständigem `$H` werden sie automatisch
ausgeführt, nicht nach einem Einzelachsen-Homing oder `$X`. Dieses RF2000-
Profil startet auch nach Reset immer verriegelt im Alarmzustand.

## Zusätzliche Echtzeitbytes für Sendersoftware

Diese Tabelle ist für Makros/Programme gedacht, die **Rohbytes** senden
können. Die Zeichenfolge `0x90` in der Konsole sendet diesen Befehl nicht.

| Byte | Wirkung |
| --- | --- |
| `0x84` | Virtuelle Schutztür anfordern; kein physischer Türeingang/keine automatische Parkbewegung, keine Abschaltung der manuellen Spindel |
| `0x85` | Jog-Abbruch; auch Abbruch von `$LR` |
| `0x90` | Vorschubkorrektur auf 100 % |
| `0x91` / `0x92` | Vorschubkorrektur +10 / −10 Prozentpunkte |
| `0x93` / `0x94` | Vorschubkorrektur +1 / −1 Prozentpunkt |
| `0x95` / `0x96` / `0x97` | Eilfahrtkorrektur auf 100 / 50 / 25 % |
| `0x99` | Logische Spindelkorrektur auf 100 % |
| `0x9A` / `0x9B` | Logische Spindelkorrektur +10 / −10 Prozentpunkte |
| `0x9C` / `0x9D` | Logische Spindelkorrektur +1 / −1 Prozentpunkt |
| `0x9E` | Logischen Spindelstopp während normaler Programmpause umschalten |
| `0xA0` / `0xA1` | Logischen Kühlmittelzustand umschalten; keine physischen Ausgänge |

Vorschubkorrektur ist in diesem Projekt auf **10–100 %** begrenzt und wirkt
nicht auf Joggen, Referenzfahrt oder `$LR`. Spindel-/Kühlmittelbytes steuern
bei dieser Hardwarekonfiguration keine Geräte.

## Wenn ein Befehl abgewiesen wird

| Meldung | Typische Bedeutung hier |
| --- | --- |
| `error:1` | Unpassende Eingabe, z. B. kein gültiger Befehlsbuchstabe |
| `error:3` | Ungültige Befehlsform, z. B. `$101` ohne `=Wert` |
| `error:5` | Funktion/Einstellung in diesem Profil deaktiviert |
| `error:8` | Befehl passt nicht zum aktuellen Betriebszustand; Stillstand abwarten |
| `error:9` | Alarm/Hardware sperrt die Aktion; `?` und `$DRV` prüfen |
| `error:12` | Einstellung überschreitet CNC-Maximum 1000 mm/min bzw. das physische Pulsbudget; getrennte Testprofile: 5 kHz; allgemeine Sendertexte können noch 30 kHz nennen |
| `error:20` | G-/M-Befehl nicht unterstützt |
| `ALARM:1` | Endschalter im normalen Betrieb ausgelöst |
| `ALARM:3` | Bewegung durch Reset abgebrochen; Positionsbezug ungesichert |
| `ALARM:6` | Referenz-/Freifahrablauf abgebrochen |
| `ALARM:8` | Schalter nach Rückzug noch aktiv |
| `ALARM:9` | Referenzkontakt beim Anfahren innerhalb des Suchwegs nicht erreicht |
| `ALARM:10` | Konfigurierter Homing-Suchweg zu klein |
| `ALARM:11` | Empfangspuffer überlaufen; Übertragung/Sendersoftware prüfen |
| `ALARM:12` | DRV8711-Treiber-/Konfigurationsfehler; Ursache prüfen, Softreset löscht den verriegelten Treiberfehler nicht |

`error:12` und `ALARM:12` sind unterschiedliche Meldungen.
Ein Startalarm ohne konkrete Fehlernummer fordert zunächst Referenzieren
oder bewusste Freigabe bei freien Eingängen an.

Nach einem Hard Limit: Job beenden, Spindel manuell ausschalten, **Reset**,
mit `$DRV` genau einen aktiven Schalter prüfen, dann **`$LR`**. Der Rückzug
beträgt höchstens 2 mm mit bis zu 20 mm/min; danach bleibt die Steuerung im
Alarm. Neu referenzieren und Werkstücknull prüfen. Vollständiger Ablauf:
[Endschalter freifahren](cnc-gsender.md#nach-einem-hard-limit-freifahren).

## Grundlage

Die Zuordnung wurde gegen [system.c](../grbl/system.c),
[gcode.c](../grbl/gcode.c), [serial.c](../grbl/serial.c),
[settings.c](../grbl/settings.c), [config.h](../grbl/config.h) und
[defaults.h](../grbl/defaults.h) dieses Projekts geprüft. Die allgemeine
[GRBL-Befehlsreferenz](../doc/markdown/commands.md) beschreibt auch Funktionen,
die im RF2000-Profil bewusst nicht vorhanden sind.
