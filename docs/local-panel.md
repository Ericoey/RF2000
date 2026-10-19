# Y-Test mit originalem Display und Tastenfeld

Das Profil **`rf2000_y_homing_test`** unterstützt das originale RF2000v2-
HD44780-Display mit 20×4 Zeichen und das vorhandene Tastenfeld. Pinbelegung,
4-Bit-Ansteuerung und Initialisierungsfolge stammen aus der bereitgestellten
Repetier-Referenz. Die Bedienoberfläche ist ein eigenes kleines GRBL-Testmenü.
Es werden keine Drucker-, Heizungs- oder Extrudermenüs übernommen.
Dieses Y-Profil ist die Standardauswahl für PlatformIO-Build und -Upload.
Das ausdrücklich auswählbare XYZ-Profil `rf2000_grbl` aktiviert dieses
Bedienfeld nicht.

## Bauen

Repository aktualisieren und im Hauptordner kompilieren:

```bash
git pull
pio run
```

In VS Code: **Project Tasks → rf2000_y_homing_test → General → Build**.
Ergebnis: `.pio/build/rf2000_y_homing_test/firmware.hex`.
Ein Build überträgt keine Firmware. Zum Aufspielen den seriellen Monitor
schließen und beispielsweise mit dem bekannten Mac-Port ausführen:

```bash
pio run -t upload --upload-port /dev/cu.usbserial-AL03P2K9
pio device monitor --port /dev/cu.usbserial-AL03P2K9
```

Den Port gegebenenfalls ersetzen. Ohne `-e` wird das Y-Testprofil gebaut und
aufgespielt; `-e rf2000_y_homing_test` wählt es ausdrücklich. Nach dem Neustart
muss die serielle Startmeldung **`RF2000 Y-only test`** enthalten.
**`RF2000 XYZ test profile`** zeigt das Profil ohne Display an. Die Auswahl
im Monitor allein ändert keine aufgespielte Firmware. Nach dem Programmieren
kann der Y-Test ohne seriellen Sender bedient werden. Am 19.10.2026 hat der
Nutzer bestätigt, dass Display, alle Tasten, Y-Referenzfahrt und Referenzschalter
mit dem bisherigen Testmenü am Gerät funktionieren. Die hier beschriebene
neue Menüführung baut auf dieser Ansteuerung auf; die Cloud-Prüfungen ersetzen
keinen Test ihrer Bedienung an der realen Platine.

## Tasten und Anzeigen

| Taste | Funktion |
| --- | --- |
| Hoch / Runter | Im Hauptmenü einen Punkt wählen; unter **Y verfahren** Y+ / Y− bewegen |
| OK | Im Hauptmenü auswählen; unter **Y verfahren** Schrittweite wechseln; Referenzstart bestätigen |
| Rechts | Im Hauptmenü Diagnose öffnen; aus Unterseiten zurück zum Hauptmenü |
| Links | Sofortiger Software-Abbruch und GRBL-Reset |
| Pause und Continue | Beide sofortiger Software-Abbruch und GRBL-Reset; keine Wiederaufnahme |
| Bisherige Z+ / Z− (PJ2 / PJ4) | Nur unter **Y verfahren** Schrittweite größer / kleiner einstellen |
| Bisherige Extrudertasten (PJ5 / PJ6) | Nur unter **Y verfahren** Vorschub größer / kleiner einstellen |

Das Hauptmenü enthält **Freigeben**, **Y verfahren**, **Y referenzieren**,
**Diagnose** und **Abbruch / Reset**. Hoch/Runter wählt den Punkt; OK öffnet
ihn oder führt ihn aus. Der Einstieg in **Y verfahren** und Änderungen von
Schrittweite oder Vorschub bewegen den Motor nicht. Rechts kehrt aus
Verfahren oder Diagnose zum Hauptmenü zurück. In der Diagnose geht auch OK
zurück. Links bleibt auf jeder Seite eine Abbruchtaste.

