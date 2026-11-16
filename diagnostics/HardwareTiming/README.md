# HardwareTiming - Laufzeiten am echten Aufbau messen

Eigenständiger Diagnose-Sketch für das bisher als **Arduino Yún / ATmega32U4**
erkannte Board. Er ersetzt beim Flashen das Steuerprogramm. Er enthält keine
Schubregelung, keinen USB-Anzeigetest und keine simulierten Sensorzeiten.

## Vorbereitung und Flashen

1. Motor-Leistungskreis abtrennen. Der Diagnose-Sketch legt an Pin 9 ausschließlich
   den bisherigen Minimalbefehl `Servo.write(50)` an; niemals einen Hochlaufwert.
2. OLED und Sensoren wie im vorhandenen Aufbau anschließen: OLED 0x3C und BMP280
   0x76 an I2C (SDA 2, SCL 3), OneWire-Temperatursensor an 8, Poti an A0/18,
   Autothrust-Schalter an 10. Die Sensorversorgung unverändert lassen.
3. `HardwareTiming.ino` im Ordner `HardwareTiming` mit der Arduino IDE öffnen.
   Die drei mitgelieferten `OledGauge...`-Dateien müssen im selben Ordner bleiben.
4. Wie zuletzt das Board **Arduino Yún** und den tatsächlichen COM-Port wählen
   (zuletzt COM4). Bibliotheken: Adafruit BMP280 Library, Adafruit Unified Sensor,
   Adafruit BusIO, DallasTemperature, OneWire, Servo und SSD1306Ascii.
5. Kompilieren und hochladen. Die Firmware ist ausschließlich für ATmega32U4
   vorgesehen, nicht für eine ESP8266-NodeMCU.
6. Seriellen Monitor auf **115200 Baud** öffnen. Die Messung startet automatisch,
   sobald der Monitor den USB-Port öffnet. Sie braucht typischerweise etwa eine
   Minute. Bis zur Zeile `# DONE.` warten.
7. Für eine Wiederholung `r` und Enter senden oder den Port schließen/neu öffnen.

Alternativ kann die Ausgabe mit `Messung-speichern.ps1` im Paket gespeichert
werden. Dabei den seriellen Monitor geschlossen lassen:

```powershell
./Messung-speichern.ps1 -Port COM4
```

Das Skript schreibt eine datierte Textdatei neben sich. Falls Windows Skripte
blockiert, kann die Ausgabe stattdessen aus dem seriellen Monitor kopiert werden.
Die vollständige Datei zur Auswertung zurückgeben.

Nach den Messungen das gewünschte Steuerprogramm aus `NewUIDesing` erneut flashen.
Dieser Sketch führt keinen automatischen Wechsel zurück zur Steuerung durch.

## Messungen

Es werden acht Wiederholungen je Kennzahl erfasst. Einheit ist **Mikrosekunden**
(1.000 µs = 1 ms). Initialisierungen werden einmal gemessen.

| Kennzahl | Gemessener Abschnitt |
| --- | --- |
| `bmp_begin` | BMP280-Initialisierung einschließlich Bibliothekswartezeit |
| `onewire_begin` | Erkennung/Initialisierung der OneWire-Geräte |
| `timer_overhead` | Leerer Messaufruf als Größenordnung der Zeitmesskosten |
| `potentiometer_read` | Ein `analogRead(A0)` |
| `autothrust_switch_read` | Ein `digitalRead(10)` |
| `servo_write_minimum` | CPU-Laufzeit von `Servo.write(50)`, nicht die Pulslänge |
| `temperature_request_blocking` | Originaler blockierender Wandlungsaufruf |
| `temperature_read_by_index` | Auslesen über Index 0 einschließlich Bibliotheksarbeit |
| `temperature_request_plus_read` | Wandlung und anschließendes Auslesen zusammen |
| `bmp_temperature_register_read` | BMP280-interner Temperaturwert, nicht Motortemperatur |
| `bmp_pressure_register_read` | Druckregister lesen und Druck kompensieren |
| `bmp_altitude_read_and_calculation` | Druck auslesen, kompensieren und Höhe berechnen |
| `bmp_forced_conversion` | Neue BMP280-Wandlung bei unverändertem Oversampling |
| `legacy_oled_clear` | Löschen mit der bisherigen SSD1306Ascii-Bibliothek |
| `legacy_oled_four_text_lines` | Vier repräsentative Textzeilen mit fixed_bold10x15 |
| `gauge_display_begin` | Initialisierung und erste vollständige Balkenanzeige |
| `gauge_unchanged` | Unveränderte Balken, keine Übertragung nötig |
| `gauge_full_update` | Geänderte Balken, alle acht Displayseiten |
| `gauge_header_blink` | Nur Autothrust-Blinkphase, eine Displayseite |
| `gauge_warning_blink` | Akku-/Temperaturwarnung, acht Displayseiten |
| `cycle_static_display` | Beispielzyklus mit unveränderter Anzeige |
| `cycle_changing_display` | Beispielzyklus mit vollständigem Displayupdate |

