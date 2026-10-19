# RF2000-CNC: Display, Tasten und Menü

Diese Anleitung gilt für das Profil **`rf2000_cnc`** und das originale
20×4-Zeichen-Display mit elf Tasten. Fräsprogramme werden über **gSender**
gestartet; ein Start von SD-Karte ist nicht eingerichtet.

Die serielle Bedienung steht in der [Befehlsübersicht für gSender](befehle.md),
einschließlich `$HY`, Joggen, Nullpunkten und Einstellungen.

Die Spindel wird weiterhin **von Hand am Spindelgerät eingeschaltet und
ausgeschaltet**. Pause, Reset und `M5` schalten diese Spindel nicht aus.

## Die Tasten an deinem Bedienfeld

Die Anordnung folgt den Gerätefotos. Die Bezeichnung „Weiter“ meint die
bisherige Continue-Taste, „Pause“ die bisherige Pause-Taste der Repetier-Firmware.

```text
         LINKS                         RECHTS

          Hoch                       Z+       Z−
           ▲
  Links ◀  OK  ▶ Rechts               −        +
           ▼
         Runter                    Weiter    Pause
```

| Taste | Auf der Startseite / im Menü | Beim Verfahren | Während eines Fräsprogramms |
| --- | --- | --- | --- |
| Hoch / Runter | Menüpunkt wählen | Y+ / Y− | Keine manuelle Achsfahrt |
| Links / Rechts | Links: zurück; Rechts: Menü öffnen bzw. auswählen | X− / X+ | Keine manuelle Achsfahrt |
| OK | Menü öffnen, auswählen oder bestätigen | Zurück zum Menü | Keine manuelle Achsfahrt |
| Z+ / Z−, rechte obere Reihe | Auf der Startseite: Verfahren öffnen, ohne zu fahren | Z positiv / negativ bewegen | Keine manuelle Achsfahrt |
| − / +, rechte mittlere Reihe | Keine Achsfahrt | Schrittweite kleiner / größer | Vorschubkorrektur −10 / +10 Prozentpunkte, höchstens 100 % |
| Weiter, rechts unten links | Keinen Fräsjob starten | Keine Bewegung starten | Einen vollständig angehaltenen Fräsjob fortsetzen |
| Pause, rechts unten rechts | Bei Stillstand oder Alarm: sofort Reset | Aktuelle Jogfahrt abbrechen; eine Sekunde halten: Reset | Kontrolliert anhalten; eine Sekunde halten: Reset |

**Weiter und Pause gleichzeitig führen sofort einen Software-Reset aus.**
Bei Stillstand, Alarm, Referenzfahrt oder Endschalter-Freifahren löst bereits
**Pause allein sofort einen Reset** aus. Die Achsreferenz ist nach einem
Reset ungültig. Der Software-Reset ersetzt keinen physischen Not-Aus.

Die geänderte Belegung ist beabsichtigt: **Links ist jetzt „Zurück“**, und
**Weiter setzt einen pausierten Fräsjob fort**. Diese Tasten waren im alten
Testmenü ebenfalls Reset-Tasten. Die unteren Tasten folgen der tatsächlich
verwendeten Original-Repetier-Zuordnung, nicht den abweichenden
PAUSE/CONTINUE-Beschriftungen einzelner Schaltplanstellen.

## Menüführung

Auf der Startseite **OK oder Rechts** drücken. Im Menü mit **Hoch/Runter**
wählen, mit **OK oder Rechts** öffnen und mit **Links** zurückgehen.
Achsen bewegen sich nur auf der Seite **Verfahren XYZ** oder nach einer
ausdrücklich bestätigten Referenz-/Freifahraktion.

```text
Startseite
└─ Hauptmenü
   ├─ Freigeben              Startsperre bei freien Eingängen lösen
   ├─ Verfahren XYZ          Achsen schrittweise bewegen
   ├─ Referenzieren
   │  ├─ XYZ                 Z-Max, danach X-Min und Y-Min
   │  ├─ X                   nur X
   │  ├─ Y                   nur Y
   │  └─ Z                   nur Z
   ├─ Arbeitsnull
   │  ├─ XYZ                 alle drei Arbeitskoordinaten nullen
   │  ├─ X                   nur X nullen
   │  ├─ Y                   nur Y nullen
   │  └─ Z                   nur Z nullen
   ├─ Diagnose               Schalter- und Treiberzustände ansehen
   ├─ Endschalter frei       begrenzter Rückzug vom aktiven Schalter
   ├─ Jogtempo               10 / 20 / 30 mm/min wählen
   └─ Abbruch / Reset        Bewegung und Sitzung abbrechen
```

