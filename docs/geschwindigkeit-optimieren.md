# RF2000-CNC: höhere Achsgeschwindigkeit und Testplan

Stand: 22.10.2026. Diese Anleitung betrifft das CNC-Profil `rf2000_cnc` mit
originaler I851 CTC V3.0, ATmega2560 und fünf DRV8711. XYZ sind aktiv; A/B
bleiben Reserve. Die Frässpindel wird weiterhin manuell bedient.
Die Firmwarekennung lautet **`20261022-rf2000-cnc-speed`**. Die gewählte
CNC-Konfiguration verwendet **fest X/Y 1/4, Z 1/16 und 18 kHz**; adaptive
Mikroschrittumschaltung ist nicht implementiert.

## Warum bisher nur geringe Geschwindigkeiten möglich waren

Der CNC-Vorgänger verwendete **5000 Schritte/s**, nicht
30.000. `error:12` erscheint, wenn Schritte/mm und Achsmaximum zusammen diese
Grenze überschreiten. Der allgemeine gSender-Fehlertext nennt hierfür noch
„30 kHz“; maßgeblich ist die Grenze der tatsächlich gebauten RF2000-Firmware.

Die bisherige Konfiguration war:

| Achse | Mikroschritte | Schritte/mm | Vorgabe mm/min | Rechnerisches Maximum bei 5 kHz |
| --- | ---: | ---: | ---: | ---: |
| X | 1/32 | 2560 | 100 | 117,19 mm/min |
| Y | 1/32 | 2560 | 100 | 117,19 mm/min |
| Z | 1/16 | 1280 | 150 | 234,38 mm/min |

Am 22.10.2026 bestätigte der Nutzer mit der aktuellen Geräteausgabe
**2560 / 2560 / 1280 Schritte/mm** und **100 / 100 / 150 mm/min**. Damit ist
der Ausgangspunkt für diese Migration geklärt. Die noch älteren
**152,380 Schritte/mm** gehören zur ursprünglichen RF2000-Mechanik der
getrennten Testprofile und passen nicht zur vorgegebenen SFU1605-Mechanik.
Solche Werte werden bei der erkannten alten CNC-Version gesondert korrigiert.
`$$` bleibt für den tatsächlichen Inhalt des jeweiligen Geräts maßgeblich.

## Mechanik, Mikroschritte und benötigte Frequenzen

Die Rechnung verwendet 200 Vollschritte pro Motorumdrehung, eine
5-mm-Spindelsteigung und zwei Motorumdrehungen pro Spindelumdrehung:

```text
Weg pro Motorumdrehung = 5 mm / 2 = 2,5 mm
Schritte/mm = 200 × Mikroschritte × 2 / 5
Schrittfrequenz = Schritte/mm × Vorschub in mm/min / 60
```

| Mikroschritte | Schritte/mm | Nomineller Weg pro STEP |
| --- | ---: | ---: |
| 1/32 | 2560 | 0,390625 µm |
| 1/16 | 1280 | 0,78125 µm |
| 1/8 | 640 | 1,5625 µm |
| 1/4 | 320 | 3,125 µm |

Dieser Weg pro STEP ist die **Befehlsauflösung**. Er beschreibt keine
gemessene Positioniergenauigkeit. Motor-Schrittfehler, lastabhängige
Mikroschrittpositionen, Elastizität, Umkehrspiel und die Maschine selbst
beeinflussen das tatsächliche Ergebnis.

Untersucht wurde zunächst **1/8** für X/Y. Damit wären für 2000 mm/min
schon 21,333 kHz und für 3000 mm/min 32 kHz nötig. Das überschreitet die
gewählte 18-kHz-Grenze. Mit 1/8 wären rechnerisch nur 1687,5 mm/min möglich.
Deshalb ist die gewählte feste Auflösung **1/4 für X/Y**: Damit passen
2000–3000 mm/min in 10,667–16 kHz. Z behält zunächst **1/16**, weil dort
Auflösung und Last getrennt zu beurteilen sind.

| Vorschub | X/Y bei 1/4 | Z bei 1/16 | Motordrehzahl |
| --- | ---: | ---: | ---: |
| 1000 mm/min | 5.333 Schritte/s | 21.333 Schritte/s | 400 U/min |
| 2000 mm/min | 10.667 Schritte/s | 42.667 Schritte/s | 800 U/min |
| 2500 mm/min | 13.333 Schritte/s | 53.333 Schritte/s | 1000 U/min |
| 3000 mm/min | 16.000 Schritte/s | 64.000 Schritte/s | 1200 U/min |

