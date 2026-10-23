# Adaptive Mikroschritte bis 1000 mm/min

Das CNC-Profil **`rf2000_cnc`** mit der Kennung
**`20261022-rf2000-cnc-adaptive`** verwendet die DRV8711-Auflösungen
**1/256, 1/128, 1/64, 1/32, 1/16 und 1/8** geschwindigkeitsabhängig.
XYZ bleiben die drei aktiven Achsen; A/B bleiben gesperrt. Display,
Tastenfeld, gSender-Befehle, Endschalter und manuelle Spindelbedienung
bleiben erhalten. Die neuen Softwareprüfungen und deren genaue Reichweite
stehen im [Prüfbericht](validation.md).

## Was automatisch geschieht

Langsame Positionierungen und feine Zustellungen verwenden feinere
Mikroschritte. Beim Beschleunigen wechselt die Firmware auf gröbere Stufen,
damit der AVR keine unzulässig hohe Zahl an STEP-Ereignissen erzeugen muss.
Beim Abbremsen wird wieder verfeinert. Entscheidend ist die Geschwindigkeit
des tatsächlich vorbereiteten Bewegungsabschnitts, nicht allein der im
G-Code angegebene F-Wert. Eine kurze Fahrt kann ihr angefordertes Tempo
wegen der Beschleunigung gar nicht erreichen.

Ein gemeinsames Mikroprofil gilt für XYZ; die am stärksten beanspruchte
bewegte Achse bestimmt die nötige Auflösung. Eine langsame Nebenachse
verliert dadurch keine Restwege: Die Firmware führt sie im festen internen
Raster mit und verteilt die Hardware-Schritte über den Abschnitt.
Die Auswahl vergröbert oberhalb von 12 kHz und verfeinert erst, wenn die
feinere Stufe höchstens 9 kHz erfordern würde. Diese Hysterese verhindert
häufiges Hin- und Herschalten an den Übergängen.

Die folgende Tabelle zeigt die nominalen Stufen für 200 Vollschritte pro
Umdrehung, SFU1605 mit 5 mm Steigung und zwei Motorumdrehungen je
Spindelumdrehung. Die Geschwindigkeiten sind Orientierungswerte bei einem
12-kHz-Pulsbudget; Verfeinerung, Richtungswechsel und konkrete Abschnitte
können den Umschaltpunkt beeinflussen.

| Hardware-Auflösung | Physische STEP/mm | Nominaler Weg pro STEP | Oberer Bereich bei 12 kHz |
| --- | ---: | ---: | ---: |
| 1/256 | 20.480 | 0,048828 µm | ca. 35,16 mm/min |
| 1/128 | 10.240 | 0,097656 µm | ca. 70,31 mm/min |
| 1/64 | 5.120 | 0,195313 µm | ca. 140,63 mm/min |
| 1/32 | 2.560 | 0,390625 µm | ca. 281,25 mm/min |
| 1/16 | 1.280 | 0,781250 µm | ca. 562,50 mm/min |
| 1/8 | 640 | 1,562500 µm | bis zur Firmwaregrenze 1000 mm/min |

Bei 1000 mm/min verlangt 1/8 etwa **10.666,7 STEP/s**. 1/4, Halb- und
Vollschritt liefern für dieses Geschwindigkeitsziel keinen nötigen Vorteil
und werden deshalb nicht verwendet. Die Fabrik-Maximalgeschwindigkeiten
bleiben **600/600/300 mm/min**, die Beschleunigung **10 mm/s²**.
Die Achsmaxima lassen sich nach bestandenem Hardwaretest bis höchstens
**1000 mm/min** einstellen. Auch der Bahnfeed bei Diagonalen wird auf
1000 mm/min begrenzt. Gespeicherte niedrigere Grenzen bleiben wirksam;
ein F1000 übergeht beispielsweise `$111=100` nicht.

## Warum die Schritte/mm konstant bleiben

GRBL plant und meldet Positionen in einem festen virtuellen **1/256-Raster**.
Für die vorgegebene Mechanik sind das auf allen Achsen nominal
**20.480 Einheiten/mm**. `$100`, `$101` und `$102` ändern sich beim Umschalten
nicht. Bei 1/8 kann ein gewöhnlicher Hardware-Schritt beispielsweise
32 dieser Einheiten zurücklegen. GRBL zählt dessen tatsächlichen Weg,
nicht pauschal einen virtuellen Schritt pro Puls.

