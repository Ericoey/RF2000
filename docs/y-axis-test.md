# Separater Y-Achs- und Homingtest

Für den ersten Y-Test gibt es das zusätzliche Profil **`rf2000_y_homing_test`**.
Die Standardfirmware `rf2000_grbl` behält ihre Homing-Sperre. Die
Hardwarezuordnung folgt bei Quellenabweichungen der als funktionierend
angenommenen RF2000v2-Repetier-Konfiguration. Y verwendet STEP PF6, DIR PF7,
SLEEPn PF2 und Y-Min/ES2 PC0/D37 am Anschluss X14. Der Ablauf folgt dem
Referenzprinzip Anfahren → Rückzug → langsameres erneutes Anfahren →
abschließend freifahren; Geschwindigkeit und Rückzug sind bewusste Testwerte.

## Unterschiede zur Standardfirmware

| Eigenschaft | `rf2000_grbl` | `rf2000_y_homing_test` |
|---|---|---|
| Freigabefähige Motoren | XYZ | nur Y; X/Z/A/B mit CTRL.ENBL=0 und SLEEPn=0 |
| Referenzfahrt | gesperrt | ausschließlich `$HY`, nach `$22=1` in derselben Sitzung |
| Erste Referenzsuche | gesperrt | fortlaufend bis Y-Min-Kontakt, ohne Gesamtweg- oder Zeitbegrenzung |
| Jogging | XYZ | ausschließlich inkrementelles Y in mm, höchstens 0,5 mm pro Kommando und 30 mm/min |
| Normale G-Code-Bewegungen | XYZ-Testbetrieb | gesperrt; auch `$H`, `$HX` und `$HZ` sind gesperrt |
| Einstellungen | GRBL-Einstellungen mit Board-Grenzen | feste Testwerte; nur `$22=0` oder `$22=1` veränderbar |
| EEPROM | normale GRBL-Verwaltung | keine Übernahme gespeicherter Einstellungen oder Startprogramme, keine EEPROM-Schreibzugriffe |

Alle fünf DRV8711 werden auch im Y-Profil initialisiert und zurückgelesen.
Eine kurze gemeinsame Wake-Phase erfolgt dabei ausdrücklich mit gesperrten
H-Brücken, CTRL.ENBL=0 und ohne STEP. Danach kann nur Y freigegeben werden.
Die übrigen STEP-Ausgänge bleiben LOW. Heizungen, P230, Spindel- und sonstige
Leistungsausgänge bleiben deaktiviert. Homing-Freigabe und gespeicherte
Parameter einer anderen Firmware werden nicht übernommen.

## Bauen und verbinden

Im Repository-Hauptverzeichnis mit vorhandenem PlatformIO:

```bash
pio run -e rf2000_y_homing_test
```

In VS Code: **Project Tasks → rf2000_y_homing_test → General → Build**.
Das Image liegt unter `.pio/build/rf2000_y_homing_test/firmware.hex`; ELF und
MAP liegen daneben. `pio run` ohne `-e` baut weiterhin das Standardprofil.
Es sind keine zusätzlichen Bibliotheken oder Terminalpakete erforderlich.

Alternativ erzeugt `make y-homing-test` die Dateien
`artifacts/y-homing/rf2000-grbl.hex`, `.elf` und `.map`.
`make check-y-homing` baut dieses Profil und startet dessen AVR-Simulation.
Ein Build überträgt keine Firmware. Das passende Programmierverfahren und
der Rückweg zur vorhandenen Firmware gehören zum
[Hardwaretestplan](hardware-test.md).

Nach dem bewusst ausgeführten Programmieren des Y-Images den seriellen
Monitor öffnen:

```bash
pio device monitor -e rf2000_y_homing_test
```

Bei mehreren Schnittstellen zuerst `pio device list` ausführen und den
passenden Port angeben, zum Beispiel auf dem Mac:

```bash
pio device monitor -e rf2000_y_homing_test --port /dev/cu.usbserial-...
```

