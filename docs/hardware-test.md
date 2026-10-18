# Kontrollierter Hardwaretest

Dieser Plan bereitet einen Test vor. Er dokumentiert **keinen bereits bestandenen Hardwaretest** und erteilt keine pauschale Freigabe zum Flashen oder Bewegen einer Maschine. Die verbindliche Schaltplangrundlage und der Quellabgleich stehen in [hardware-verification.md](hardware-verification.md). Jede Stufe setzt die protokollierte Erfüllung der vorherigen Stufe voraus; bei Abweichung Versorgung sicher abschalten und die Ursache klären.

Arbeiten an 230 V und an der vorhandenen Netz-/Not-Aus-Schaltung gehören in qualifizierte Hände. Für alle ersten Stufen Netzspannungsverbraucher, Spindel, Heizbett und Heizpatronen **physisch abtrennen**; USB allein ist keine verlässlich getrennte Versorgung für sämtliche Platinenbereiche. Keine Motorstecker unter Spannung stecken oder lösen. Z gegen Absinken sichern, Werkzeuge entfernen, Arbeitsbereich räumen.

## 1. Dokumentation, Rückweg und stromloser Aufbau

1. Für diesen Auftrag gilt die Platine nach Nutzervorgabe als vollständig dem Schaltplan `references/rf2000-schematic.pdf` entsprechend. Aufdruck und Seriennummer dienen der Zuordnung des Prüfprotokolls; ein erneuter Revisionsabgleich ist keine offene Vorbedingung.
2. Vor einer erwogenen Programmierung bestehende Flash-/EEPROM-Inhalte und Fuses mit einem geeigneten, zur Platine passenden Verfahren sichern und die Sicherung verifizieren. Bootloader und Fuses nicht ungeprüft überschreiben. Das bereitgestellte Build enthält keine Freigabe, den alten Druckerstand zu verlieren.
3. Die festen Pinzuordnungen aus dem Plan im Prüfprotokoll verwenden: DRV4 PA4/PA6; OUT3 über PC4 LOW, PA7 Eingang ohne Pullup; PC0 bleibt ES2-Eingang. Beim stromlosen Aufbau Motor-/Sensorleitungen und gesteckte Verbinder auf Beschädigung oder Fehlanschluss prüfen.
4. Die im Plan festgelegten 0,033-Ω-Shunts R80–R89 für die Stromberechnung verwenden. Die später gemessenen Wicklungsströme gegen Motorgrenzen und diese Sollwerte prüfen.
5. Endschalterstecker und Kabel verfolgen. ES1/2/4 sind zweipolige Signale gegen VSS; ES3 ist vierpolig und kann eine aktive Sensorplatine versorgen. Pinreihenfolge anhand der realen Platine verifizieren. NO/NC, Versorgung, Betätigungsrichtung und Kabelbruchzustand separat aufschreiben. Keine 24 V auf AVR-Eingänge geben.
6. Unabhängige Abschaltung nachweisen. Ein serielles `!`, Ctrl-X oder USB-Abziehen ist kein unabhängiger Not-Aus.

**Abbruch:** Fehlende Rücksicherung, beschädigte oder falsch gesteckte Leitung, unbekannte externe Sensorversorgung oder unwirksame unabhängige Abschaltung.

## 2. Logikbetrieb ohne Motoren und Lasten

Alle fünf Motoren und alle Leistungslasten bleiben abgetrennt. Nach den Vorbereitungen aus Stufe 1 und mit einem bewusst gewählten Programmierverfahren den geprüften Testbuild einsetzen. AVR-Programmer/Bootloader-Einstellungen aus der tatsächlichen Platine bestimmen; keine automatische Flashaktion aus diesem Dokument ableiten.

- 5 V, 3,3 V und gegebenenfalls Motorversorgung mit passender Strombegrenzung prüfen. DRV8711-Logik und Registerzugriff benötigen eine gültige Versorgung und Wake-Sequenz; ein USB-Dialog allein belegt dies nicht.
- RESET beobachten und stabilen Lauf prüfen. Der Plan ist verbindlich: PA7 gehört zu OUT3, PC0 bleibt ES2-Eingang. Unerwartete Watchdog-Neustarts untersuchen; keine Taktimpulse auf diesen Pins als Umgehung erzeugen.
- HZ1/PB4, HZ2/PH6, HZ3/PE2, P230/PE5, OUT1/PA3, OUT2/PA5, OUT3/PC4 mit hochohmigem PA7, OUT4/PC5, OUT5/PF4 und RGB/PH3–PH5 mit Oszilloskop/Logikanalysator messen. Sie müssen über Reset, Boot, Initialisierung, Alarm und M3/M4/M7/M8 inaktiv bleiben. Hochohmige Resetphasen anhand der externen Beschaltung beurteilen; bei einem tatsächlichen HIGH-Puls keine Last anschließen.
- STEP aller fünf Kanäle darf ohne bewusstes Bewegungskommando keine Flanken zeigen. SLEEPn aller Kanäle muss nach der Initialisierung inaktiv LOW sein; kurze kontrollierte Wake-Phasen für Registerkonfiguration nur ohne STEP und bei deaktivierter Leistungsfreigabe. Reserve A/B bleibt nach der Initialisierung LOW und deren CTRL.ENBL bleibt gesperrt.
- Seriell die GRBL-Kennung, Achsenzahl XYZ und Einstellungen lesen. `$22=1` und `$H` müssen im unverifizierten Build zurückgewiesen werden. `$X` darf weder einen Fehlerzustand elektrisch beseitigen noch Reserveachsen freigeben. `G1 A...` und `G1 B...` dürfen keine Reservebewegung auslösen.