Die 2:1-Untersetzung erhöht die erforderliche Motordrehzahl. Ausreichendes
Drehmoment bei 800–1200 U/min ist noch am Gerät nachzuweisen. Die geringere
STEP-Anforderung allein beweist keine verlustfreie mechanische Bewegung.
Die Mikroschritte werden im DRV8711 programmiert; allein `$100` bis `$102`
zu ändern genügt nicht. Die durch TI dokumentierten MODE-Einstellungen
ermöglichen diese feste Wahl. Ein adaptives Umschalten mit laufender
Phasen-/Positionsnachführung bleibt eine mögliche spätere Entwicklung und
ist ausdrücklich kein Bestandteil dieses Profils.

## Konkrete Änderungen

X/Y 1/4 verwendet im DRV8711 `CTRL.MODE=2`: mit freigegebenem Kanal ergibt
sich `CTRL=0xE11`, während der gesperrten Initialisierung `0xE10`. Z bleibt
bei `0xE21` und 1/16. Die A/B-Register bleiben mit `CTRL=0xE28` gesperrt.
STEP-/DIR-/SLEEP- und Endschalterpins werden dadurch nicht verändert.

Die neuen nominalen CNC-Schrittwerte sind **320 / 320 / 1280 Schritte/mm**.
Die gemeinsame Mikroprofildefinition wird sowohl von der DRV8711-
Initialisierung als auch von den GRBL-Vorgaben verwendet.

Die CNC-Schrittausgabe schreibt eine unveränderte Richtung nicht bei jedem
Tick neu. Bei stabiler Richtung beginnt STEP direkt im Timer1-Interrupt;
Timer0 beendet den Puls. Nur der erste tatsächliche STEP nach einem
Richtungswechsel durchläuft zusätzlich den mindestens 5 µs langen
Timer0-Vorlauf. Das gilt auch, wenn dazwischen AMASS-Ticks ohne STEP liegen.
Die AMASS-Schrittinkremente werden im Vordergrund vorbereitet, wodurch
variable 32-Bit-Verschiebungen aus dem zeitkritischen Segmentwechsel
entfallen. AMASS selbst bleibt aktiv. Pulsende, Fehlerprüfung und Schutz
vor Pulsüberlappung bleiben wirksam; es gibt keine Laufzeit-SPI-Zugriffe.

Die CNC-Grenze beträgt **18.000 Schritte/s**. Das entspricht rechnerisch
**3375 mm/min für X/Y** und **843,75 mm/min für Z**. Die getrennten Standard-
und Y-Testprofile behalten ihr altes Mikroprofil und ihre 5-kHz-Grenze.

Ein isolierter Vorversuch des optimierten Pfads erreichte bei 20-kHz-
Anforderung im XYZ-Test durchschnittlich **19.975,87 Schritte/s**. Ein
22-kHz-Versuch bestand nicht. Deshalb werden weder 22 noch 30 kHz als
Produktionsgrenze übernommen; die gewählten 18 kHz lassen Abstand zum
bestandenen Vorversuch. Dieser Versuch ist vom Nachweis des endgültigen
Release-ELF zu unterscheiden. Die finalen Puls-, Plateau-, Mehrachs-,
UART-/LCD- und Fehlerfallmesswerte stehen im [Prüfbericht](validation.md).

Der finale AVR-Lauf bestand 15 Geschwindigkeits-/Fehlerfälle bei 10 µs
eingestellter Pulsbreite sowie zusätzlich den XYZ-Rücklauftest bei 20 µs.
Für die XYZ-Fahrt mit Richtungswechsel und 18-kHz-Anforderung ergaben sich:

| Pulsbreiteneinstellung | Plateau-Mittel | Fenster über 64 Flanken | Kleinster HIGH-Puls | Kleinste LOW-Zeit |
| --- | ---: | ---: | ---: | ---: |
| 10 µs | 17.977,53 Schritte/s | 17.886,15–18.068,89 Schritte/s | 10,625 µs | 24,250 µs |
| 20 µs | 17.977,51 Schritte/s | 17.885,53–18.070,16 Schritte/s | 20,688 µs | 16,625 µs |

Im zusätzlichen 20-µs-Rücklauftest lag der kleinste gemessene DIR-Vorlauf
bei 168,25 µs. Das sind Ergebnisse des simulierten AVR-Programms unter
Kommunikations-/Displaylast, keine Oszilloskopmessungen an der Maschine.
Die Schwankungen der kurzen Fenster entstehen durch Interruptlatenzen;
die Einstellung 18 kHz beschreibt die nominale Frequenzgrenze.

