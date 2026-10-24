# RF2000-CNC: Geschwindigkeit und Hardwaretest

Stand: 22.10.2026. Das aktuelle CNC-Profil
**`20261024-rf2000-cnc-adaptive-spi`** verwendet automatische Mikroschritte
von **1/256 bis 1/8** und ein festes internes Raster mit nominal
**20480 Einheiten/mm auf X/Y/Z**. Die Maximalgeschwindigkeit ist auf
**1000 mm/min für Einzelachsen und Bahnfeed** begrenzt. Details der Umschaltung stehen in
[Adaptive Mikroschritte](adaptive-mikroschritte.md).

Die Originalplatine I851 CTC V3.0 mit ATmega2560 und fünf DRV8711 bleibt
unverändert; XYZ sind aktiv, A/B bleiben Reserve. Die Frässpindel wird
manuell bedient. Die alten festen Profile und ihre damaligen Grenzwerte
sind nicht mit diesem Firmwarestand austauschbar.

## Mechanik und benötigte Schrittfrequenz

Die vorgegebene Mechanik verwendet 200 Vollschritte pro Motorumdrehung,
5 mm Spindelsteigung und zwei Motorumdrehungen pro Spindelumdrehung:

```text
Weg pro Motorumdrehung = 5 mm / 2 = 2,5 mm
Physische STEP/mm = 200 × Mikroschritte × 2 / 5
Physische Schrittfrequenz = STEP/mm × mm/min / 60
```

Bei 1000 mm/min dreht der Motor mit 400 U/min. Mit 1/8 erfordert das
10.666,7 STEP/s; mit 1/256 wären es 341.333 STEP/s. Deshalb trennt die
Firmware das feste feine Koordinatenraster von den tatsächlich ausgegebenen
Pulszahlen. Die Automatik vergröbert beim Beschleunigen und verfeinert beim
Abbremsen, ohne die gespeicherten Schritte/mm bei jeder Stufe zu ändern.

Die Fabrikvorgaben bleiben **600/600/300 mm/min** bei **10 mm/s²**.
Gültige niedrigere alte Grenzen wie 100/100/150 werden beim Update erhalten.
Die Begrenzungen `$110`/`$111`/`$112` gelten auch bei einem höheren F-Wert.
Ein auf 100 begrenztes Y fährt deshalb trotz gSender-F1000 höchstens 100.

## Bestehende Einstellungen und Migration

Die CNC-Profilversion ist **76**. Version 75 mit nominal 320/320/1280 wird
mit X/Y ×64 und Z ×16 umgerechnet. Version 74 mit 2560/2560/1280 wird mit
X/Y ×8 und Z ×16 umgerechnet. Feinabgleich, Arbeitskoordinaten, Startzeilen
und eigener Buildtext bleiben bei erkannten CNC-Datensätzen erhalten.
Der bekannte überholte X/Y-Wert 152,380 wird gesondert korrigiert.