Der AVR erzeugt dabei keine 341.333 feinen Interrupts/s, die 1/256 bei
1000 mm/min erfordern würde. Die vorbereiteten Abschnitte werden direkt
in gröbere, zeitlich verteilte Hardware-Schritte übersetzt. Restwege bleiben
zwischen den absoluten Abschnittszielen erhalten, auch über kontinuierlich
verbundene CAM-Zeilen und Kreisbögen hinweg. Erst an einem Übergang mit Geschwindigkeit null, nach einer kontrollierten
Pause oder beim regulären Endhalt wird
das Ziel im Feinraster abgeschlossen. Es gibt keinen erzwungenen Halt nach
jeder CAM-Zeile. Ein Alarm
oder Reset startet keine nachträgliche Korrekturbewegung.

Die elektrische DRV8711-Phase wird unabhängig von den GRBL-Koordinaten
geführt. Das ist nötig, weil TI beim nächsten STEP nach einem MODE-Wechsel
zur nächsten gültigen Position der neuen Auflösung fortschaltet. Ein erster
Schritt kann deshalb kürzer sein als ein normaler Schritt dieser Stufe.
Referenzieren oder GRBL-Softreset dürfen den angenommenen elektrischen
Phasenstand nicht einfach auf null setzen. Ein tatsächlicher gemeinsamer
Treiber-RESET startet den Indexer bei der dokumentierten 45°-Position.

Im normalen Verlauf werden Restwege in Fahrtrichtung mitgeführt; ein
Auflösungswechsel soll keine künstlichen Rückwärts-Schritte erzeugen.
An den Abschnitts-Endpunkten beträgt der nominale vom Quantisierer
mitgeführte Restweg höchstens 31 feine Einheiten, also etwa **1,514 µm je
Achse** bei dieser Mechanik. Das ist weniger als ein 1/8-Schritt von
1,5625 µm. Diese Grenze beschreibt ausschließlich den Restweg an den
Abschnitts-Endpunkten. Die stufenweise Interpolation zwischen den
Endpunkten erzeugt zusätzliche Abweichungen; daraus folgt keine
entsprechende Genauigkeitsgrenze für die gesamte Bahn oder die Mechanik.

## Umschalten ohne laufenden STEP-Puls

Ein Auflösungswechsel erfolgt an einer Abschnittsgrenze mit beendetem
STEP-Puls und angehaltenem Schritttimer. Der Vordergrund schreibt und liest
die CTRL-MODE-Felder zurück. Erst nach bestätigten Registerwerten und erneuter
Fehler-/Resetprüfung darf der nächste Abschnitt starten. SPI läuft nicht
im Schrittinterrupt; die Fehler- und Endschalterüberwachung bleibt während
der Transaktion ansprechbar.

Referenzfahrten verwenden während jeder Phase ein festes Profil: bei den
100/20-mm/min-Vorgaben 1/32. Erhöhte Such-/Feintempi können vor Beginn der
Phase 1/16 oder 1/8 erfordern. Innerhalb einer Phase wird das Profil nicht
gewechselt, damit ein bereits am Schalter gestoppter Motor konsistent
bleibt. Das begrenzte Freifahren `$LR` bei F20 verwendet 1/256. `$MS` liest
das momentane Hardware-Profil und die festgelegten Grenzen; ein unbekanntes
Profil wird als 0 angezeigt. Das Display zeigt weiterhin die aktuelle Aktion.

Beispiel unmittelbar nach erfolgreichem Start:

```text
$MS
[MS:VIRTUAL=256|MICRO=256|MAX=1000|PULSE_MAX=12000]
ok
```

`VIRTUAL=256` ist das konstante Koordinatenraster, `MICRO=256` bedeutet
momentan 1/256 am Treiber. Während anderer Stufen kann dort beispielsweise
`MICRO=32` oder `MICRO=8` stehen. `MAX` ist die Obergrenze in mm/min,
`PULSE_MAX` das physische Pulsbudget in STEP/s. Der Befehl liest den
bestätigten Cache; er führt keine neue Messung und keine Bewegung aus.