Der gebaute CNC-Stand verwendet 46.076 Byte Flash und 3.836 Byte statisches
SRAM (`.text=45958`, `.data=118`, `.bss=3718`). Sein ELF-SHA256 lautet
`0785cad168cbbb4a712c643ac81fab901868654282091ec0879c0968f5dee1f3`.
Build- und Simulationsprotokolle müssen sich auf diesen ausgelieferten
Stand beziehen; eine maximale mechanische Geschwindigkeit ist damit noch
nicht gemessen.

## EEPROM und bestehende Kalibrierung

Der CNC-Datensatz wechselt von **Version 74 auf 75**. Ein gültiger alter
X/Y-Feinabgleich wird durch acht geteilt, passend zu 1/32 → 1/4; Z bleibt
unverändert. Der bestätigte Satz 2560/2560/1280 ergibt somit 320/320/1280.
Der bekannte veraltete Wert 152,380 wird gesondert auf 320 korrigiert und
als Korrektur gemeldet. Ein blindes Teilen dieses alten Mechanikwerts
würde falsche 19,0475 Schritte/mm erzeugen.

Die Migration meldet:

```text
[MSG:RF2000 EEPROM 74->75; X/Y microsteps 32->4]
[MSG:RF2000 offsets/startup retained; rehome and verify travel]
```

G54–G59, G28/G30, Startzeilen und eigener Buildtext bleiben bei erkannten
CNC-Datensätzen erhalten. Vorhandene Maximaltempi/Beschleunigungen werden
nicht erhöht; zu hohe Werte werden auf die neuen Factory-Vorgaben begrenzt.
Damit bleiben die bestätigten **100 / 100 / 150 mm/min** beim gültigen Update
zunächst erhalten.

Der globale Schreibvorgang setzt zuerst Versionmarker **`0xA5`**, schreibt
Daten und Prüfsumme und setzt zuletzt Version 75. Ein unterbrochener
Schreibvorgang oder beschädigter bekannter CNC-Datensatz wird mit globalen
Vorgaben wiederhergestellt; die getrennten Koordinaten-/Startdaten bleiben
erhalten. Diese Wiederherstellung meldet `settings recovered; offsets/startup
retained` und kann Factory-Tempi ergeben. Bei leerem/fremdem Profil gilt
weiter die vollständige Initialisierung. Deshalb nach jedem Update vor
Bewegungen `$I` und `$$` prüfen.

**Alte `$100=2560` / `$101=2560` nach dieser Firmware niemals zurückspielen.**
Bei den neuen festen 1/4-Registern würden damit achtfach zu große reale Wege
entstehen. Ein pauschales `$RST=*` ist für die erkannte Migration nicht nötig
und würde die gespeicherten Werkstücknullpunkte löschen.

Die GRBL-Maschinenkoordinaten bleiben in der vereinbarten Konvention:
X/Y referenzieren MIN, Z referenziert MAX. Arbeitsnull und Maschinenreferenz
bleiben getrennte Funktionen. Die höhere Geschwindigkeit ändert keine
Referenzrichtung und schaltet Hard Limits nicht ab.

## Motorstrom und Spindeldrehzahl

Die Motor-Typenschildangabe **2,5 A pro Phase** ist kein Anlass, den bisherigen
Stromcode anzuheben. TORQUE bleibt X/Y=110 und Z=105, ISGAIN-Verstärkung 20
bei angenommenen 0,033 Ω Messwiderständen. Daraus folgen rechnerisch:

| Achsen | Phasen-Spitzenstrom | Sinusförmiger RMS-Wert |
| --- | ---: | ---: |
| X/Y | ca. 1,790 A | ca. 1,266 A |
| Z | ca. 1,709 A | ca. 1,208 A |

TORQUE.SMPLTH=1 bleibt bei 100 µs Back-EMF-Abtastzeit. Bei 18 kHz beträgt
das STEP-Intervall nur etwa 55,6 µs; daraus wird keine zuverlässige
Back-EMF-Stallerkennung abgeleitet. STALLn ersetzt hier weder Endschalter
noch eine Messung von Schrittverlusten. Die Stromregelung ist von dieser
diagnostischen Back-EMF-Abtastung zu unterscheiden.

Das sind typische Rechenwerte, keine Strommessungen. Mehr Drehzahl kann
auch an Versorgung, Wicklungsinduktivität, Riemenspannung und verfügbarer
Motorkraft scheitern. Ein höherer Strom ersetzt diese Prüfung nicht.

