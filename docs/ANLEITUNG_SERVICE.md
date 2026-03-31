# Anleitung Service

Dieses Projekt misst `Forward` und `Reverse` direkt am ESP32 und stellt die Werte plus `SWR` im eigenen WLAN-Access-Point dar.

Es gibt zwei Hauptaufgaben:

- **Leistungsmessung**
  Der Anwender stimmt die Endstufe auf **maximale Leistung** ab.
  Dabei ist nur wichtig: **hoeherer Ton = mehr Leistung**.

- **SWR-Messung**
  Diese ist vorbereitet und kann spaeter kalibriert werden.
  Im Moment soll sie noch **nicht fuer eine echte Stationsbewertung** benutzt werden.

## Hardware

- esp32 dev. board
- `GPIO25` -> passiver Piezo
- `GPIO27` -> Taster nach `GND`
- `GPIO34` -> Forward-Messspannung
- `GPIO35` -> Reverse-Messspannung

## Spannungsteiler je Messkanal

Je Kanal:

- `100 kOhm` vom Sensor-Ausgang zum ESP32-Pin
- `33 kOhm` vom ESP32-Pin nach `GND`
- `3.3 V Zenerdiode` parallel zum `33 kOhm`
oder
- je ein `Trimmpoti` fuer `Uf` und `Ur`

Wichtig:

- an `GPIO34` und `GPIO35` nie mehr als `3.3 V`
- `GND` von Bridge und ESP32 verbinden

## Einstellung der maximalen Eingangsspannung

Fuer `Uf` und `Ur` gibt es je einen eigenen Trimmpoti.

Ziel:

- bei der **maximal zu messenden Leistung** sollen am ESP32-Eingang
  hoechstens **3.3 V** anliegen

Das gilt fuer beide Kanaele:

- `Uf` an `GPIO34`
- `Ur` an `GPIO35`

### Vorgehen

1. Messaufbau anschliessen.
2. Die Spannungen `Uf` und `Ur` im Access Point auf der Webseite ablesen (siehe unten).
3. Trimmpoti fuer `Uf` so einstellen, dass bei maximal gewuenschter
   Vorlauf-Leistung am Eingang **nicht mehr als 3.3 V** anliegen.
4. Trimmpoti fuer `Ur` genauso einstellen, dass auch am Reverse-Eingang
   **nicht mehr als 3.3 V** anliegen, dies ist aber unkritisch.

Die Werte werden also direkt im AP kontrolliert.

### Wichtige Regel

- lieber etwas Reserve lassen
- niemals absichtlich ueber `3.3 V` gehen

Die `3.3 V` Zenerdiode ist als Schutz vorhanden. Der ESP32 ist dadurch gegen Ueberspannung abgesichert. Trotzdem soll sauber eingestellt werden, damit die Messung korrekt bleibt.

## WLAN

- SSID: `SWR-Buzzer-LX1WJ`
- kein Passwort
- Webseite: `http://192.168.4.1`

## Webinterface

Angezeigt werden:

- Forward- und Reverse-Spannung am ESP32
- ADC-Rohwerte
- Referenzspannung
- aktuelle Tonfrequenz

Einstellbar sind:

- `Startfrequenz`
- `Spannweite in Prozent`
- `Buzzer Lautstaerke 1-10`

- `SWR Messung ein oder aus`
- `SWR Tonfrequenz`
- `SWR Piepdauer`
- `SWR Pause`
- `SWR Gruppenpause`
- `Zeit fuer langen Tastendruck`
- `Kalibriertabelle mit 30 Zeilen`

## Einstellungen fuer die Leistungsmessung

Auf der Webseite gibt es Einstellungen fuer die Leistungsmessung.

### Startfrequenz

Das ist der Ton, den du direkt nach dem kurzen Tastendruck hoerst.

Beispiel:

- `200 Hz` = tiefer Startton
- `1000 Hz` = hoeherer Startton

Wenn du einen hoeheren Grundton besser hoerst, stellst du hier einen groesseren Wert ein.

### Spannweite in Prozent

Damit stellst du ein, wie stark sich der Ton bei Leistungsveraenderung aendert.

- **kleiner Wert** = Ton aendert sich langsamer und ruhiger
- **groesserer Wert** = Ton aendert sich schneller und deutlicher

Beispiel:

- `5 %` = eher kleine Tonveraenderung
- `20 %` = deutlich staerkere Tonveraenderung

Wenn dir die Tonveraenderung zu klein vorkommt, nimm einen groesseren Wert.
Wenn der Ton zu nervoes wirkt, nimm einen kleineren Wert.

### Buzzer Lautstaerke

Hier stellst du nur ein, wie laut der Piepser ist.

- `1` = leise
- `10` = laut

Das aendert **nicht** die Leistungsmessung, sondern nur die Lautstaerke.

## Taster

- kurzer Druck bei `Aus`: Leistungston ein und Forward-Referenz setzen
- kurzer Druck bei aktivem Ton: aus
- langer Druck bei `Aus`: SWR-Ton ein, aber nur wenn SWR im AP eingeschaltet wurde
- langer Druck bei aktivem Ton: aus

## Messlogik

- im Modus `Leistung` wird nur `Forward` frisch gemessen
- im Modus `SWR` werden `Forward` und `Reverse` frisch gemessen
- dadurch bleibt die Leistungsanzeige schnell, auch wenn SWR im AP eingeschaltet ist

## SWR-Messung

Die SWR-Messung ist schon vorbereitet, aber noch **nicht auf eine bestimmte Funkstation kalibriert**.

Darum gilt im Moment:

- **SWR im WLAN-Menue ausgeschaltet lassen**
- **SWR am Geraet jetzt noch nicht benutzen**

Spaeter geht die SWR-Messung so:

1. **Lang auf den Taster druecken.**
2. Dann hoerst du eine Folge von Piepsern.
3. Die Zahl der Piepser zeigt den SWR-Bereich.

Das bedeutet:

- **1 Piepser** = SWR zwischen 1 und 1.99
- **2 Piepser** = SWR zwischen 2 und 2.99
- **3 Piepser** = SWR zwischen 3 und 3.99
- **4 Piepser** = SWR zwischen 4 und 4.99

Je weniger Piepser, desto besser.

## SWR-Kalibrierung

Im AP gibt es eine Tabelle mit `30` Zeilen:

- Spalte 1: SWR manuell eingeben
- Spalte 2: `Uf`
- Spalte 3: `Ur`
- pro Zeile ein `Capture`-Button

Mit `Capture` werden die aktuellen Messwerte in die ausgewaehlte Zeile uebernommen.
Danach wird das SWR aus der Tabelle interpoliert.

## Kurzform

- **kurz druecken**
- **abstimmen**
- **auf den hoechsten Ton gehen**
- **bei Bedarf mit dem Handy ins WLAN SWR-Buzzer-LX1WJ und 192.168.4.1 oeffnen**

SWR bitte im Moment noch ausgeschaltet lassen.
