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
kann der Y-Test ohne seriellen Sender bedient werden. Bleiben trotz bestätigtem
Y-Profil schwarze Kästchen sichtbar, ist die LCD-Funktion am Gerät noch zu
prüfen; sie wurde bisher ausschließlich simuliert.

## Tasten und Anzeigen

| Taste | Funktion |
| --- | --- |
| Hoch / Runter | Menüpunkt auswählen; eine Betätigung pro vollständigem Drücken und Loslassen |
| OK | Ausgewählten Punkt ausführen oder Referenzstart bestätigen |
| Rechts | Diagnose anzeigen / zurück zum Menü |
| Links | Sofortiger Software-Abbruch und GRBL-Reset |
| Pause und Continue | Beide sofortiger Software-Abbruch und GRBL-Reset; keine Wiederaufnahme |
| Vier bisherige Z-/Extrudertasten | Ohne Bewegungsfunktion |

Beim Einschalten alle Tasten loslassen. Die Eingaben werden mit 20 ms
entprellt; gehaltene Tasten wiederholen keine Aktion. Bewegungsbefehle
während einer laufenden Aktion werden verworfen. STOP wirkt bereits beim
ersten LOW-Sample der 1-kHz-Abfrage, ohne die Entprellzeit abzuwarten.
Eine gehaltene STOP-Taste sperrt auch neue Bewegungen über die serielle
Schnittstelle. Nach Abbruch ist erneut zu entriegeln; nichts fährt selbst an.

Das Display zeigt ALARM, BEREIT, JOG oder die aktuelle Referenzphase sowie
`DRV:OK` / `DRV:FEHL` und `YMIN:frei` / `YMIN:aktiv`. Der Startalarm ist
beabsichtigt. In der Diagnose bedeuten X/Y/Z-/Z+ jeweils `1` für betätigt
und `0` für frei. `F` ist die DRV-Fehlerursache, `R` die Readbackfehler-Maske
und `A` die letzte Alarmnummer; diese Zahlen erscheinen dezimal.
`Befehl gesperrt E…` zeigt einen GRBL-Statusfehler, keine ausgeführte Bewegung.
Hoch/Runter wählt anschließend wieder einen Menüpunkt.

## Erster Y-Test am Bedienfeld

Die elektrischen Prüfungen und Voraussetzungen aus
[y-axis-test.md](y-axis-test.md) gelten unverändert. Ausschließlich Y wird
freigegeben; X/Z/A/B bleiben gesperrt. Die physische Richtung und die
Schalterfunktion müssen vor der Referenzfahrt geprüft sein.

1. Nach dem Start alle Tasten loslassen. `DRV:OK` und bei freiem Schalter
   `YMIN:frei` prüfen. Den Y-Schalter von Hand betätigen: Die Anzeige muss
   auf `aktiv` wechseln und nach Loslassen wieder `frei` werden. Mit Rechts
   lassen sich die anderen Eingänge ebenfalls ohne Bewegung prüfen.
2. **Entriegeln → OK**. Aktive Schalter oder Treiberfehler verhindern die
   Freigabe. Bei einem kritischen Alarm zuerst mit Links zurücksetzen;
   ein verriegelter Treiberfehler erfordert nach Ursachenbehebung einen
   echten MCU-Neustart.
3. **Y -0.1 mm → OK**: genau ein kurzer Jog mit 20 mm/min. Y- muss zum
   Min-Schalter zeigen. **Y +0.1 mm → OK** prüft die Gegenrichtung.
   Vor jeder weiteren Aktion auf das Ende der Bewegung warten.
4. Bei bestätigter Richtung und Schalterfunktion sowie freien Eingängen
   **Y referenzieren → OK** wählen. Das Display fragt nach Richtung und
   Schalterprüfung. **OK vollständig loslassen und erneut drücken** startet
   die Fahrt. Hoch/Runter/Rechts verwirft diese Bestätigung. Eine zwischenzeitlich
   über USB gestartete Bewegung verwirft sie ebenfalls.
5. Die Anzeige wechselt durch **REF-SUCHE → RUECKZUG → FEINANFAHRT →
   FREIFAHREN → BEREIT**. Der Ablauf entspricht den seriellen Befehlen
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