Den Beispielpfad durch den tatsächlich angezeigten Port ersetzen. Das Profil
stellt **115200 Baud, LF-Zeilenende und lokale Eingabeanzeige** ein. Ein anderer
Sender muss 115200 Baud und 8N1 verwenden. Andere Programme dürfen denselben
Port nicht gleichzeitig geöffnet haben. Das Öffnen kann die Platine über
DTR zurücksetzen; danach beginnt eine neue Sitzung mit `$22=0`.

## 1. Endschalter und Treiber ohne Motor prüfen

Die elektrischen Vorprüfungen aus [hardware-test.md](hardware-test.md)
gelten weiterhin. Motoren, Spindel und Heizer bleiben zunächst abgetrennt;
keine Motorstecker unter Spannung stecken. Z gegen Absinken sichern.
Die unabhängige Abschaltung muss erreichbar sein.

Im seriellen Monitor **`$DRV` eingeben und Enter drücken**. Die folgenden
Kommandos jeweils einzeln mit Enter abschicken. Der erwartete fehlerfreie
Ausgangspunkt ist `READY=1`, `FAULT=0`, `READBACK=0`, `LIMITS=0`.
Der beim Start angezeigte Alarm ist beabsichtigt und allein kein Treiberfehler.

Y-Min von Hand betätigen und erneut `$DRV` senden: Wenn alle anderen
Schalter frei sind, muss `LIMITS=2` erscheinen. Nach Loslassen muss wieder
stabil `LIMITS=0` erscheinen. PC0/D37 ist der Y-Endschaltereingang; er wird
nicht als Watchdog-Ausgang getaktet. Bei abweichender Polarität, instabilem
Signal, Readbackfehler, FAULT oder unerwartetem Neustart keine Bewegung starten.
Ein Treiberfehler kann durch `$X` oder einen Software-Reset nicht gelöscht werden.

## 2. Y-Richtung mit einem kleinen Jog prüfen

Erst nach bestandener elektrischer Prüfung spannungsfrei **nur den
Y-Motor** anschließen. X/Z/A/B bleiben abgetrennt. Motorstrom und Erwärmung
müssen zur vorhandenen Mechanik und zum Motor passen. Die übernommenen
TORQUE-Werte sind keine Ampereangaben; Y hat bei ISGAIN 20 und 0,033 Ω
rechnerisch etwa 1,79 A Wicklungs-Spitzenstrom, noch keinen gemessenen Strom.

Für den Jog alle vier Schalter freigeben und auf stabile Freigabe warten.
**Ctrl-X** sendet den GRBL-Software-Reset (Byte `0x18`, ohne Enter). Danach
zunächst `$DRV` prüfen und bei den oben genannten fehlerfreien Werten
**`$X` mit Enter** senden. `$X` dient dem Jog-Unlock und übergeht keinen
aktiven Eingang oder Treiberfehler.

Nur wenn der entsprechende 0,1-mm-Weg mechanisch frei ist, einzeln senden:

```text
$J=G91 G21 Y-0.1 F20
```

`Y-` muss sich tatsächlich **zum Y-Min-Schalter hin** bewegen. Mit `?` den
Status abfragen und vor dem nächsten Kommando auf `<Idle...>` warten.
Danach bei freiem Gegenweg:

```text
$J=G91 G21 Y0.1 F20
```

`Y+` muss sich vom Schalter entfernen. Bei falscher Richtung sofort abbrechen
und die Ursache klären. Keine Homing-Fahrt zur Richtungsbestimmung verwenden.
Die Firmware akzeptiert nur einen Jog im Leerlauf, maximal 0,5 mm und
30 mm/min. **Keine Befehlsfolgen vorpuffern oder streamen.** Die Begrenzung
gilt pro Kommando und begrenzt keine Summe wiederholter Jog-Kommandos.

## 3. Erst danach Y referenzieren