Unter **Y verfahren** startet jeder Druck auf Hoch genau einen positiven
Y-Jog, jeder Druck auf Runter einen negativen. OK wechselt zyklisch zwischen
**0,01 / 0,10 / 0,50 mm**; die bisherigen Z-Tasten stellen diese Stufen
ohne Umlauf größer oder kleiner. Die bisherigen Extrudertasten wählen
**10 / 20 / 30 mm/min**, ebenfalls mit festen Grenzen. Nach jedem Reset
gelten wieder **0,10 mm und 20 mm/min**. Diese Auswahl liegt nur im RAM;
sie verändert weder EEPROM noch die Referenzgeschwindigkeiten. Die vier
Zusatzknöpfe haben außerhalb dieser Seite keine Aktion und bewegen keine
Z- oder Extruderachse.
Bei 152,38 Schritten/mm sind 0,01 mm nur etwa ein bis zwei Mikroschritte;
die tatsächliche Strecke wird auf ganze Schritte gerundet.

Beim Einschalten alle Tasten loslassen. Die Eingaben werden mit 20 ms
entprellt; gehaltene Tasten wiederholen keine Aktion. Bewegungsbefehle
während einer laufenden Aktion werden verworfen. STOP wirkt bereits beim
ersten LOW-Sample der 1-kHz-Abfrage, ohne die Entprellzeit abzuwarten.
Eine gehaltene STOP-Taste sperrt auch neue Bewegungen über die serielle
Schnittstelle. Nach Abbruch ist erneut zu entriegeln; nichts fährt selbst an.

Das Display zeigt ALARM, BEREIT, JOG oder die aktuelle Referenzphase. Die
Y-Zeile enthält die Maschinenposition in mm und den Referenzstatus:
`R:?` bedeutet nicht referenziert, `R:JA` eine in dieser Sitzung erfolgreich
abgeschlossene Referenzfahrt. Ein Reset oder Alarm löscht diesen Status.
Während der Referenzfahrt stehen die Phase und `Y wird referenziert` im
Display; der interne, abschnittsweise zurückgesetzte Zähler wird dort nicht
als laufende Maschinenposition ausgegeben.
Vor einer Referenzfahrt ist die angezeigte Position nur ein Schrittzähler
ohne gesicherten mechanischen Bezug. Nach Y-Min-Homing liegt sie entsprechend
der GRBL-Maschinenkoordinaten bei ungefähr **−289 mm**, nicht bei null:
290 mm konfigurierter Verfahrweg mit 1 mm abschließendem Rückzug.

Im Hauptmenü und während Homing erscheinen außerdem `DRV:OK` / `DRV:FEHL`
und `YMIN:frei` / `YMIN:aktiv`. Auf der Verfahrseite stehen stattdessen
Schrittweite und Vorschub. Der Startalarm ist beabsichtigt. In der Diagnose
bedeuten X/Y/Z-/Z+ jeweils `1` für betätigt
und `0` für frei. `F` ist die DRV-Fehlerursache, `R` die Readbackfehler-Maske
und `A` die letzte Alarmnummer; diese Zahlen erscheinen dezimal.
`AKTION GESPERRT E…` zeigt einen GRBL-Statusfehler und einen Hinweis wie
`Erst freigeben` oder `Endschalter aktiv`. **OK oder Rechts quittiert nur
die Meldung**; erst ein weiterer Tastendruck kann eine Aktion auslösen.

## Erster Y-Test am Bedienfeld

Die elektrischen Prüfungen und Voraussetzungen aus
[y-axis-test.md](y-axis-test.md) gelten unverändert. Ausschließlich Y wird
freigegeben; X/Z/A/B bleiben gesperrt. Die physische Richtung und die
Schalterfunktion müssen vor der Referenzfahrt geprüft sein.

1. Nach dem Start alle Tasten loslassen. `DRV:OK` und bei freiem Schalter
   `YMIN:frei` prüfen. Den Y-Schalter von Hand betätigen: Die Anzeige muss
   auf `aktiv` wechseln und nach Loslassen wieder `frei` werden. Mit Rechts
   lassen sich die anderen Eingänge ebenfalls ohne Bewegung prüfen.
