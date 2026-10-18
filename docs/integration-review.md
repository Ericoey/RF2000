# Integration: Boot, Reset und EEPROM

Die unabhängige Prüfung der RF2000-Integration fand zwei zusammenhängende EEPROM-Fehler, die behoben wurden:

- Der serielle GRBL-Puffer ist 256 Byte groß, EEPROM-Datensätze haben 80 Byte. Die ursprüngliche Speicherung mit 256 Byte überlappte Startupzeilen und Buildinformationen. Speicherung, Lesen und Wiederherstellen verwenden jetzt einheitlich 80 Byte mit 81 Byte Abstand einschließlich Prüfsumme. Startup0 belegt 768–848, Startup1 849–929 und Buildinfo 942–1022.
- Startupindizes werden vor G-Code-Ausführung auf ganze Zahlen im Bereich 0–1 geprüft. Negative, gebrochene und zu große Indizes ändern keine Datensätze. Zeichenketten mit 80 oder mehr Zeichen werden abgewiesen; 79 Zeichen plus NUL sind möglich. Kürzere Datensätze werden mit Nullen aufgefüllt. Ein Datensatz ohne NUL wird auch bei passender Prüfsumme verworfen und geleert.

Zusätzlich stellte die ursprüngliche AVR-EEPROM-Schreibfunktion den Interruptstatus mit einem bedingungslosen `sei()` wieder her. Sie erhält jetzt den ursprünglichen SREG-Wert. Die Bootinitialisierung aktiviert die UART-Interrupts deshalb ausdrücklich vor dem langen Bericht bei neuem oder inkompatiblem EEPROM; die Motoren sind zu diesem Zeitpunkt bereits abgeschaltet und es laufen noch keine Bewegungstimer.

`python3 tools/test_eeprom_records.py` kompiliert die tatsächlichen C-Funktionen aus `settings.c` und `eeprom.c` mit simuliertem EEPROM und AVR-Registern. AddressSanitizer und UndefinedBehaviorSanitizer prüfen Datensatzgrenzen, Index- und Längengrenzen, NUL-Behandlung, Wiederherstellung und Erhaltung von SREG bei allen Schreibvarianten. Der Test wurde erfolgreich ausgeführt. Der vollständige AVR-Build und die UART-Simulation prüften zusätzlich `$N0=G21`, `$N1=G90`, `$I=EEPROMCHECK`, getrenntes Zurücklesen sowie die unveränderten Datensätze nach `$N2`, `$N-1` und `$N0.5`.

Weiter geprüft: Jeder Boot und Software-Reset startet in ALARM; `$X` und der Bewegungsstart prüfen aktive Endschalter und Treiberzustand erneut. Homing ist ohne die Compilefreigabe `RF2000_HOMING_VERIFIED` gesperrt. Die RF2000-Einstellungen erzwingen STEP-Puls 10–20 µs, feste STEP-/Freigabe-/Endschalterpolarität, Hard Limits und deaktivierten Lasermodus. Feed-Override bleibt auf höchstens 100 %. Nicht eingerichtete Spindel-, Kühlmittel-, Digitalausgabe- und Probe-Befehle werden abgewiesen.

Diese Ergebnisse bestätigen die ausgeführten Softwaretests. Elektrische Pegel, Motordrehrichtungen und reale Fehlerabschaltung müssen im kontrollierten Hardwaretest gemessen werden.