Voraussetzungen sind die dokumentiert richtige Y-Richtung, der gemessene
LOW-aktive Y-Min-Schalter und passender Motorstrom. Der Schalter muss zu
Beginn frei sein. Alle anderen Endschalter müssen ebenfalls frei sein.
Für den vom Nutzer beschriebenen mechanisch endlosen Prüfaufbau fährt die
erste Suche fortlaufend Richtung Y-Min, bis dessen Kontakt erkannt wird.
**Es gibt dabei keine Gesamtweg- oder Zeitbegrenzung. Ein ausbleibender
erster Kontakt stoppt diese Fahrt nicht automatisch.** Ein Software-Abbruch
erfolgt unmittelbar mit Ctrl-X (Byte `0x18`, ohne Enter); Treiberfehler und
andere Endschalter beenden die Fahrt ebenfalls.

Die erste Suche setzt wiederholt 5-mm-Bewegungsabschnitte fort, ohne
Gesamtweggrenze und ohne zusätzliche Entprellpause zwischen den Abschnitten.
An den Übergängen kann sie kurz abbremsen und wieder beschleunigen;
konstante Geschwindigkeit über die Abschnittsgrenzen wird nicht zugesichert.
Nach dem ersten Kontakt folgen Rückzug, langsame zweite Annäherung und
abschließender Rückzug. Die zweite Annäherung ist auf 5 mm begrenzt, damit
ein ausbleibendes erneutes Schaltsignal als Fehler erkannt wird. Für die
gesamte Referenzfahrt gibt es keine feste maximale Wegsumme.

Die Geschwindigkeiten und Rückzüge wurden gegenüber Repetier bewusst für
dieses Testprofil geändert; sie werden nicht als Originalwerte ausgegeben:

| Parameter | Bereitgestellte Repetier-Konfiguration, Y im Fräsmodus | Y-Testprofil |
|---|---|---|
| Erstes Anfahren | 70 mm/s = 4200 mm/min | 60 mm/min |
| Langsame erneute Annäherung | Suchtempo / 20 = 210 mm/min | 20 mm/min |
| Rückzug (`ENDSTOP_Y_BACK_MOVE`) | 5 mm | 1 mm je Rückzug |

Jetzt im Monitor nacheinander eingeben, jeweils mit Enter:

```text
$22=1
$HY
```

`$22=1` schaltet Y-Homing nur für die laufende Sitzung frei; jeder
Software- oder Hardware-Reset setzt es wieder auf 0. `$HY` darf bei freien
Eingängen aus Alarm oder Leerlauf starten. Ein vorheriges `$X` ist dafür
nicht erforderlich. Bei einem bereits betätigten Schalter wird der Start
abgewiesen, ohne automatische Freifahrt. Erst mechanisch sicher entlasten.

Erwarteter Ablauf: Y fährt zum Min-Schalter, zieht sich 1 mm zurück,
nähert sich langsam erneut und zieht sich abschließend 1 mm zurück.
Bei einem von Hand betätigten Prüfschalter: beim ersten Rückzug loslassen,
bei der langsamen zweiten Annäherung erneut betätigen und beim abschließenden
Rückzug wieder loslassen. Dauerhaftes Festhalten ergibt Rückzugfehler 8.
Nach Erfolg antwortet die Firmware mit `ok`; `?` zeigt Leerlauf. Ein nicht
erneut erreichter Schalter bei der zweiten Annäherung führt zu Homing-Alarm 9,
ein nicht freigegebener Schalter beim Rückzug zu Homing-Alarm 8. Ein vor dem
ersten Kontakt ausbleibendes Schaltsignal erzeugt keinen automatischen
Suchweg-Alarm. Ein anderer Endschalter oder Treiberfehler
beendet den Test. Ursache klären und Position erneut beurteilen.

**Abbruch:** Während Homing wirkt `!` nicht als Stopp. **Ctrl-X** sendet
den Software-Abbruch. **Ctrl-C schließt im PlatformIO-Monitor nur das
Terminal und stoppt keine Bewegung.** Bei Gefahr die unabhängige
Hardware-Abschaltung verwenden; eine serielle Verbindung ersetzt sie nicht.

Dieses Profil ist ein gesonderter Y-Inbetriebnahmetest. Es gibt weder X/Z-
Homing noch Fräsbetrieb frei. Es wurden in der Cloud keine reale Platine
programmiert, kein Motor bewegt und keine physische Referenzfahrt bestätigt.