**Referenzieren, Arbeitsnull und Endschalter frei benötigen eine zweite,
getrennte OK-Betätigung.** Erst auswählen, Taste loslassen, die angezeigte
Aktion prüfen und nochmals OK drücken. Mit Links wird die Bestätigung
verlassen. Ein gehaltener oder versehentlich doppelt belegter Tastendruck
soll keine Aktion auslösen.

**Freigeben** entspricht `$X`: Es löst die Startsperre, sofern die Eingänge
frei und die Treiber bereit sind. Es ersetzt keine Referenzfahrt und fährt
einen betätigten Schalter nicht frei.

**Arbeitsnull** setzt die gewählten Achsen im aktuell aktiven
Arbeitskoordinatensystem **G54–G59** auf null. Dabei bewegt sich keine Achse.
Arbeitsnull und Maschinenreferenz sind zwei verschiedene Dinge.

## Was die Startseite zeigt

Die Startseite zeigt **die gerade ausgeführte Aktion**, den Betriebszustand,
die XYZ-Arbeitspositionen in Millimetern, das aktive Arbeitskoordinatensystem
und den tatsächlichen Vorschub. Die Positionen beziehen sich auf den gerade
aktiven Werkstücknullpunkt; sie sind keine Anzeige der noch ausstehenden
Programmfahrt.

Bei einer Referenzfahrt wird die **wirklich aktive Achse oder Achsgruppe**
mit der aktuellen Phase angezeigt. Eine XYZ-Referenzfahrt bleibt deshalb
nicht während des ganzen Ablaufs mit „REF XYZ“ beschriftet. Der Verlauf ist:

```text
REF Z: SUCHEN
REF Z: RUECKZUG
REF Z: FEINANFAHRT
REF Z: FREIFAHREN
REF X+Y: SUCHEN
REF X+Y: RUECKZUG
REF X+Y: FEINANFAHRT
REF X+Y: FREIFAHREN
```

X und Y werden im zweiten Teil gemeinsam angefahren; jeder Schalter stoppt
seine eigene Achse. Bei einer Einzelreferenzfahrt steht entsprechend nur
`REF X`, `REF Y` oder `REF Z`. Die Referenzanzeige `XYZ`, `X--` usw. wird erst
nach erfolgreichem Abschluss aktualisiert; `---` bedeutet keinen gültigen
Maschinenbezug nach Einschalten, Alarm oder Reset.

So sehen die 20×4-Anzeigen mit beispielhaften Positionen aus. Nach einer
erfolgreichen XYZ-Referenzfahrt und bei aktivem G54:

```text
CNC BEREIT   G54 XYZ
X:   12.34Y:   56.78
Z:    5.00 F:     0
OK:Menue SP:MANUELL
```

```text
REF Y: FEINANFAHRT
Schalter: Y-MIN
F:20 mm/min
II:Abbruch SP:MAN.
```

`SP:MANUELL` bzw. `SP:MAN.` erinnert an die manuelle Spindelbedienung.
`II` steht für Pause, `>` für Weiter und `<` für Links/Zurück. Beim Fräsen
steht oben beispielsweise `CNC FAHRT XY`: Die Buchstaben zeigen die im
aktuellen Bewegungsabschnitt beteiligten Achsen. Beim Anhalten steht dort
zunächst `BREMSEN` und nach dem Stillstand `HALT`. Weiter
wirkt erst nach vollständigem Stillstand. Ein Alarm zeigt die Fehlernummer; Diagnose und
Endschalter-Freifahren helfen anschließend bei der Einordnung.

Wird Pause noch während der kurzen Treiber-Aktivierung vor dem ersten
Schritt gedrückt, wird die Fahrt abgebrochen. Nach diesem Reset erneut
referenzieren bzw. für die manuelle Einrichtung freigeben.

## Achsen am Gerät einrichten

1. Bei freien Schaltern **Freigeben** auswählen oder zuerst referenzieren.
2. **Verfahren XYZ** öffnen; alternativ auf der Startseite einmal Z+ oder
   Z− drücken. Dieser erste Tastendruck öffnet nur die Seite. Danach fährt
   die linke/rechte Kreuztaste X−/X+,
   unten/oben fährt Y−/Y+, die rechte obere Tastenreihe fährt Z+/Z−.
3. Mit **−/+** die Schrittweite **0,01 / 0,10 / 0,50 mm** wählen. Das allein
   bewegt noch keine Achse. Jeder getrennte Richtungstastendruck fährt genau
   einen Schritt der gewählten Länge; Halten löst keine Folgefahrten aus.
4. Mit OK zurückgehen. Das lokale Tempo lässt sich unter **Jogtempo** auf
   **10, 20 oder 30 mm/min** einstellen.
5. Am gewünschten Werkstücknullpunkt **Arbeitsnull → XYZ** oder eine einzelne
   Achse wählen und mit einer zweiten OK-Betätigung bestätigen.