2. **Freigeben → OK**. Aktive Schalter oder Treiberfehler verhindern die
   Freigabe. Bei einem kritischen Alarm zuerst mit Links zurücksetzen;
   ein verriegelter Treiberfehler erfordert nach Ursachenbehebung einen
   echten MCU-Neustart.
3. **Y verfahren → OK**. Die Vorgabe ist 0,10 mm bei 20 mm/min.
   **Runter** startet genau einen Y−-Jog zum Min-Schalter; **Hoch** prüft
   die Gegenrichtung Y+. Vor jeder weiteren Bewegung auf deren Ende warten.
   Mit **Rechts** zurück zum Hauptmenü. Falls nötig, Schrittweite und
   Vorschub vor dem nächsten Jog über OK oder die Zusatzknöpfe ändern.
4. Bei bestätigter Richtung und Schalterfunktion sowie freien Eingängen
   **Y referenzieren → OK** wählen. Das Display fragt nach Richtung und
   Schalterprüfung. **OK vollständig loslassen und erneut drücken** startet
   die Fahrt. Hoch/Runter/Rechts verwirft diese Bestätigung. Eine zwischenzeitlich
   über USB gestartete Bewegung verwirft sie ebenfalls.
5. Die Anzeige wechselt durch **REF-SUCHE → RUECKZUG → FEINANFAHRT →
   FREIFAHREN → BEREIT**; anschließend steht neben der Y-Position **R:JA**.
   Der Ablauf entspricht den seriellen Befehlen
   `$22=1` und `$HY`: 60 mm/min erste Suche, 1 mm Rückzug, 20 mm/min
   zweite Annäherung, 1 mm abschließender Rückzug.

Die erste Suche läuft entsprechend dem gewünschten endlosen Prüfaufbau
ohne Gesamtweg- oder Zeitbegrenzung bis zum Y-Min-Kontakt. Die zweite
Annäherung ist auf 5 mm begrenzt. Bei einem handbetätigten Prüfschalter
beim ersten Rückzug loslassen, bei der zweiten Annäherung erneut betätigen
und beim abschließenden Rückzug wieder loslassen.

**Abbruch jederzeit mit Links, Pause oder Continue.** Diese Tasten wirken
wie der serielle GRBL-Reset Ctrl-X; sie ersetzen keinen unabhängigen
Hardware-Not-Aus. Ein dauerhaft betätigter Schalter wird nicht automatisch
freigefahren. Alarm 8 bedeutet Rückzugfehler, 9 fehlenden zweiten Kontakt,
6 Referenzabbruch und 12 Treiber-/Readbackfehler.

## Implementierung und Nachweise

`RF2000_LOCAL_PANEL` ist standardmäßig ausschließlich im Y-Testprofil aktiv.
LCD-Schreibzugriffe erfolgen portmaskiert in kurzen atomaren Abschnitten;
die gemeinsam genutzten Motor- und SPI-Portbits bleiben erhalten. Laufzeit-
Updates übertragen höchstens ein LCD-Byte je zwei Timer-Ticks im Vordergrund;
auch über Tickgrenzen liegt mindestens 1 ms zwischen Bytes.
Der Timer-ISR liest nur Tasten und führt gegebenenfalls den GPIO-Abbruch aus.
Auch die blockierende Referenzfahrt und der kritische Alarmzustand bedienen
die Displayaktualisierung. Menüs lösen Befehle ausschließlich an einer
freien Protokollgrenze über die vorhandenen GRBL-Prüfungen aus.

`make check-panel` prüft das tatsächliche AVR-ELF einschließlich LCD-Bus,
Tasten und Bewegungen; `make check-y-homing` prüft zusätzlich die seriellen
Y-Abläufe. Ergebnisse und Grenzen stehen in [validation.md](validation.md).
Eine reale Platine, ihr Display und ihre Tasten wurden in der Cloud nicht
getestet. Die belegte Pinzuordnung und bewusste Änderungen gegenüber
Repetier stehen in [hardware-verification.md](hardware-verification.md).
