# Prüfbericht

Stand: 18.10.2026. Alle folgenden Befehle wurden in der Cloudmaschine
ausgeführt. Es wurde keine Platine programmiert und kein Motor bewegt.

## Build und Wiederholbarkeit

| Variante | Flash | Statisches SRAM | Ergebnis |
| --- | ---: | ---: | --- |
| GRBL RF2000 XYZ | 35.226 / 253.952 Byte | 3.520 / 8.192 Byte | gebaut |
| Repetier Community RF2000v2 | 218.996 / 253.952 Byte | 6.914 / 8.192 Byte | gebaut |

GRBL verwendet `.text=35176`, `.data=50`, `.bss=3470`. Für SRAM werden
`.data` und `.bss` addiert. Die neue Binutils-Kurzausgabe zählt initialisierte
Daten bei dieser Zielplattform anders; die Kapazitätsprüfung verwendet
deshalb die einzelnen ELF-Sektionen. Statische SRAM-Reserve belegt keine
maximale Stacktiefe während aller Betriebsarten.

Der Installer wurde aus einem leeren zweiten Werkzeugpräfix ausgeführt:
signierte APT-Metadaten, festgelegte Debian-Paketversionen und der geprüfte
Arduino-Core-Commit. Anschließend entstand ein vollständiger GRBL-Build in
einem zweiten Buildverzeichnis. Sein HEX ist bytegleich zum Hauptbuild.
Repetier wurde mehrfach gebaut; HEX, ELF und EEP waren bei Wiederholung
bytegleich. MAP-Dateien können temporäre LTO-Pfade enthalten.

Firmware-SHA256:

```text
GRBL     6c9b5ba8fe9859a3fab238af9202e40301f7dc5b04248492e1af0d2472142050
Repetier cffc216c8d5f4c40fb4f7691d6aa3490faa1e21fdb5dfb04bf7d173bd58ca013
```

Die 64 extrahierten Repetier-Referenzdateien wurden zusätzlich byteweise
gegen das hochgeladene Archiv verglichen. Der dokumentierte RF2000v2-
Kenntnisnahmehinweis wird ausschließlich in der getrennten Buildkopie
ersetzt. Repetier bleibt Druckerfirmware mit ihren ursprünglichen Funktionen.

## Gesamttest `make check`

Ergebnis: **bestanden**, Exitstatus 0. Vollständige Ausgabe:
`artifacts/validation.log`.

| Prüfung | Ausgeführte Nachweise |
| --- | --- |
| Tatsächlicher Limit-Code mit GPIO-Modell | Alle 16 Kombinationen der vier Eingänge; feste Polarität; aktiver Eingang beim Init; sofortige Stopentscheidung; fünf freie Samples; Prellen; Z-Min/Z-Max; Fehlersperre; spätere Homing-Schalterauswahl |
| Tatsächliche DRV8711-Zustandsmaschine | Alle fünf Registerprofile; ENBL0 vor Wake; wirksamer Awake-Reset; UVLO-Löschung; Readbackfehler in allen drei Phasen; XYZ-Maske; Reserve ENBL0; 25-ms-Wake; persistenter Fault; Fehler während Wake; INT6-Flankenkonfiguration |
| Tatsächliche EEPROM-Funktionen mit ASAN/UBSAN | Getrennte Datensätze; Längen-/Indexgrenzen; NUL-Behandlung; Wiederherstellen; SREG-Erhaltung |
| Echter AVR-ELF in simavr | Fünf DRV8711-SPI-Modelle; fabrikleeres EEPROM; Bootalarm; Befehls-/Einstellungsgrenzen; EEPROM über UART; STEP-/DIR-GPIO; bewegter Z-Max-Test; Fault-/Readback-Fehler; zwei gezielte ISR-Fehlerzeitpunkte; kurzer INT6-Puls |

Der ELF-Simulator prüfte genau 32/32/16 XYZ-Schritte vorwärts und dieselbe
Anzahl zurück. Die minimale normale Pulsbreite dieses Laufs betrug
**14,312 µs** bei eingestellten 10 µs. Ein weiterer Z-Lauf erzeugte genau
**6.400 Pulse**, mit **4992,81 Schritten/s** im schnellsten Fenster über
64 Flanken, bei Statusanfragen mit 100 Hz. Fremdbits von PORTF sowie
Reserve-/Lastenausgänge wurden mitbeobachtet.

Fehler wurden beim Eintritt in Timer1 und den verzögerten Timer0-ISR
eingespeist: kein weiterer STEP, alle Treiber schlafen, spezifischer
Treiberalarm 12 bleibt erhalten. Zusätzlich wurde ein nach **1 µs** bereits
beendeter FAULTn-Puls im Ruhezustand injiziert. INT6 verriegelt ihn; Ctrl-X
löscht diese Sperre nicht. Ein korrumpierter SPI-Readback verhindert `$X`
und Bewegungen. PH1/Z-Max beendet eine laufende Bewegung und blockiert die
Freigabe, solange der Eingang aktiv ist.

## Codeprüfung und Grenzen

Die unabhängigen Prüfungen fanden und korrigierten unter anderem die
DRV4-Pinabweichung, RESET/UVLO im Sleep, A/B-ENBL, das Überschreiben geteilter
STEP-Portbits, einen undefinierten Timerstart, die Alarmreihenfolge nach
`mc_reset`, EEPROM-Überlappung und den unbedingten Interrupt-Reenable im
EEPROM-Treiber. Serieller RX-Überlauf erzwingt nun einen Bewegungsreset und
die Quittierung eines kritischen Alarms. Dieser Überlaufpfad wurde gelesen
und kompiliert; eine gezielte Überlaufinjektion war nicht Teil des
beschriebenen Simulatorlaufs.

Im vollständigen GRBL-Build verbleiben Upstream-Warnungen zu absichtlichem
Switch-Fallthrough und den 256-Byte-Ringpuffern mit uint8-Überlauf. Die
separaten RF2000-Board-/Treiber-/Steppermodule wurden ohne Warnungen
kompiliert. Repetier enthält die dokumentierten ursprünglichen fehlenden
Return-Pfade in `Printer.h`. Warnungen wurden nicht global abgeschaltet.

Nicht geprüft: reale Leiterplattenrevision, elektrische Signalqualität,
Watchdog-Netzzuordnung, Endschalter-NO/NC und Kabelbruchverhalten, tatsächliche
Wicklungsströme, mechanische Bewegungsrichtungen, reale Anhaltewege und
Dauerbetrieb. Homing bleibt gesperrt. Die Hardwareprüfung folgt
[hardware-test.md](hardware-test.md); Softwaretests erteilen keine
automatische Freigabe für eine Referenzfahrt oder den Fräsbetrieb.