Schrittweite und lokales Jogtempo gelten für die aktuelle Sitzung. Nach
Reset sind wieder **0,10 mm** und **20 mm/min** eingestellt.

Die CNC-Grundeinstellungen sind **2560 Schritte/mm für X/Y**, **1280 für Z**
und maximale Vorschübe **100 / 100 / 150 mm/min**. Das passt zu 2:1-Übersetzung,
5-mm-Spindelsteigung und 1/32-Mikroschritten für X/Y bzw. 1/16 für Z.
Gespeicherte GRBL-Einstellungen bleiben beim Firmwareupdate erhalten;
gegebenenfalls zuerst die [Kalibrierung in gSender umstellen](cnc-gsender.md#bestehende-cnc-einstellungen-umstellen).
Y ist am Gerät bestätigt; X/Z-Fahrweg und Schalter sind noch am Gerät zu prüfen.

## Referenzieren am Display oder über gSender

Am Display **Referenzieren → Y → OK zur Bestätigung** wählen, um nur Y zu
referenzieren. Für alle Achsen entsprechend **XYZ** wählen. Über die
gSender-Konsole ist derselbe Ablauf direkt erreichbar:

| Befehl | Referenzfahrt |
| --- | --- |
| `$H` | Z-Max, danach X-Min und Y-Min |
| `$HX` | Nur X-Min |
| `$HY` | Nur Y-Min |
| `$HZ` | Nur Z-Max |

Jede Referenzfahrt umfasst Suche, Rückzug, langsames erneutes Anfahren und
abschließendes Freifahren. Ein schon vor dem Start betätigter Endschalter
verhindert die normale Referenzfahrt; dafür ist zunächst der begrenzte
Freifahrablauf vorgesehen.

## Wenn eine Achse im normalen Betrieb einen Endschalter trifft

Der Hard Limit hält die Achsen sofort an. Das Programm wird **nicht
automatisch zurückgefahren oder fortgesetzt**: Durch den abrupten Halt kann
die Positionszuordnung verloren gegangen sein.

1. Den Fräsjob in gSender beenden und die manuelle Spindel ausschalten.
2. Zunächst Reset ausführen, um den kritischen Alarmzustand zu verlassen.
   Am Gerät geht das mit **Weiter + Pause gleichzeitig**.
3. Unter **Diagnose** prüfen, welcher Schalter aktiv ist. Es darf nur ein
   physischer Endschalter aktiv sein und kein Treiberfehler vorliegen.
4. **Endschalter frei** auswählen und mit einer zweiten OK-Betätigung
   bestätigen. Alternativ in gSender **`$LR`** senden.
5. Die betroffene Achse fährt höchstens **2 mm mit bis zu 20 mm/min** vom Schalter
   weg. Andere Schalter und die Treiberüberwachung bleiben aktiv. Nachdem
   der gewählte Schalter fünf aufeinanderfolgende Abfragen frei war, löst
   auch seine erneute Betätigung sofort wieder einen Alarm aus.
6. Danach bleibt die Steuerung im Alarm. Erneut referenzieren und den
   Werkstücknullpunkt prüfen. Den unterbrochenen Job neu einrichten.

Bleibt der Schalter innerhalb des Rückzugswegs betätigt, endet der Ablauf mit
`ALARM:8`. **Pause**, Reset, ein anderer betätigter Endschalter oder ein
Treiberfehler brechen den Versuch ab. Details stehen unter
[Endschalter freifahren](cnc-gsender.md#nach-einem-hard-limit-freifahren).

## Quellen und Zuordnung

Die Menüführung trennt Navigation, schrittweises Einrichten, Referenzfahrt
und laufenden Fräsjob entsprechend den GRBL-Betriebsarten. Für Pause,
Fortsetzen und Jog-Abbruch wurden die offiziellen GRBL-Unterlagen verwendet:

- [GRBL v1.1 Commands: `$H`, `$X`, Pause, Fortsetzen und Reset](https://github.com/gnea/grbl/wiki/Grbl-v1.1-Commands)
- [GRBL v1.1 Jogging: `$J` und Jog-Abbruch](https://github.com/gnea/grbl/wiki/Grbl-v1.1-Jogging)
- [GRBL v1.1 Interface: Zustände und Statusmeldungen](https://github.com/gnea/grbl/wiki/Grbl-v1.1-Interface)

Die konkreten Tasten folgen den Nutzerfotos und der Originalfirmware:
[ui.cpp](../references/repetier/Repetier/ui.cpp),
`UIDisplay::ui_init_keys()` / `ui_check_keys()`, sowie
[pins.h](../references/repetier/Repetier/pins.h). Rechts oben liegen PJ2/PJ4,
rechts mittig PJ5/PJ6 und unten PH7/PH2. Die neue CNC-Belegung ist im Projekt
implementiert; sie ist keine allgemeine GRBL-Standardbelegung.