Die Umschaltung benötigt Zeit. Registertransfer, Rücklesen, Interruptlast
und kleine Restwegabschlüsse können die reale Bahndauer verlängern,
besonders bei kurzen Segmenten und häufigen Wechseln. Ein Restwegabschluss
kann bis zu 31 feine Pulse je Achse bei ungefähr 6 kHz hinzufügen, nominal
rund 5,2 ms zuzüglich weiterer Verarbeitung. Die eingestellten
1000 mm/min sind eine Obergrenze; daraus folgt keine Garantie, dass jede
kurze Bahn dieses Tempo erreicht. Der Prüfbericht unterscheidet deshalb
angeforderte Geschwindigkeit, tatsächliche Ausgabe und Fehlerfälle.

TORQUE, ISGAIN, Resetsequenz und Motorpins ändern sich durch die Automatik
nicht. Ein fehlerhaftes MODE-Readback sperrt die Bewegung. A/B erhalten
während des Umschaltens keine Freigabe. STALLn ist weiterhin keine
Positionsrückmeldung; die firmwareseitig richtige Schrittzählung erkennt
keine mechanisch verlorenen Schritte.

## Update von den bisherigen CNC-Versionen

Vor dem Aufspielen `$I`, `$$`, `$#` und `$N` sichern. Anschließend `$I` und
`$$` lesen. Die neue CNC-Profilversion ist **76**:

| Erkannter alter Datensatz | Umrechnung zum virtuellen 1/256-Raster |
| --- | --- |
| Version 75: X/Y 320, Z 1280 | X/Y ×64, Z ×16 → nominal 20480/20480/20480 |
| Version 74: X/Y 2560, Z 1280 | X/Y ×8, Z ×16 → nominal 20480/20480/20480 |
| Überholter X/Y-Mechanikwert 152,380 in Version 74 | Gesonderte Korrektur auf nominal 20480 mit Meldung |

Ein vorhandener Feinabgleich wird proportional umgerechnet. Koordinaten
G54–G59, G28/G30, Startzeilen und eigener Buildtext bleiben bei erkannten
CNC-Datensätzen erhalten. Die Migration erhöht gespeicherte niedrigere
Geschwindigkeiten nicht; die bisherigen 100/100/150 bleiben zunächst
100/100/150. Höhere alte Werte werden konservativ begrenzt.

Der Versionsmarker wird vor einer globalen Aktualisierung auf einen
Transaktionswert gesetzt und erst nach Daten und Prüfsumme auf 76 gesetzt.
Ein erkannter unterbrochener oder beschädigter CNC-Datensatz stellt globale
Vorgaben wieder her und erhält die getrennten Koordinaten-/Startdatensätze.
Ein leerer oder fremder Datensatz wird vollständig initialisiert.
Nach jedem Update deshalb die tatsächlich gemeldeten Werte kontrollieren.

**Alte Werte 320, 1280 oder 2560 nicht aus einer Sicherung zurückspielen.**
`$100`–`$102` beschreiben jetzt das feste virtuelle Raster. Die mechanische
Kalibrierung erfolgt weiterhin mit
`neuer Wert = bisheriger Wert × Sollweg / gemessener Weg`, aber erst nach
Prüfung von Mechanik und Firmwareprofil. Ein pauschales `$RST=*` ist für
die erkannte Migration nicht nötig und löscht die Werkstücknullpunkte.

## Bedienung und erster Test

Die automatische Stufenwahl benötigt keinen neuen G-Code-Befehl.
Positionieren, Referenzieren und Fräsen verwenden die üblichen GRBL-Befehle.
Die kleinen lokalen Jogschritte bleiben 0,01/0,10/0,50 mm bei 10/20/30 mm/min.
Sie werden in Millimetern angegeben, unabhängig von der Hardware-Auflösung.

1. Nach dem Update neue Kennung und nominal 20480/20480/20480 mit `$I` und
   `$$` kontrollieren. Einen erhaltenen Feinabgleich nicht blind durch
   nominale Werte ersetzen. Eingänge und Treiberbereitschaft mit `$DRV`
   prüfen. Die manuelle Frässpindel ausschalten.
