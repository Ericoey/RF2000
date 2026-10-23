# Kontrollierter Hardwaretest

Dieser Plan bereitet einen Test vor. Er dokumentiert **keinen bereits bestandenen Hardwaretest** und erteilt keine pauschale Freigabe zum Flashen oder Bewegen einer Maschine. Die verbindliche Quellenpriorität und der Quellabgleich stehen in [hardware-verification.md](hardware-verification.md): Bei abweichenden Hardwarezuordnungen gilt nach jüngster Nutzervorgabe die als funktionierend angenommene RF2000v2-Repetier-Konfiguration. Jede Stufe setzt die protokollierte Erfüllung der vorherigen Stufe voraus; bei Abweichung Versorgung sicher abschalten und die Ursache klären.

Arbeiten an 230 V und an der vorhandenen Netz-/Not-Aus-Schaltung gehören in qualifizierte Hände. Für alle ersten Stufen Netzspannungsverbraucher, Spindel, Heizbett und Heizpatronen **physisch abtrennen**; USB allein ist keine verlässlich getrennte Versorgung für sämtliche Platinenbereiche. Keine Motorstecker unter Spannung stecken oder lösen. Z gegen Absinken sichern, Werkzeuge entfernen, Arbeitsbereich räumen.

Die elektrischen Vorprüfungen gelten für alle RF2000-Profile. Die
ursprünglichen XYZ-Inbetriebnahmeschritte unten beschreiben
`rf2000_grbl`; Unterschiede des aktuellen CNC-Profils sind ausdrücklich
genannt. Das [CNC-Profil](cnc-gsender.md)
`20261022-rf2000-cnc-adaptive` verwendet automatische XYZ-Auflösungen von
**1/256 bis 1/8**, ein festes virtuelles Raster mit nominal
**20480/20480/20480 Einheiten/mm** und höchstens **1000 mm/min Achs-/Bahnfeed**.
Factory-Tempi bleiben 600/600/300 mm/min bei 10 mm/s². Gültige niedrigere
alte Tempi werden nicht erhöht. Vor Bewegungen `$I`, `$$`, `$MS` und die
[Migration](cnc-gsender.md#bestehende-cnc-einstellungen-umstellen) prüfen.
**Alte Werte 320, 1280 oder 2560 nicht zurückspielen.**

Das CNC-Profil erlaubt XYZ-Homing nach ausdrücklicher Nutzervorgabe auf Basis angenommener X/Z-Richtungen; die physischen Prüfpunkte bleiben als Nachweis offen.
Für den ersten Test ausschließlich der Y-Achse gibt es zusätzlich
`rf2000_y_homing_test`: Nur Y kann bestromt werden, X/Z/A/B bleiben gesperrt.
Seine konkreten Build-, Monitor-, Jog- und `$HY`-Kommandos stehen in
[y-axis-test.md](y-axis-test.md). Die elektrischen Vorprüfungen dieses Plans
gelten auch dafür; die Y-Richtung und Schalterpolarität müssen vor dessen
Homing geprüft sein. Das Standard-/Y-Testprofil behält seine alte
Mikroauflösung und 5-kHz-Grenze. Für den höheren CNC-Vorschub gilt zusätzlich
der [stufenweise Geschwindigkeits- und Wegtest](geschwindigkeit-optimieren.md).

## 1. Dokumentation, Rückweg und stromloser Aufbau

1. Für diesen Auftrag gilt die bereitgestellte alte RF2000-Repetier-Konfiguration nach Nutzervorgabe als funktionierend; bei Zuordnungskonflikten hat ihr RF2000v2-Block Vorrang vor dem ergänzenden Schaltplan. Aufdruck und Seriennummer dienen der Zuordnung des Prüfprotokolls; ein erneuter Revisionsabgleich ist keine offene Vorbedingung.
2. Vor einer erwogenen Programmierung bestehende Flash-/EEPROM-Inhalte und Fuses mit einem geeigneten, zur Platine passenden Verfahren sichern und die Sicherung verifizieren. Bootloader und Fuses nicht ungeprüft überschreiben. Das bereitgestellte Build enthält keine Freigabe, den alten Druckerstand zu verlieren.
3. Die maßgeblichen Pinzuordnungen im Prüfprotokoll verwenden: DRV4 PA4/PA6; OUT3 über PC4 LOW; PA7/D29 als Watchdog-Anschluss im Zustand Eingang ohne Pullup entsprechend `HAL::stopWatchdog()`; PC0/D37 bleibt ES2/Y-Min-Eingang. PA7 und PC4 werden nicht als gemeinsames OUT3 behandelt. Beim stromlosen Aufbau Motor-/Sensorleitungen und gesteckte Verbinder auf Beschädigung oder Fehlanschluss prüfen.
4. Die ergänzend aus dem Plan übernommenen 0,033-Ω-Shunts R80–R89 für die Stromberechnung verwenden. Die später gemessenen Wicklungsströme gegen Motorgrenzen und diese Sollwerte prüfen.
5. Endschalterstecker und Kabel verfolgen. ES1/2/4 sind zweipolige Signale gegen VSS; ES3 ist vierpolig und kann eine aktive Sensorplatine versorgen. Pinreihenfolge anhand der realen Platine verifizieren. NO/NC, Versorgung, Betätigungsrichtung und Kabelbruchzustand separat aufschreiben. Keine 24 V auf AVR-Eingänge geben.
6. Unabhängige Abschaltung nachweisen. Ein serielles `!`, Ctrl-X oder USB-Abziehen ist kein unabhängiger Not-Aus.

**Abbruch:** Fehlende Rücksicherung, beschädigte oder falsch gesteckte Leitung, unbekannte externe Sensorversorgung oder unwirksame unabhängige Abschaltung.

## 2. Logikbetrieb ohne Motoren und Lasten

Alle fünf Motoren und alle Leistungslasten bleiben abgetrennt. Nach den Vorbereitungen aus Stufe 1 und mit einem bewusst gewählten Programmierverfahren den geprüften Testbuild einsetzen. AVR-Programmer/Bootloader-Einstellungen aus der tatsächlichen Platine bestimmen; keine automatische Flashaktion aus diesem Dokument ableiten.

- 5 V, 3,3 V und gegebenenfalls Motorversorgung mit passender Strombegrenzung prüfen. DRV8711-Logik und Registerzugriff benötigen eine gültige Versorgung und Wake-Sequenz; ein USB-Dialog allein belegt dies nicht.
- RESET beobachten und stabilen Lauf prüfen. PA7 ist gemäß RF2000v2-Repetier `WATCHDOG_PIN` und bleibt ohne Pullup hochohmig, entsprechend `HAL::stopWatchdog()`; GRBL erzeugt keinen aktiven Watchdog-Takt. PC0 bleibt ES2/Y-Min-Eingang. Unerwartete Watchdog-Neustarts untersuchen; keine Taktimpulse auf einem Limit- oder Leistungsausgang als Umgehung erzeugen.
- HZ1/PB4, HZ2/PH6, HZ3/PE2, P230/PE5, OUT1/PA3, OUT2/PA5, OUT3/PC4, OUT4/PC5, OUT5/PF4 und RGB/PH3–PH5 mit Oszilloskop/Logikanalysator messen. Sie müssen über Reset, Boot, Initialisierung, Alarm und M3/M4/M7/M8 inaktiv bleiben. PA7 separat auf Eingang ohne Pullup prüfen. Hochohmige Resetphasen anhand der externen Beschaltung beurteilen; bei einem tatsächlichen HIGH-Puls keine Last anschließen.
- STEP aller fünf Kanäle darf ohne bewusstes Bewegungskommando keine Flanken zeigen. SLEEPn aller Kanäle muss nach der Initialisierung inaktiv LOW sein; kurze kontrollierte Wake-Phasen für Registerkonfiguration nur ohne STEP und bei deaktivierter Leistungsfreigabe. Reserve A/B bleibt nach der Initialisierung LOW und deren CTRL.ENBL bleibt gesperrt.
- Seriell die GRBL-Kennung, Achsenzahl XYZ und Einstellungen lesen. `$22=1` und `$H` müssen im Standardbuild zurückgewiesen werden. Im bewusst gewählten Y-Profil ist ausschließlich die sitzungsbezogene Freigabe `$22=1` für `$HY` vorgesehen; `$H`, `$HX` und `$HZ` bleiben gesperrt. `$X` darf weder einen Fehlerzustand elektrisch beseitigen noch Reserveachsen freigeben. `G1 A...` und `G1 B...` dürfen keine Reservebewegung auslösen.

**Abbruch:** Motorfreigabe ohne beabsichtigten Test, unerwartete STEP-Flanke, Netz-/Heizausgang aktiv, wiederholter Reset oder umgehbare Homing-Sperre.

## 3. SPI- und Treiberprüfung

Oszilloskop/Logikanalysator gemeinsam an Masse, SCLK/PD5, SDATI/PD6, SDATO/PD4, SCS und SLEEPn anschließen. Nur ausreichend hochohmige und für die Signalspannung geeignete Tastköpfe verwenden.

1. HIGH-aktive SCS-Phase, LOW-Ruhetakt und genau 16 MSB-first Takte pro Schreibwort bestätigen. Es darf beim Lesen immer nur ein SCS HIGH sein; SDATO muss echte wechselnde Daten liefern und darf nicht lediglich den Pullupwert abbilden.
2. RESET_DRV/PE7: HIGH-Puls und anschließende Wartezeit entsprechend Implementierung prüfen. **Reset muss bei wachen Treibern erfolgen**, da TI §7.4.1 Reset im Sleep ausschließt. Vor dieser Wake-Phase muss CTRL.ENBL=0 für alle fünf Kanäle geschrieben und verifiziert worden sein; es darf keine STEP-Flanke geben. UVLO erst im wachen Zustand löschen, Ursache einer tatsächlichen Unterspannung nicht übergehen. Wake-up-Zeit ab SLEEPn-HIGH messen (Datenblatt maximal 1 ms; Port verwendet längere Wartezeit).
3. Jeden der fünf Treiber einzeln auslesen. Im Standard-/Y-Testprofil gilt
   CTRL.MODE X/Y/A/B 1/32, Z 1/16. Im aktuellen CNC-Profil gilt dagegen
   **XYZ zunächst 1/256 (`CTRL=0xE41`)**, anschließend automatische
   Stufen bis 1/8 (`0xE19`). Reserve A/B bleiben 1/32 und ENBL=0 (`0xE28`). ISGAIN ist als 2 codiert und bedeutet
   Verstärkung 20; TORQUE bleibt X=110, Y=110, Z=105, A/B=90.
   OFF/BLANK/DECAY/STALL/DRIVE gegen die Quellwerte vergleichen. Der
   read-only Befehl **`$DRV`** meldet den gespeicherten Starttest:
   `[DRV:READY=1|FAULT=0|READBACK=0|LIMITS=0|STATUS=0,0,0,0,0]` ist der
   normale fehlerfreie Ausgangspunkt. Werte sind dezimal; FAULT ist eine
   Bitmaske (1=FAULTn, 2=Readback, 4=STATUS-Leistungsfehler), READBACK Bits
   0–4 entsprechen DRV1–5. STATUS sind gespeicherte Startwerte, keine neue
   SPI-Messung während der Bewegung. Der Befehl ersetzt den Logikanalysator nicht.
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
- Timer-Overflow/Pulsendende, Reset während eines Pulses, Alarm während einer Bewegung und UART-Last untersuchen. Im normalen Betrieb keine verkürzten oder zusätzlichen Pulse und kein ständig HIGH bleibender STEP. Eine Fehler-/Resetabschaltung darf einen laufenden Puls bewusst verkürzen; sie darf keine spätere STEP-Flanke zulassen.
- Maximale im gewählten Build eingestellte Schrittfrequenz anhand Feedrate
  und tatsächlicher MODE-Stufe bestimmen. Standard-/Y-Testprofile bleiben
  bei 5 kHz. Das adaptive CNC-Profil begrenzt Achs-/Bahnfeed auf 1000 mm/min
  und verwendet ein 12-kHz-Pulsbudget. Bei 1/8 und 1000 sind nominal
  10.666,7 STEP/s nötig. Die virtuelle Kalibrierung 20480 ist nicht die
  physische STEP/mm-Zahl jeder Stufe.
- MODE-Wechsel gemeinsam mit STEP und SCS beobachten: kein aktiver Puls
  während Schreiben/Readback, keine neue Flanke vor bestätigten Registern,
  ausreichend MODE-Setup/Hold. Den ersten Schritt nach einem nicht ausgerichteten MODE-Wechsel
  gemäß TI als Weg zur nächsten gültigen Indexerposition auswerten.
- Beim Beschleunigen/Abbremsen alle Stufen und Hysterese prüfen. Langsame
  Nebenachsen, Richtungswechsel, kurze CAM-Segmente und Kreise beobachten.
  Positionsvergleich muss phasengewichtete Wege prüfen; die reine Anzahl
  von STEP-Flanken ist bei wechselnder Auflösung nicht ausreichend.
- Feine Restwege dürfen bei regulärem Halt abgeschlossen werden. Fehler oder
  Reset dürfen keine nachträgliche Korrekturfahrt starten. Limits und FAULT
  gezielt auch während eines MODE-Transfers testen. Die Zeit für Transfers
  und Restwegabschluss gemeinsam mit der tatsächlichen Fahrzeit messen.

Die Softwaremesswerte und der genaue geprüfte ELF-Stand stehen in
[validation.md](validation.md). Die früheren 18-kHz-Messungen des festen
1/4-Profils qualifizieren die adaptive Version nicht. Auflösung ist keine
mechanische Genauigkeit; der reale Test folgt zusätzlich
[adaptive-mikroschritte.md](adaptive-mikroschritte.md) und
[geschwindigkeit-optimieren.md](geschwindigkeit-optimieren.md).

## 6. Erster einzelner Motor, dann XYZ

Diese Stufe ist erst nach erfolgreicher elektrischer Prüfung vertretbar. Die testende Person benötigt unmittelbaren Zugriff auf einen **unabhängigen Totmann-/Not-Aus**, freie Sicht und einen mechanisch begrenzten, kollisionsfreien Testweg. Spindel und Heizer bleiben abgetrennt; A/B bleiben abgetrennt und deaktiviert. Keine Homing-Fahrt.

1. Spannungsfrei nur X-Motor anschließen; Z gegen Absinken sichern. Motornenndaten und gemessenen Shunt gegen den berechneten Spitzenstrom prüfen. Ist ein zulässiger Strom nicht belegt, noch keinen Motor bestromen.
2. Ohne STEP-Fahrt Freigabe, Stromaufnahme und Erwärmung kurz beobachten. Mit geeigneter Stromsonde beziehungsweise sauberer Shuntmessung tatsächlichen Wicklungsstrom verifizieren; Netzteilanzeige allein ist kein Phasenstrommessgerät. Bei Fault, ungewöhnlichem Geräusch, schneller Erwärmung oder unerwartetem Haltemoment sofort abschalten.
3. Eine einzelne sehr kurze, langsam begrenzte XYZ-Jogbewegung unter Totmannaufsicht ausführen, etwa **0,1 mm** in der jeweils zuvor als kollisionsfrei bewerteten Richtung. Die konkrete Richtung erst am Gerät festlegen; hier stehen bewusst keine blind ausführbaren G-Code-Fahrbefehle. Nur eine Bewegung im Puffer, sofortige Abschaltung erreichbar.
4. Bewegungsrichtung, tatsächlichen Weg und Stillstand nach Ende prüfen; Gegenrichtung erst nach positivem Ergebnis. Limits bei minimaler Geschwindigkeit und ausreichendem Abstand kontrolliert nachweisen. Hard-Limits stoppen abrupt und sichern die Z-Last nicht gegen Absinken.
5. Spannung aus; X trennen, Y anschließen und entsprechend prüfen. Danach Z mit besonderer Sicherung gegen Absinken und Kollision testen. Erst danach die drei Motoren gemeinsam anschließen.
6. Schritte/mm mit Messuhr bestätigen. Für unveränderte Originalmechanik
   gelten die Testprofil-Startwerte X/Y 152,38 und Z 1280. Für die
   vorgegebene CNC-Mechanik mit SFU1605/2:1 und adaptiven Registern
   gilt dagegen ein festes virtuelles Raster **20480/20480/20480**. Nicht zwischen diesen Profilen
   mischen. Zuerst langsame kleine Wege, dann 10/50-mm-Messung und
   wiederholte Rückkehr zum Messbezug prüfen. Strom und Temperatur
   anschließend stufenweise unter einer kleinen realen Last beobachten.
   Geschwindigkeit und Beschleunigung getrennt nach dem
   [Geschwindigkeits-Testplan](geschwindigkeit-optimieren.md) erhöhen.

Das Standard-Testprofil hat keinen softwareseitigen Freifahrablauf. Im
CNC-Profil ist nach Hard Limit hingegen der ausdrücklich angeforderte
Ablauf **Reset → `$LR` → neu referenzieren** möglich, wenn genau ein
physischer Schalter aktiv ist, die Treiber bereit sind und der Rückzugsweg
frei ist. Er fährt höchstens 2 mm mit bis zu 20 mm/min und bleibt danach
im Alarm; es gibt keine automatische Rückfahrt oder Jobfortsetzung.
[Einzelheiten und Abbruchbedingungen](cnc-gsender.md#nach-einem-hard-limit-freifahren).
Wiederanlauf nach Fault/Reset nur nach Ursachenklärung und erneuter Positionsprüfung.

## 7. Homing bleibt ein eigener Freigabeschritt

Das bisherige XYZ-Testprofil hält `RF2000_HOMING_VERIFIED=0`; `$22` oder
EEPROM-Werte umgehen dessen Sperre nicht. Das aktuelle CNC-Profil ist nach
ausdrücklicher Nutzeranweisung mit angenommenen X/Z-Richtungen freigegeben;
siehe [cnc-gsender.md](cnc-gsender.md). Es erlaubt `$H` sowie `$HX`/`$HY`/`$HZ`
bei `$22=1` und freien Eingängen. Die Mikroauflösungs-/Timingänderung liefert
keinen neuen physischen Richtungs- oder Schalternachweis. Ein ausgefülltes
Prüfprotokoll dokumentiert die Prüfung aller Richtungen und Kontakte.

Das zusätzliche Y-Testprofil ist eine getrennte Ausnahme nur für Y: nach
realer Prüfung von Y-Richtung und Y-Min-Polarität kann ausschließlich `$HY`
mit `$22=1` für die aktuelle Sitzung freigegeben werden. Der
[Y-Testablauf](y-axis-test.md) beschreibt die Einzelkommandos. Die erste
Referenzsuche läuft nach Nutzervorgabe ohne Gesamtweg- oder Zeitbegrenzung
bis zum Y-Min-Kontakt. Ein fehlender erster Kontakt stoppt diese Suche
nicht automatisch. Nach Kontakt folgen Rückzug, langsame zweite Annäherung
und abschließender Rückzug; die zweite Annäherung bleibt auf 5 mm begrenzt.
60/20 mm/min und 1 mm Rückzug sind bewusste Prüfstandswerte gegenüber der
alten Repetier-Konfiguration; der Y-Testablauf nennt beide Wertesätze.
Bereits betätigte Schalter verhindern den Start. `!` stoppt einen laufenden
Homingzyklus nicht; Ctrl-X sendet den Software-Abbruch, Ctrl-C beendet im
PlatformIO-Monitor lediglich das Terminal. Die unabhängige Abschaltung
bleibt erforderlich.

Für die CNC-Anwendung ist **Z-Max mit positiver Z-Referenzrichtung** aus dem Repetier-Fräsmodus der Quellenkandidat. Das bedeutet erst nach realer Richtungsprüfung „vom Werkstück weg“. X/Y sind Quellkandidaten Richtung Min. Nach erfolgreicher Prüfung Homing zuerst ohne Motoren am Logikanalysator, danach einzeln langsam mit Totmannaufsicht testen: freier Start, bereits aktiver Schalter, dauerhaft aktiver Schalter, nicht erreichter/abgetrennter Schalter, Pull-off, zweite langsame Annäherung, erneutes Auslösen und Alarm. Keine automatische Freigabe aus diesem Dokument ableiten.

Beim vollständigen CNC-`$H` fährt zuerst Z vollständig, danach X/Y parallel.
Die zuerst angekommene X/Y-Achse wartet ohne weitere STEP-Pulse am Kontakt,
bis auch die zweite Achse ihren Schalter erreicht hat. Erst dann folgt der
gemeinsame Rückzug. Die langsamen Referenztempi bleiben 100/20 mm/min;
der höhere mögliche Fräsvorschub erhöht sie nicht automatisch.

## Protokollvorlage

| Feld | Eintrag am Prüfplatz |
|---|---|
| Platinenaufdruck / Seriennummer / Fotos | offen |
| Quellcommit / HEX-SHA256 / Buildzeit / Compiler | offen |
| Flash-/EEPROM-/Fuse-Sicherung und Wiederherstellung geprüft | offen |
| Verbindliche Hardwarezuordnung | RF2000v2-Repetier-Konfiguration laut Nutzervorgabe funktionierend; Vorrang bei Abweichung zum Plan |
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
| Physischer Homing-Nachweis, verantwortliche Person, Datum | offen; CNC-Ausführung nach Nutzeranweisung freigegeben |
