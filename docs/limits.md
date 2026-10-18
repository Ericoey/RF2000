# Endschalter, Freigaben und Referenzfahrt

Die RF2000-Plattform liest vier physische Eingänge direkt: X-Min PC2/D35,
Y-Min PC0/D37, Z-Min PC6/D31 und Z-Max PH1/D16. X-/Y-Max existieren nicht.
`rf2000_limits_raw_state()` bildet sie in genau dieser Reihenfolge auf Bits
0–3 ab. GRBLs `Pn:`-Status fasst beide Z-Schalter zum Achsenbit Z zusammen.

## Nachgewiesene elektrische Zuordnung

`references/repetier/Repetier/pins.h` im bereitgestellten RF2000v2-Abschnitt
nennt D35, D37, D31 und D16. `RF2000v2.h` aktiviert alle vier Pull-ups und
setzt `ENDSTOP_*_INVERTING` auf `true`. `Printer.h` vergleicht
`READ(pin) != ENDSTOP_*_INVERTING`; die wirksame Auslösung ist daher LOW.
Die neue Plattform behält diese Polarität und interne Pull-ups fest bei.
`$5` kann sie nicht umkehren; eine EEPROM-Fehleinstellung verändert die
Sicherheitsabfrage nicht.

Der bereitgestellte Schaltplan Blatt 2 zeigt an ES1/X13, ES2/X14 und ES4/X36
je 1 kOhm nach P5V, 100 Ohm zum Anschluss und 100 nF nach VSS. Der
vierpolige ES3/X15 führt P5V an Pins 1/2, ES3 an Pin 3 und VSS an Pin 4;
die externe optische Sensorlogik ist nicht abgebildet. Die interne
Pull-up-Beschaltung bleibt auch dort aktiv. Ein offener mechanischer
Kontakt bzw. unterbrochenes Kabel zieht diese Eingänge nicht automatisch
LOW. Der Schaltplan beweist weder die mechanische NO-/NC-Ausführung noch
eine Kabelbrucherkennung. Diese Firmware behauptet daher keine solche
Erkennung und ersetzt keinen unabhängigen Not-Aus.

## Stoppen und erneute Freigabe

Port C und PH1 besitzen am ATmega2560 keine geeigneten Pin-Change-Interrupts.
Timer2 läuft deshalb exklusiv im CTC-Modus mit 1 kHz (16 MHz / 64 / 250).
OC2A/OC2B sind abgekoppelt, es entsteht kein PWM-Ausgang. Jeder Poll liest
alle vier Eingänge und FAULTn. Ein aktiver Eingang wird beim ersten Sample
ausgewertet, ohne einen verzögernden Einschaltfilter. Die Abtastlatenz
beträgt bis zu einem Pollintervall zuzüglich laufender höher priorisierter
Interrupts; elektrische und mechanische Nachlaufwege müssen am realen
Aufbau gemessen werden.

Alle fünf nSLEEP-Ausgänge werden im Alarmfall unmittelbar per GPIO LOW,
anschließend GRBLs Reset und Alarm 1 (Hard Limit) bzw. Alarm 12
(Treiberfehler) ausgelöst. Es erfolgt keine SPI-Übertragung im Poll-ISR.
Auch im Leerlauf und bei einem bereits bestehenden Alarm bleiben die
Treiber bei aktiven Eingängen abgeschaltet. Wiederholte Alarme erzeugen
keine absichtliche Reset-Endlosschleife. `$21=0` schaltet den Board-Schutz
nicht ab. Ein erkannter Treiberfehler bleibt im Treibermodul gelatcht;
`$X` und ein Software-Reset beseitigen ihn nicht.

Nach einer Auslösung erfolgt die Freigabe erst nach fünf aufeinanderfolgenden
Polls mit allen Eingängen frei. Die Bewegungseintrittspunkte prüfen zusätzlich die
momentanen physischen Pegel und Treiberbereitschaft. `$X` wird bei einem
aktiven oder noch nicht stabil freigegebenen Eingang abgelehnt. Ein schon
beim Einschalten aktiver Endschalter führt somit nicht zu einer Fahrt.
Ein abgefallener Impuls zwischen zwei Polls kann vom Poll prinzipbedingt
übersehen werden; ein während einer Startprüfung anliegender Impuls wird
unabhängig davon erkannt. Der Hardwaretest muss die Eignung dieses
Abtastverfahrens für die geplante Geschwindigkeit nachweisen.