2. Langsame 0,1- und 1-mm-Bewegungen jeder Achse mit Messbezug prüfen,
   anschließend eine längere Wegmessung in beiden Richtungen durchführen.
   Erst danach einzeln referenzieren und den gemeinsamen Ablauf prüfen.
3. Auf ausreichend freiem Weg langsame Hin-/Rückfahrten ausführen und die
   tatsächliche Rückkehr messen. Die Bildschirmposition allein beweist
   keine verlorenen Schritte. Die Stufenwahl braucht keine Änderungen von
   `$100`–`$102`.
4. Achsmaximum und F nur stufenweise erhöhen: beispielsweise 100, 200, 300,
   400, 600, 800 und höchstens 1000 mm/min. Tempo und Beschleunigung getrennt
   optimieren. Nach jedem Schritt Geräusch, Rückkehr und Schalterverhalten
   kontrollieren. Bei Störung zur zuletzt bestandenen Stufe zurückgehen.
5. Anschließend XY/XYZ-Bahnen, Kreise und kurze CAM-Segmente ohne Werkzeug
   prüfen. Eine Leerfahrt ersetzt noch keine Prüfung unter Fräslast.
   Arbeitsnullpunkt und Werkzeug zuerst kontrollieren und dann mit kleinen,
   zu Material und Werkzeug passenden Schnittdaten beginnen.

Bei 10 mm/s² braucht eine Fahrt mit 1000 mm/min rund 13,89 mm zum
Beschleunigen und dieselbe Strecke zum Bremsen. Ein Weg deutlich über
27,78 mm ist daher für einen Abschnitt mit tatsächlichem Volltempo nötig.
Die unabhängige Maschinenabschaltung muss erreichbar bleiben; `M5`, Pause
oder Firmware-Reset schalten die manuell bediente Spindel nicht aus.

Die 1/256-Befehlsauflösung von etwa **0,049 µm** ist keine Genauigkeitszusage.
Mikroschritt-Nichtlinearität, Motor-Schrittfehler, Last, Elastizität,
Umkehrspiel und Messmittel bestimmen das reale Ergebnis.

## Builds und Softwareprüfung

Das CNC-Profil wurde mit den beiden vorgesehenen Werkzeugketten gebaut:

| Werkzeugkette | Flash | Statisches SRAM |
| --- | ---: | ---: |
| PlatformIO Atmel AVR 5.1.0 / AVR-GCC 7.3.0 | 50.958 Byte | 3.893 Byte |
| Make / AVR-GCC 14.2.0 | 52.130 Byte | 3.893 Byte |

Der PlatformIO-Build einschließlich Größenprüfung und HEX-Erzeugung
bestand. Die frühere Registry-Sperre ist für diesen aktuellen Build
überwunden. [Builddetails und Hashes](platformio.md#stand-der-cloudprüfung).

`make check-adaptive`, `make check-cnc` und `make check-speed` starten
dieselbe adaptive AVR-Suite mit dem Make-ELF. Für ein lokal mit PlatformIO
gebautes Image ist der direkte Aufruf
`tests/run-adaptive-sim.sh .pio/build/rf2000_cnc/firmware.elf` vorgesehen.
Die Suite muss neben MODE-Auswahl auch die tatsächlich gefahrenen,
phasengewichteten Wege und die Fehlerfälle prüfen. Beide Compiler-Ausgaben haben je 27 adaptive AVR-Fälle bestanden.
Beim Make-ELF bestanden zusätzlich acht Menü-, 20 Freifahr- und zwei
gestaffelte XY-Referenzfälle, insgesamt 57 Fälle auf demselben ELF.
Kommandos, Messwerte und Hashzuordnung stehen im [Prüfbericht](validation.md). Ein Build ist kein
Hardwaretest und keine Messung der tatsächlichen Fräsgenauigkeit.

## Quellen und Nachweise

- TI DRV8711, [SLVSC40H](https://www.ti.com/lit/ds/symlink/drv8711.pdf),
  §7.3.3 für Indexer und MODE-Wechsel, §6.6/6.7 für Signalzeiten,
  §7.4.1 für Sleep und RESET.
- [DRV8711 und Stromwerte](drv8711.md)
- [gSender und EEPROM-Migration](cnc-gsender.md)
- [Geschwindigkeit und Hardwaretest](geschwindigkeit-optimieren.md)
- [Prüfbericht](validation.md)