**Abbruch:** Motorfreigabe ohne beabsichtigten Test, unerwartete STEP-Flanke, Netz-/Heizausgang aktiv, wiederholter Reset oder umgehbare Homing-Sperre.

## 3. SPI- und Treiberprüfung

Oszilloskop/Logikanalysator gemeinsam an Masse, SCLK/PD5, SDATI/PD6, SDATO/PD4, SCS und SLEEPn anschließen. Nur ausreichend hochohmige und für die Signalspannung geeignete Tastköpfe verwenden.

1. HIGH-aktive SCS-Phase, LOW-Ruhetakt und genau 16 MSB-first Takte pro Schreibwort bestätigen. Es darf beim Lesen immer nur ein SCS HIGH sein; SDATO muss echte wechselnde Daten liefern und darf nicht lediglich den Pullupwert abbilden.
2. RESET_DRV/PE7: HIGH-Puls und anschließende Wartezeit entsprechend Implementierung prüfen. **Reset muss bei wachen Treibern erfolgen**, da TI §7.4.1 Reset im Sleep ausschließt. Vor dieser Wake-Phase muss CTRL.ENBL=0 für alle fünf Kanäle geschrieben und verifiziert worden sein; es darf keine STEP-Flanke geben. UVLO erst im wachen Zustand löschen, Ursache einer tatsächlichen Unterspannung nicht übergehen. Wake-up-Zeit ab SLEEPn-HIGH messen (Datenblatt maximal 1 ms; Port verwendet längere Wartezeit).
3. Jeden der fünf Treiber einzeln auslesen. CTRL.MODE: X/Y/A/B 1/32, Z 1/16; CTRL.ISGAIN=20; TORQUE X=110, Y=110, Z=105, A/B=90. OFF/BLANK/DECAY/STALL/DRIVE gegen die Quellwerte vergleichen. Reserve-ENBL soll nach Konfiguration gesperrt sein. Der read-only Befehl **`$DRV`** meldet den gespeicherten Starttest: `[DRV:READY=1|FAULT=0|READBACK=0|LIMITS=0|STATUS=0,0,0,0,0]` ist der normale fehlerfreie Ausgangspunkt. Werte sind dezimal; FAULT ist eine Bitmaske (1=FAULTn, 2=Readback, 4=STATUS-Leistungsfehler), READBACK Bits 0–4 entsprechen DRV1–5. STATUS sind gespeicherte Startwerte, keine neue SPI-Messung während der Bewegung. Der Befehl ersetzt den Logikanalysator nicht.
4. STATUS und gemeinsames FAULTn prüfen. Ein angeblich erfolgreicher Read mit `0x000` oder `0xFFF` allein genügt nicht: auch bekannte, unterschiedliche Konfigurationsregister müssen stimmen. Keine nachträgliche Freigabe nach fehlgeschlagenem Readback.
5. Fehlerpfad zuerst ohne Motoren und nur mit elektrisch verträglicher, kontrollierter Signalinjektion prüfen. FAULTn ist Open-Drain: niemals aktiv gegen einen Treiberausgang HIGH treiben. Kein absichtlicher Kurzschluss an einer Motorbrücke. LOW muss Alarm/Abschaltung auslösen und darf keine automatische Wiederaufnahme bewirken.

**Abbruch:** Registerabweichung, unlesbarer Kanal, gleichzeitiges Lesen mehrerer Treiber, Fault ignoriert oder A/B freigegeben. Bei Sleep-bedingt fehlendem Readback die Sequenz untersuchen; den Read nicht überspringen.

## 4. Endschalter ohne Motoren