**Alte Werte 320, 1280 oder 2560 nicht zurückspielen.** Für die vorgegebene
Mechanik zeigt `$$` nun nominal 20480/20480/20480; vorhandene gemessene
Feinabgleiche können leicht abweichen. Schritte/mm nicht ändern, um eine
Geschwindigkeitswarnung zu umgehen. Ablauf und Wiederherstellung:
[EEPROM-Migration](cnc-gsender.md#bestehende-cnc-einstellungen-umstellen).

## Konservative erste Einstellungen

Nach dem Update zunächst mit den bisherigen langsamen Grenzen den realen
Weg messen. Im Stillstand jede Zeile einzeln senden:

```text
$110=100
$111=100
$112=150
$120=10
$121=10
$122=10
$$
```

Nach bestandener langsamer Prüfung können die Fabrik-Starttempi gewählt
werden:

```text
$110=600
$111=600
$112=300
$$
```

Die Werte ändern die gespeicherten Grenzen, erzeugen aber noch keine
Bewegung. Höhere Werte nur nach bestandenen Zwischenstufen wählen. Die
neue Grenze ist höchstens 1000 mm/min; frühere 3375-mm/min-Versuche gehörten
zum festen 1/4-Profil und gelten für diese Firmware nicht.

## Hardwaretest in acht Stufen

Die Bewegungen führt ausschließlich der Bediener an der Maschine aus.
Die Cloud hat keine reale Maschine vermessen. Fräser entfernen, manuelle
Spindel ausschalten und freie Fahrwege herstellen. `M5`, Pause und Reset
schalten die manuelle Spindel nicht aus.

1. **Konfiguration und kleine Bewegung prüfen.** Vor dem Update `$I`, `$$`,
   `$#` und `$N` sichern. Danach neue Kennung, Migration und virtuelle
   Kalibrierung prüfen. Mit `$DRV` freie Schalter und bereite Treiber
   kontrollieren. Nur eine Achse mit 0,1 bzw. 1 mm bei F20 bewegen:

   ```text
   $J=G21G91X1F20
   $J=G21G91X-1F20
   ```

   Jede Zeile einzeln senden und jeweils `Idle` abwarten. Richtung und
   tatsächlichen Weg beobachten. Für Y/Z entsprechend anpassen.

2. **Weg messen.** X/Y zunächst über 10 mm und danach 50 mm messen; Z nur
   über einen ausreichend freien Weg. Gleichen Messbezug und dieselbe
   Anfahrrichtung verwenden, um Umkehrspiel getrennt zu halten. Formel:
   `neuer Wert = alter Wert × Sollweg / Istweg`. Große Faktorenfehler erst
   anhand Mechanik, Firmwarekennung und Migration klären.

3. **Rückkehr prüfen.** Mit Messuhr oder wiederholbarem Messbezug zehnmal
   einen begrenzten Weg hin und zurück fahren. Nach Stillstand die
   tatsächliche Rückkehr messen. Eine MPos-Anzeige auf null beweist keine
   Schrittverlustfreiheit. Auch die spätere Achslast berücksichtigen.

4. **Geschwindigkeit je Achse erhöhen.** Beschleunigung zunächst bei
   10 mm/s² belassen. In Stufen prüfen, etwa 100, 200, 300, 400, 600, 800
   und höchstens 1000 mm/min. Pro Stufe Geräusch, Verhalten, Rückkehr und
   Schalter prüfen. Achsmaximum und F müssen zur Stufe passen. Bei einer
   Störung zur letzten bestandenen Stufe zurückgehen und Reserve einplanen.
   Bei 1000 mm/min und 10 mm/s² werden je 13,89 mm für Beschleunigen und
   Bremsen benötigt; kurze Wege erreichen das Tempo nicht.

5. **Beschleunigung getrennt optimieren.** Erst mit zuverlässig bestandenem
   Tempo Beschleunigung in kleinen Stufen ändern. Kurze Wechselbewegungen
   und längere Fahrten messen. `Bremsweg = v² / (2 × a)` mit v in mm/s
   verwenden. Endschalter ersetzen keine eingeplante Bremsstrecke.

6. **Umschalten und gemeinsame Bahnen prüfen.** Langsame und schnellere
   Fahrten kombinieren, anschließend XY/XYZ und Kreise prüfen. Auch kurze
   CAM-Segmente, Pause und Fortsetzen einer normalen Fahrt testen.
   MODE-Transfer und Restwegabschluss benötigen Zeit; die erreichte
   Geschwindigkeit kann unter dem angeforderten Wert liegen.

7. **Referenzfahrt und Grenzen prüfen.** `$HX`, `$HY`, `$HZ` einzeln und
   danach `$H` prüfen: Z-Max, anschließend X/Y-Min. Beim gemeinsamen Suchen
   wartet die erste Achse am Schalter auf die zweite. Danach folgen
   Rückzug, Feinanfahrt und letzter Rückzug. Soft Limits erst nach geprüften
   Fahrwegen und Referenzbezug einschalten. Für ein versehentlich aktives
   einzelnes Limit gilt Reset → `$LR` → neu referenzieren; es gibt keinen
   automatischen Wiederanlauf des Jobs.

8. **Erste Fräsversuche.** Werkzeug und Werkstücknull einrichten, zuerst
   Leerfahrt mit Abstand zum Werkstück prüfen. Schnittdaten anhand Material,
   Werkzeug, Schneidenzahl, Eingriff und Maschinensteifigkeit wählen.
   Eine bestandene Leerfahrt mit 1000 mm/min ist keine Freigabe für denselben
   Vorschub in Aluminium. Mit kleinem Eingriff beginnen.

Pro Achse Firmwarekennung, `$$`, gemessenen Weg, Rückkehrabweichung,
zuverlässiges Tempo, Beschleunigung und Reserve dokumentieren.

## Strom, Auflösung und Genauigkeit

TORQUE bleibt X/Y=110 und Z=105 bei Verstärkung 20 und angenommenen
0,033-Ω-Shunts. Daraus folgen nominale Phasen-Spitzenströme von ca.
1,790 A für X/Y und 1,709 A für Z. Die Motorangabe 2,5 A ist kein Anlass,
diese Werte automatisch anzuheben. Strom und Temperatur bleiben zu messen.
[DRV8711-Details](drv8711.md).

Die feinste Befehlsauflösung beträgt nominal ca. 0,049 µm. Der größte
Hardware-Schritt bei 1/8 beträgt 1,5625 µm. Das sind Rechengrößen; Motor-
Nichtlinearität, Last, Elastizität, Umkehrspiel und Maschine begrenzen die
reale Genauigkeit. STALLn und die GRBL-Positionsanzeige sind keine
Positionsgeber. Die manuelle Spindeldrehzahl ist unabhängig vom Achsvorschub.

## Historischer Stand

Der Vorgänger `20261022-rf2000-cnc-speed` verwendete fest X/Y 1/4, Z 1/16
mit 320/320/1280 Schritten/mm und 18 kHz. Daraus ergaben sich rechnerisch
3375/3375/843,75 mm/min. Die noch ältere CNC-Version verwendete X/Y 1/32
und eine 5-kHz-Grenze. Diese alten Werte erklären frühere Anleitungen,
sind aber keine Einstellwerte für das adaptive Profil.

Aktuelle Build- und Simulationsnachweise: [Prüfbericht](validation.md).