**24.000 U/min betreffen die Frässpindel.** Ihre Drehzahl ist unabhängig
von der Schrittmotordrehzahl. Für einen 6-mm-Fräser ergibt sich ein sinnvoller
Schnittvorschub erst aus Werkzeug, Schneidenzahl, Zahnvorschub, Eingriff,
Material und Maschinensteifigkeit. Die erreichbare Leerfahrt ist keine
Freigabe, Aluminium mit demselben Vorschub zu fräsen.

## Konservative erste Einstellungen

Die Factory-Vorgaben für ein neues bzw. wiederhergestelltes CNC-Profil sind
**600 / 600 / 300 mm/min** bei **10 / 10 / 10 mm/s²**. Sie liegen deutlich
unter den rechnerischen Frequenzobergrenzen. Beim gültigen Update bleiben
niedrigere gespeicherte Werte zunächst erhalten.

Für die ersten Wegmessungen nach der Firmwareumstellung können die
bisherigen langsamen Grenzwerte verwendet werden. Dazu im Stillstand
die folgenden Zeilen einzeln senden:

```text
$110=100
$111=100
$112=150
$120=10
$121=10
$122=10
$$
```

Das setzt noch keine Achse in Bewegung. Die Werte sind mm/min für
`$110`–`$112` und mm/s² für `$120`–`$122`. Vor dem Fahren muss `$$` zusätzlich
die zur tatsächlich programmierten Mikroschrittauflösung passende
Kalibrierung zeigen. Nicht versuchsweise nur die Schrittzahl ändern, um
eine Schrittfrequenzwarnung zu umgehen.

Nach bestandener langsamer Wegprüfung können im Stillstand die neuen
Startvorgaben gewählt werden, jeweils eine Zeile senden und `ok` abwarten:

```text
$110=600
$111=600
$112=300
$120=10
$121=10
$122=10
$$
```

Höhere Tempi erst stufenweise testen. Für Z bedeuten 1/16 und 18 kHz maximal
843,75 mm/min auf der Frequenzseite; 2000–3000 mm/min sind damit für Z nicht
freigegeben. Auch X/Y 2000–3000 erfordern erst einen bestandenen
mechanischen Test einschließlich Rückkehrmessung.

## Hardwaretest in acht Stufen

Die folgenden Bewegungen führt ausschließlich der Bediener an der Maschine
aus. Die Cloud hat keinen Hardwaretest durchgeführt und führt keine
automatische reale Bewegung aus. Fräser entfernen, Frässpindel am Gerät
ausschalten und freie Fahrwege herstellen. `M5`, Pause und Reset schalten
die manuelle Spindel nicht aus.

1. **Konfiguration und kleine Bewegung prüfen.** Vor dem Update `$I`, `$$`,
   `$#` und `$N` sichern. Nach dem Update Firmwarekennung, Migrationsmeldung,
   Schritte/mm und Grenzwerte prüfen. Mit `$DRV` freie Schalter und bereite
   Treiber kontrollieren. Jeweils nur eine Achse mit 0,1 bzw. 1 mm bei F20
   bewegen; Richtung und tatsächliche Bewegung beobachten. Beispiel:

   ```text
   $J=G21G91X1F20
   $J=G21G91X-1F20
   ```

2. **Tatsächlichen Weg messen.** X/Y zunächst über 10 mm, danach über
   50 mm messen; Z nur mit ausreichend freiem Weg über eine geeignete
   Länge. Immer denselben Messbezug und eine Bewegungsrichtung verwenden,
   um Umkehrspiel nicht als Schrittzahlfehler zu behandeln. Eine Korrektur
   lautet `neue Schritte/mm = alte Schritte/mm × Sollweg / Istweg`.
   Grobe Faktorenfehler zuerst anhand Mechanik und Mikroschritten klären.

3. **Rückkehr und Schrittverluste prüfen.** Eine Messuhr oder einen
   ausreichend wiederholbaren Messbezug an einer freien Position verwenden.
   Zehnmal einen begrenzten Weg hin und zurück fahren, jeweils Stillstand
   abwarten, und die Rückkehr messen. Auch unter der später üblichen
   Achslast prüfen. GRBL besitzt hier keinen Positionsgeber: Die angezeigte
   Rückkehr zu 0 allein beweist nicht, dass keine Schritte verloren gingen.