Für jeden Schalter einzeln den rohen MCU-Pegel, die Statusanzeige (`?`, gegebenenfalls `Pn:`), `$DRV` und die Alarmmeldung protokollieren. In `$DRV` unterscheidet LIMITS die physischen Kontakte als Bitmaske: 1=X-Min, 2=Y-Min, 4=Z-Min, 8=Z-Max; gleichzeitig aktive Kontakte addieren sich. Erwarteter elektrischer Ausgangspunkt: HIGH frei, LOW betätigt. Dies muss am tatsächlichen Sensor nachgewiesen werden.

| Versuch je X-Min, Y-Min, Z-Min und Z-Max | Erwartete Feststellung |
|---|---|
| Frei → betätigt → frei, mehrfach langsam | Eindeutige Statusänderung; Z-Min und Z-Max müssen beide erkannt werden |
| Kurze Betätigung und mechanisches Prellen | Anhaltende Betätigung löst zuverlässig aus, kein dauerhaft maskierter Eingang |
| Betätigt beim Einschalten und beim Reset | Alarmzustand; keine Freifahrbewegung oder STEP-Flanke |
| `$X` bei weiterhin betätigtem Eingang | Keine Bewegung/Freigabe an einem weiterhin aktiven Limit |
| Kabel abziehen | Tatsächlichen Zustand protokollieren; keine behauptete Kabelbrucherkennung |
| Mehrere Schalter gleichzeitig | Alle physischen Eingänge bleiben erkennbar, Alarm unabhängig von Reihenfolge |

PC0/PC2/PC6 und PH1 werden mit nominell 1 kHz timerbasiert abgefragt; LOW wird beim ersten Sample wirksam, Freigabe benötigt fünf freie Samples. Am Logikanalysator eine Bewegung ohne Motoren erzeugen und Limitpegel mit STEP vergleichen; maximale Stopplatenz einschließlich ungünstiger Abtastphase und serieller Last messen. Der 1-ms-Abtastabstand zuzüglich Interruptsperrzeit und das 100-µs-RC der extern beschalteten Eingänge sind kein Ersatz für die Messung. Die Anhaltewege der späteren Mechanik zusätzlich berücksichtigen.

**Abbruch:** Falsche Polarität, nicht erkannter Z-Schalter, Alarmumgehung, unerklärliches Reset-/ES2-Verhalten oder Stopplatenz oberhalb der für die Maschine vertretbaren Grenze.

## 5. STEP-/DIR-Zeitmessung ohne Motoren

Nach positiven Stufen 1–4 und ausschließlich mit lastlosen Ausgängen die XYZ-Signale prüfen:

- Ruhestand und aktive STEP-Polarität; Puls-HIGH und -LOW jeweils mindestens **1,9 µs** nach TI §6.7. Die konfigurierte GRBL-Pulsbreite darf diese Grenze nicht unterschreiten.
- DIR vor dem ersten STEP stabil; Richtungsvorlauf und Nachlauf jeweils mindestens **200 ns** nach TI §6.7 auch beim schnellen Richtungswechsel messen.
- X/Y/Z einzeln und synchron; korrektes Setzen/Löschen auf PORTF/PORTL ohne Änderung von SLEEPn/PF2 oder sonstigen Fremdbits.
- Timer-Overflow/Pulsendende, Reset während eines Pulses, Alarm während einer Bewegung und UART-Last untersuchen. Keine verkürzten oder zusätzlichen Pulse, kein ständig HIGH bleibender STEP.
- Maximale im Prüfbuild eingestellte Schrittfrequenz anhand Feedrate und steps/mm bestimmen und messen. Der Port begrenzt die Parametrierung auf **5 kHz je Achse**, Standardpulsbreite 10 µs und DIR-Vorlauf 5 µs. Die ersten Default-Raten ergeben X/Y ca. 1270 Schritte/s und Z 3200 Schritte/s. Eine Messung bei kleiner Frequenz qualifiziert nicht automatisch den upstream-Maximalwert.
- Während sämtlicher Tests PA4/PC1 (Reserve-STEP), PA2/PC7 (Reserve-SLEEPn) und Heiz-/P230-Ausgänge mitbeobachten: A/B niemals schalten/freigeben, Lasten inaktiv.

**Abbruch:** Zeitverletzung, Fremdbitänderung, verlorene Pulse, ISR-Überlastung oder irgendeine Reservebewegung.

## 6. Erster einzelner Motor, dann XYZ

Diese Stufe ist erst nach erfolgreicher elektrischer Prüfung vertretbar. Die testende Person benötigt unmittelbaren Zugriff auf einen **unabhängigen Totmann-/Not-Aus**, freie Sicht und einen mechanisch begrenzten, kollisionsfreien Testweg. Spindel und Heizer bleiben abgetrennt; A/B bleiben abgetrennt und deaktiviert. Keine Homing-Fahrt.

