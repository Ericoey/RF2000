# Alarm 12 beim RF2000

`ALARM:12` bedeutet einen verriegelten DRV8711-Treiber-/Konfigurationsfehler.
Die Firmware hält deshalb alle Motoren abgeschaltet. `error:12` ist eine
andere Meldung: Sie betrifft eine unzulässige Geschwindigkeitseinstellung.

## Diagnose in gSender 1.6.4

Bei `[MSG:Reset to continue]` wartet GRBL auf einen Software-Reset und
verarbeitet normale Konsolenzeilen noch nicht. Vorgehen nach dem Alarm:

1. gSender verbunden lassen. **Den roten STOP-Knopf mitten im rechten
   X/Y-Steuerkreuz** einmal anklicken. Im GRBL-Alarmzustand sendet dieser
   Knopf das Resetzeichen `0x18`.
2. Die neue `Grbl 1.2i`-Startmeldung abwarten.
3. In der Konsole einzeln `$DRV` und `$MS` senden und die Antworten sichern.

Der untere Job-STOP kann ohne laufenden Job deaktiviert sein. Ein getipptes
`$RST=*` ist kein Software-Reset: Es löscht Einstellungen und Nullpunkte.
`Ctrl+X` im Konsoleneingabefeld ist kein verlässlich belegter gSender-
Tastaturweg; der rote mittlere STOP ist im Quellcode ausdrücklich zugeordnet.

Der Software-Reset löscht den verriegelten Treiberfehler nicht. Ein echter
MCU-Neustart oder erneutes Öffnen des seriellen Ports kann dagegen den
Starttest neu ausführen und den vorherigen Diagnosezustand verlieren.
Deshalb vor der Diagnose weder ausschalten noch neu verbinden.

Die gSender-Zuordnung ist anhand des offiziellen 1.6.4-Quellstands geprüft:
[Jogging.ts](https://github.com/Sienci-Labs/gsender/blob/eb2df2d2073d9dd1a762c0ed150d9be19360cf49/src/app/src/features/Jogging/utils/Jogging.ts#L109).

## Antworten verstehen

Beispiel eines erfolgreichen Starttests:

```text
[DRV:READY=1|FAULT=0|READBACK=0|LIMITS=0|STATUS=0,0,0,0,0]
[MS:VIRTUAL=256|MICRO=256|MAX=1000|PULSE_MAX=12000]
```

Das bestätigt den aktuellen Startzustand. Es enthält keinen gespeicherten
Fehler einer früheren MCU-Sitzung.

| Feld | Bedeutung |
| --- | --- |
| `FAULT=1` | Gemeinsames FAULTn wurde aktiv; Versorgung und Treiberfehler prüfen |
| `FAULT=2` | Ein Register wurde nicht wie erwartet zurückgelesen |
| `FAULT=4` | Der Start-STATUS enthält einen Leistungsstufenfehler |
| Andere FAULT-Werte | Bitkombinationen, beispielsweise 3 = FAULTn plus Readback |
| `READBACK` | Kanalmaske: X=1, Y=2, Z=4, Reserve A=8, Reserve B=16 |
| `MICRO=0` | Eine Laufzeit-Umstellung blieb unbestätigt; vor jeder Bewegung ist vollständige Registerbestätigung erforderlich |
| `LIMITS` | Physische Endschaltermaske: X-Min=1, Y-Min=2, Z-Min=4, Z-Max=8 |

Das gemeinsame XYZ-Profil wird vor der ersten Bewegung auf allen drei
Kanälen geprüft. Ein fehlgeschlagenes X-Readback kann daher auch den Beginn
einer Z-Referenzfahrt sperren. Der Kanalname allein beweist keinen defekten
Motor oder Treiber.

## Korrektur des Laufzeit-Readbacks

Bei der bisherigen Kennung `20261022-rf2000-cnc-adaptive` wurde auch das
Readback beim MODE-Wechsel mit dem schnellen SPI-Transport ausgeführt.
SDATO ist Open-Drain und verwendet den AVR-Pull-up. Eine verzögerte HIGH-
Flanke kann deshalb zu einem scheinbar falschen Registerwert führen.

Die Kennung **`20261024-rf2000-cnc-adaptive-spi`** verwendet für dieses
Readback den langsameren, bereits beim Start verwendeten Ablauf.
MODE-Schreiben bleibt schnell. Die XYZ-Registervergleiche, FAULT-Abfrage,
Pulsbarriere und Abschaltung bleiben wirksam; Fehler werden nicht ignoriert.

Ein neuer Test mit synthetischer 1-µs-HIGH-Anstiegszeit reproduziert beim
alten offiziellen ELF Alarm 12 vor dem ersten Homingschritt. Derselbe Test
besteht beim korrigierten ELF. Das ist eine gezielte Nachstellung im
Simulator, keine Messung der tatsächlichen Signalflanke an der Maschine.

Projekt aktualisieren, `rf2000_cnc` bauen und nach dem Aufspielen die neue
Kennung mit `$I` prüfen. EEPROM-Version 76 und die Kalibrierung bleiben
unverändert; `$RST=*` oder neue Schritte/mm sind dafür nicht nötig.
Falls Alarm 12 weiterhin auftritt, die oben beschriebenen Diagnosewerte
sichern und die Ursache anhand der FAULT-/READBACK-Bits weiter eingrenzen.