4. **Geschwindigkeit je Achse erhöhen.** Beschleunigung zunächst bei
   10 mm/s² belassen. Einen begrenzten Weg verwenden, der das gewünschte
   Tempo überhaupt erreicht. Aus den langsamen Startwerten in kleinen
   Stufen erhöhen, etwa 100, 200, 300, 400, 600 mm/min; höhere Stufen nur
   innerhalb der dokumentierten Firmwaregrenze und nach bestandener
   vorheriger Stufe. Pro Stufe Rückkehrmessung, Motorgeräusch und Verhalten
   prüfen. Bei Schrittverlusten oder Störungen zur letzten bestandenen
   Stufe zurückgehen und Reserve einplanen. `$110`/`$111`/`$112` begrenzen
   auch ein höheres F; zum Prüfen müssen Achsmaximum und F zusammenpassen.
   Bei 10 mm/s² benötigt eine Fahrt mit 2000 mm/min schon je 55,6 mm
   für Beschleunigung und Bremsung, bei 3000 mm/min je 125 mm. Eine kurze
   10- oder 50-mm-Fahrt erreicht diese Tempi daher nicht. Reicht der freie
   Achsweg nicht aus, zunächst mit dem niedrigeren bestandenen Tempo die
   Beschleunigung separat prüfen; nicht allein aus einer hohen F-Anzeige
   auf eine erreichte Geschwindigkeit schließen.

5. **Beschleunigung separat optimieren.** Erst nach Wahl eines zuverlässig
   bestandenen Tempos Beschleunigung in kleinen Schritten erhöhen. Kurze
   Wechselbewegungen und längere Fahrten messen. Tempo und Beschleunigung
   nicht gleichzeitig erhöhen. Als Bewegungsmodell gilt
   `Bremsweg = Geschwindigkeit² / (2 × Beschleunigung)`, wobei die
   Geschwindigkeit dafür in mm/s eingesetzt wird. Endschalter ersetzen
   keine eingeplante Bremsstrecke.

6. **Gemeinsame Bewegungen und Kommunikation prüfen.** Danach X/Y und XYZ
   gemeinsam mit dem späteren Senderbetrieb testen. Gleiche F-Angaben
   beschreiben bei normalem G-Code den Bahn- bzw. Vektorvorschub; sie sind
   nicht automatisch die Geschwindigkeit jeder beteiligten Achse.
   Pause/Fortsetzen einer normalen Fahrt und Jog-Abbruch kontrollieren.

7. **Referenzfahrt und Grenzen erneut prüfen.** `$HX`, `$HY` und `$HZ`
   einzeln prüfen, dann `$H`: erst Z-Max, danach X/Y-Min. Der zuerst
   erreichte X/Y-Schalter bleibt beim parallelen Suchlauf betätigt, bis
   auch die andere Achse ihren Kontakt erreicht hat; danach folgt der
   gemeinsame Rückzug. Suche, Feinanfahrt und abschließendes Freifahren
   müssen vollständig funktionieren. Hard Limits bei geringer Testfahrt
   durch gezieltes Betätigen eines Schalters prüfen, ohne in einen
   mechanischen Anschlag zu fahren. Für den Rückzug nach Alarm den
   dokumentierten Ablauf Reset → `$LR` → neu referenzieren verwenden.
   Soft Limits erst nach geprüften Fahrwegen `$130`–`$132` und erfolgreicher
   Referenzfahrt aktivieren und im freien Bereich kontrollieren.

8. **Erst danach Fräsversuche.** Werkstück und Werkzeug einrichten,
   Werkstücknull setzen und zunächst eine Leerfahrt des Programms mit
   Abstand zum Werkstück prüfen. Erste Aluminiumversuche mit kleinem
   Eingriff und zu Werkzeug/Material passenden Schnittdaten beginnen.
   Die Spindeldrehzahl von bis zu 24.000 U/min ist keine eigenständige
   Vorschubvorgabe.

Für jede Achse sollten Firmwarekennung, Mikroauflösung, Schritte/mm,
Maximaltempo, Beschleunigung, gemessener Weg, Rückkehrfehler und gewählte
Reserve protokolliert werden. Nur eine tatsächlich bestandene Kombination
aus diesen Werten als Maschinenkonfiguration speichern.

## Weiterführende Projektdokumente

- [Befehle und Einstellungen](befehle.md)
- [CNC und gSender](cnc-gsender.md)
- [Bedienfeld und Menü](bedienfeld-menue.md)
- [DRV8711-Register und Stromberechnung](drv8711.md)
- [Build- und Simulationsnachweise](validation.md)