1. Spannungsfrei nur X-Motor anschließen; Z gegen Absinken sichern. Motornenndaten und gemessenen Shunt gegen den berechneten Spitzenstrom prüfen. Ist ein zulässiger Strom nicht belegt, noch keinen Motor bestromen.
2. Ohne STEP-Fahrt Freigabe, Stromaufnahme und Erwärmung kurz beobachten. Mit geeigneter Stromsonde beziehungsweise sauberer Shuntmessung tatsächlichen Wicklungsstrom verifizieren; Netzteilanzeige allein ist kein Phasenstrommessgerät. Bei Fault, ungewöhnlichem Geräusch, schneller Erwärmung oder unerwartetem Haltemoment sofort abschalten.
3. Eine einzelne sehr kurze, langsam begrenzte XYZ-Jogbewegung unter Totmannaufsicht ausführen, etwa **0,1 mm** in der jeweils zuvor als kollisionsfrei bewerteten Richtung. Die konkrete Richtung erst am Gerät festlegen; hier stehen bewusst keine blind ausführbaren G-Code-Fahrbefehle. Nur eine Bewegung im Puffer, sofortige Abschaltung erreichbar.
4. Bewegungsrichtung, tatsächlichen Weg und Stillstand nach Ende prüfen; Gegenrichtung erst nach positivem Ergebnis. Limits bei minimaler Geschwindigkeit und ausreichendem Abstand kontrolliert nachweisen. Hard-Limits stoppen abrupt und sichern die Z-Last nicht gegen Absinken.
5. Spannung aus; X trennen, Y anschließen und entsprechend prüfen. Danach Z mit besonderer Sicherung gegen Absinken und Kollision testen. Erst danach die drei Motoren gemeinsam anschließen.
6. Schritte/mm mit Messuhr bestätigen: Quellenwerte X/Y 152,38, Z 1280 sind Startwerte für unveränderte Mechanik. Strom und Temperatur anschließend stufenweise unter einer kleinen realen Last beobachten. Geschwindigkeiten und Beschleunigungen nur nach Messergebnissen erhöhen.

Bei betätigtem Endschalter wird nicht per Software zwangsfrei gefahren. Zunächst spannungsfrei und mechanisch sicher entlasten. Wiederanlauf nach Fault/Reset nur nach Ursachenklärung und erneuter Positionsprüfung.

## 7. Homing bleibt ein eigener Freigabeschritt

Der ausgelieferte unverifizierte Build hält `RF2000_HOMING_VERIFIED=0`. `$22` oder EEPROM-Werte dürfen das nicht verändern. Eine Freigabe erfordert ein ausgefülltes Prüfprotokoll für alle Richtungen/Schalter und einen bewusst neu erzeugten Build.

Für die CNC-Anwendung ist **Z-Max mit positiver Z-Referenzrichtung** aus dem Repetier-Fräsmodus der Quellenkandidat. Das bedeutet erst nach realer Richtungsprüfung „vom Werkstück weg“. X/Y sind Quellkandidaten Richtung Min. Nach erfolgreicher Prüfung Homing zuerst ohne Motoren am Logikanalysator, danach einzeln langsam mit Totmannaufsicht testen: freier Start, bereits aktiver Schalter, dauerhaft aktiver Schalter, nicht erreichter/abgetrennter Schalter, Pull-off, zweite langsame Annäherung, erneutes Auslösen und Alarm. Keine automatische Freigabe aus diesem Dokument ableiten.

## Protokollvorlage

| Feld | Eintrag am Prüfplatz |
|---|---|
| Platinenaufdruck / Seriennummer / Fotos | offen |
| Quellcommit / HEX-SHA256 / Buildzeit / Compiler | offen |
| Flash-/EEPROM-/Fuse-Sicherung und Wiederherstellung geprüft | offen |
| Verbindliche Hardwaregrundlage | RF2000-Schaltplan laut Nutzervorgabe vollständig passend |
| Reset-/Watchdog-Verhalten im Logikbetrieb | offen |
| Versorgung / Shunts / Motornennströme | offen |
| Registerwerte aller fünf Treiber / FAULT-Test | offen |
| Limitpegel frei/aktiv/abgezogen, je Eingang | offen |
| Maximale Limitreaktionszeit unter Last | offen |
| Gemessene STEP-Breite / DIR-Setup/-Hold / Frequenz | offen |
| A/B stets ohne STEP und Freigabe | offen |
| HZ/P230/OUT inaktiv über alle Zustände | offen |
| Not-Aus / Totmann / Z-Sicherung geprüft | offen |
| XYZ-Richtung, Wegmessung, Strom und Temperatur | offen |
| Homing-Freigabe, verantwortliche Person, Datum | **nicht erteilt** |