Die OneWire-Gerätezahl, maximale konfigurierte Auflösung und parasitäre
Versorgung werden mit ausgegeben. Die BMP280-Einstellungen werden durch
`bmp.begin()` wie im Steuerprogramm gesetzt. `ctrl_meas` wird vor dem
Wandlungstest ausgegeben. Für diesen Test wird kurz in Sleep/Forced gewechselt;
Oversampling und Filter bleiben unverändert, danach wird der ursprüngliche Modus
wiederhergestellt. Ein Fehler beim Wiederherstellen wird gemeldet.

Bei den Beispielzyklen wird in Durchlauf 1 und 5 tatsächlich die Temperatur
gewandelt und gelesen. Die übrigen Durchläufe warten die bisherigen 638 ms.
Zusätzlich laufen Höhenmessung, Poti-/Schalterabfrage, Minimal-PWM-Befehl und
Displayupdate. Es ist **kein exakter Nachbau der gesamten Regelschleife**:
Vario-Glättung, Autothrust-/Temperaturberechnung und bisherige Debugausgaben
sind darin nicht enthalten. Die Einzelwerte machen den Unterschied zwischen
Temperatur- und Pausendurchläufen sichtbar.

## Ausgabe lesen

Kommentarzeilen beginnen mit `#`. Die Daten sind durch Semikolon getrennt:

```text
raw;Kennzahl;Nummer;Dauer_us;gueltig
summary;Kennzahl;Anzahl;Minimum_us;Mittelwert_us;Maximum_us;Fehler
```

Die `summary`-Zeilen lassen sich in eine Tabellenkalkulation übernehmen. Zeiten
werden auch für fehlgeschlagene Auslesevorgänge protokolliert; `gueltig=0` bzw.
`Fehler>0` unbedingt beachten. Ein reiner Wandlungsauftrag bestätigt noch keinen
gültigen Temperaturwert; dafür die getrennten Auslese-/Kombinationsmessungen
prüfen. Fehlende Geräte führen zu `SKIP`, nicht zu erfundenen Ersatzzeiten.
Ein OLED-ACK bestätigt nur die I2C-Adresse, nicht die optische Darstellung.

## Aussagekraft und Grenzen

- I2C bleibt bei **100 kHz**. Die kopierten Displaydateien sind unverändert aus
  `NewUIDesing`, Commit **3dee895**. Das ist der Vergleichsstand dieser Messung.
- Serienausgabe und Testvorbereitung liegen außerhalb der gemessenen Aufrufe.
  Interrupts, Servo-Timer und die USB-Verbindung bleiben aktiv. USB-Interrupts
  können daher in den gemessenen CPU-Zeiten enthalten sein.
- `readPressure()` und `readAltitude()` lesen im Normalmodus vorhandene Daten.
  Ihre Dauer ist **nicht die Wandlungszeit** des Drucksensors. Dafür gibt es
  den gesonderten Forced-Test: Er misst Befehl plus Warten bis zum Ende einer
  frischen Wandlung, ohne zusätzliches Druckauslesen. Die Abfrageauflösung ist
  ungefähr 1 ms zuzüglich I2C-Abfrage; es ist keine sub-ms-Präzisionsmessung.
- `micros()` hat beim 16-MHz-AVR typischerweise 4-µs-Schritte. Sehr kurze
  Einzelaufrufe sind entsprechend grob quantisiert; nicht überinterpretieren.
- Ein I2C-Timeout von 25 ms begrenzt Hänger durch einen defekten Bus im
  Diagnoseprogramm. Erfolgreiche Messungen verwenden denselben Bustakt;
  Timeout-/Fehlermessungen sind keine normalen Betriebszeiten.
- Wärmeübertragung Motor → Sensor, Druckausgleich im Gehäuse, Filterantwort,
  tatsächliche PWM-Pulslänge und ESC-/Motorreaktion werden damit nicht gemessen.
  Dafür sind definierte physikalische Änderungen bzw. Oszilloskopmessungen nötig.
- Acht Stichproben liefern beobachtete Min-/Mittel-/Maximalwerte, keine
  garantierten Worst-Case-Zeiten und keine Freigabe für den Flugbetrieb.

## Verifizierter Build

Arduino AVR Boards 1.8.6, Ziel `arduino:avr:yun`, 16 MHz:
27.790 / 28.672 Byte Flash (96%), 753 / 2.560 Byte statisches RAM (29%).
Erfolgreich kompiliert; Laufzeiten müssen mit den tatsächlich angeschlossenen
Sensoren und dem Display erfasst werden.

```sh
arduino-cli compile --fqbn arduino:avr:yun HardwareTiming
```

Im Downloadpaket liegt zusätzlich die dafür kompilierte HEX-Datei unter
`firmware`. Sie ist nur für den genannten Board-/Taktstand vorgesehen.