Nach einem Endschalteralarm müssen die Eingänge mit abgeschalteten
Antrieben sicher freigegeben werden. Es gibt absichtlich keine
softwareseitige Rückfahrumgehung bei betätigtem Schalter. Manuelles
Freigeben muss zur Mechanik und deren Schwerkraftlast passen.

## Referenzfahrt bleibt gesperrt

`RF2000_HOMING_VERIFIED=0` ist die ausgelieferte Einstellung. `$H`,
`$22=1` und ein direkter Aufruf der Referenzroutine sind gesperrt. Die
Sperre darf erst in einem bewusst neu kompilierten Hardwareprofil geändert
werden, nachdem Schalterpegel und reale Achsenrichtungen geprüft wurden.
Insbesondere ist Repetiers Druckerreferenz Z-Min nicht mit dessen
Fräsmodus gleichzusetzen: Der Originalcode kehrt dafür die Z-Richtung um.
Z-Max ist der Kandidat für die CNC-Referenz, keine hier bestätigte
Bewegungsrichtung.

Für ein später verifiziertes Profil berücksichtigt der Code X/Y-Min und
wählt für Z anhand der Homingrichtung genau Z-Min oder Z-Max. Nur die
jeweils im laufenden Homingzyklus erwarteten Referenzschalter dürfen den
Homing-Achsenlock auslösen, ohne den ganzen Zyklus als Hard Limit zu
beenden; der gegenüberliegende Z-Schalter und andere Eingänge bleiben
Hard Limits. Eine Referenzfahrt startet nicht mit bereits betätigtem
Schalter. X-/Y-Homing in MAX-Richtung wird mangels passender Schalter
abgewiesen. Dies ist eine vorbereitete Implementierung und kein Nachweis
einer auf realer Hardware erfolgreichen Referenzfahrt.

## Diagnose ohne Motorfreigabe

`$DRV` ist ein rein lesender Board-Befehl, auch im Alarmzustand. Beispiel:
`[DRV:READY=0|FAULT=2|READBACK=4|LIMITS=8|STATUS=0,0,0,0,0]`.
Alle Zahlen sind dezimal. `READY` beschreibt die Treiberbereitschaft;
`FAULT` ist die Bitmaske 1=FAULTn erkannt, 2=Register-Readback fehlgeschlagen,
4=Fehlerstatus bei Initialisierung. `READBACK` nennt betroffene Treiber
mit Bits 0–4 (X/Y/Z/A/B). `LIMITS` nennt aktuell aktive X-Min/Y-Min/Z-Min/
Z-Max mit Bits 0–3. `STATUS` enthält die fünf bei Initialisierung gelesenen
DRV8711-Statusregister, keine live gelesenen Registerwerte. Der Befehl
überträgt kein SPI und verändert keinen Ausgang. Das Beispiel ist ein
Formatbeispiel und kein gemessener Zustand.

## Kontrollierter Eingangstest vor Bewegung

1. Antriebs-/Werkzeugleistung zunächst gemäß Hardwaretestplan isolieren;
   alle vier erwarteten freien und betätigten Pegel am MCU bzw. Testpunkt
   messen und mit dem Status vergleichen. Beide Z-Schalter getrennt testen.
2. Jeden Schalter im Leerlauf betätigen: alle nSLEEP müssen LOW bleiben,
   Freigabeversuche müssen abgewiesen werden. Nach Loslassen und stabiler
   Freigabe darf ein bewusster Reset/Unlock den Limit-Alarm aufheben.
3. Gegenprobe mit eingelesener alter EEPROM-Polarität und deaktiviertem
   `$21`: die verpflichtende Board-Abfrage muss unverändert schützen.
4. FAULTn über die elektrisch zulässige Testmethode prüfen; danach darf ein
   Unlock keinen Treiber freigeben. Nicht mit einem MCU-Ausgang kurzschließen.
5. Erst nach dokumentierter Richtungsprüfung kurze Einzelachsbewegungen
   mit begrenzter Energie ausführen und das reale Stoppverhalten messen.
   Homing bleibt dabei gesperrt, bis sämtliche Voraussetzungen belegt sind.

Es wurden keine physischen Hardwaremessungen oder Referenzfahrten in der
Cloud ausgeführt. Host-Tests bzw. AVR-Kompilation prüfen nur die Software.
