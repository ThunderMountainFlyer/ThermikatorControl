# HardwareTiming v2 - asynchrone Temperaturmessung und Vergleich

Eigenständiger Diagnose-Sketch für das bisher als **Arduino Yún / ATmega32U4**
erkannte Board. Er ersetzt beim Flashen das Steuerprogramm. Er enthält keine
Schubregelung, keinen USB-Anzeigetest und keine simulierten Sensorzeiten.
Die mitgelieferte Displayimplementierung ist ein fester Vergleichsstand mit
synchronen Übertragungen und Varioskala -2 bis +2 m/s. Sie misst nicht den
aktuellen inkrementellen OLED-Treiber oder dessen Startanzeige. Für die
250-ms-Regelung steht der `VARIO_TIMING_BENCH`-Build des Hauptprogramms bereit;
siehe [Projekt-README](../../README.md#hardware-timing-verification).

## Vorbereitung und Flashen

1. Motor-Leistungskreis abtrennen. Der Diagnose-Sketch legt an Pin 9 ausschließlich
   den bisherigen Minimalbefehl `Servo.write(50)` an; niemals einen Hochlaufwert.
2. OLED und Sensoren wie im vorhandenen Aufbau anschließen: OLED 0x3C und BMP280
   0x76 an I2C (SDA 2, SCL 3), OneWire-Temperatursensor an 8, Poti an A0/18,
   Autothrust-Schalter an 10. Die Sensorversorgung unverändert lassen.
3. `HardwareTiming.ino` im Ordner `HardwareTiming` mit der Arduino IDE öffnen.
   Die mitgelieferten `OledGauge...`-Dateien und `MotorTemperature.h` müssen
   im selben Ordner bleiben.
4. Wie zuletzt das Board **Arduino Yún** und den tatsächlichen COM-Port wählen
   (COM4 ist im Beispiel unten ein Platzhalter). Bibliotheken: Adafruit BMP280
   Library, Adafruit Unified Sensor, Adafruit BusIO, DallasTemperature, OneWire,
   Servo und SSD1306Ascii.
5. Kompilieren und hochladen. Die Firmware ist ausschließlich für ATmega32U4
   vorgesehen, nicht für eine ESP8266-NodeMCU.
6. Seriellen Monitor auf **115200 Baud** öffnen. Nach `# READY.` den Autothrust-Schalter **AUS und dann EIN** schalten.
   Erst diese Betätigung startet die Messung. Ein vollständiger Lauf dauert
   typischerweise ein bis zwei Minuten. Bis zur Zeile `# DONE.` warten.
7. Für eine Wiederholung den Schalter wieder AUS und dann EIN schalten.
   Ein bereits eingeschalteter Schalter löst beim Öffnen von USB keinen Lauf aus.
   Der Eingang an Pin 10 wird 50 ms entprellt. Einschalten bedeutet wie im
   Steuerprogramm HIGH; die vorhandene Schalterbeschaltung bleibt unverändert.
   Der serielle Monitor bzw. das Aufnahmeskript muss zum Start verbunden sein.
   Während eines Laufs werden Schalterbetätigungen nicht als weitere Starts
   gespeichert; zum Wiederholen nach dem Ende neu schalten.

Alternativ kann die Ausgabe mit [Messung-speichern.ps1](../Messung-speichern.ps1)
gespeichert werden. Den seriellen Monitor geschlossen lassen und vom
Repository-Hauptverzeichnis aus starten (den tatsächlichen Port einsetzen):

```powershell
./diagnostics/Messung-speichern.ps1 -Port COM4
```

Das Skript schreibt eine datierte Textdatei neben sich und wartet bis zu zehn
Minuten auf den Schalterstart. Danach hat der Messlauf 180 Sekunden Zeit.
Für eine weitere Aufnahme das Skript erneut starten und den Schalter neu
AUS/EIN betätigen. Falls Windows Skripte
blockiert, kann die Ausgabe stattdessen aus dem seriellen Monitor kopiert werden.

Nach den Messungen das Steuerprogramm aus dem Repository-Hauptverzeichnis
auf `main` erneut flashen.
Dieser Sketch führt keinen automatischen Wechsel zurück zur Steuerung durch.

## Messungen

Die Einzelaufruf- und Displaytests erfassen acht Wiederholungen je Kennzahl;
die asynchronen Phasen erfassen Aufrufe fortlaufend. Einheit ist
**Mikrosekunden** (1.000 µs = 1 ms). Initialisierungen werden einmal gemessen.

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

Die großen alten Textanzeige-Tests (`legacy_oled_clear`,
`legacy_oled_four_text_lines`) und die beiden alten `cycle_...`-Messungen sind
im neuen Standardlauf nicht aktiv. Sie passen nicht gemeinsam mit allen
asynchronen Messungen in den AVR-Flash. Die ursprünglichen Messprotokolle
bleiben die Vergleichsbasis. Mit `LEGACY_BASELINE=1` im Sketch lassen sich
stattdessen die alten Tests ohne die neuen Async-Phasen kompilieren.

Die OneWire-Gerätezahl, maximale konfigurierte Auflösung und parasitäre
Versorgung werden mit ausgegeben. Dieser Diagnose-Sketch verwendet für
seine Register-Lesetests die Einstellungen nach `bmp.begin()` im Normalmodus;
das Hauptprogramm stellt dagegen explizit auf getaktete Forced-Wandlungen um.
`ctrl_meas` wird vor dem Wandlungstest ausgegeben. Für diesen Test wird kurz in Sleep/Forced gewechselt;
Oversampling und Filter bleiben unverändert, danach wird der ursprüngliche Modus
wiederhergestellt. Ein Fehler beim Wiederherstellen wird gemeldet.

Nur im optionalen Legacy-Baseline-Modus wird in Durchlauf 1 und 5 tatsächlich die Temperatur
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

- I2C bleibt bei **100 kHz**. Die kopierten Displaydateien entsprechen Commit
  **3dee895**. Das ist der feste Vergleichsstand dieser Messung; sie werden
  unabhängig vom OLED-Treiber des Hauptprogramms verwendet.
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

## Build

Ziel: Arduino AVR Boards 1.8.6, `arduino:avr:yun`, 16 MHz.
Vom Repository-Hauptverzeichnis aus kompilieren. Flash- und RAM-Belegung zeigt
der Compiler; besonders auf den knappen Flash-Speicher des ATmega32U4 achten.
Laufzeiten werden mit den tatsächlich angeschlossenen Geräten erfasst.

```sh
arduino-cli compile --fqbn arduino:avr:yun diagnostics/HardwareTiming
```

## Asynchrone Messphasen

Die lokale Datei `MotorTemperature.h` ist bytegleich mit dem Temperaturcode
aus dem Steuerprogramm, Commit 6ed1bd7. Ein abgeleitetes DallasTemperature-
Busobjekt misst die realen Methodenaufrufe; es werden keine Sensordaten
simuliert. Interrupts und Minimal-PWM bleiben aktiv. Während einer Phase wird
keine Serienausgabe ausgeführt; die gesammelten Zusammenfassungen folgen danach.
Der zusätzliche Instrumentierungsaufwand gehört zu den gemessenen Gesamtzeiten.

Die Kommentarzeile `# ASYNC phase ...` ordnet die nachfolgenden Summary-Zeilen
zu. **Die gleichen Kennzahlnamen kommen in jeder Phase erneut vor**; beim
Auswerten die Phasennummer berücksichtigen, nicht die Werte überschreiben.

- **Phase 0:** Fertigabfrage mit 10-ms-Pause, ohne Druck-/Displayarbeit.
- **Phase 1:** Beispielzyklus mit Druck-/Poti-/Schalterabfrage, statischer
  Anzeige und 638-ms-Pause.
- **Phase 2:** Derselbe Beispielzyklus, aber jedes Mal vollständiges Balkenupdate.

Pro Phase werden drei gültige neue Temperaturwerte angestrebt. Nach 20 Sekunden
oder bei geschlossenem USB-Port endet die Phase auch ohne drei Werte; der
Abschluss meldet gültige Ergebnisse und Fehlerstatus. Bei fehlendem Sensor wird
der Async-Teil übersprungen. Die Schalter-Entprellung und Vorbereitungen gehören
nicht zur gemessenen Zykluszeit. Die 638-ms-Phasen sind weiterhin Beispielzyklen,
keine Messung der kompletten Autothrust-/Glättungsberechnung.

| Kennzahl | Bedeutung |
| --- | --- |
| `async_request_command` | Nur Wandlungsbefehl, ohne auf das Ergebnis zu warten |
| `async_ready_probe` | Einzelne Fertigabfrage; BUSY ist kein Fehler |
| `async_cached_read` | Tatsächliches Auslesen über gespeicherte ROM-Adresse |
| `async_poll_busy` | Kompletter Reader-Aufruf bei noch laufender Wandlung |
| `async_poll_idle` | Reader-Aufruf zwischen Messungen, ohne fällige Arbeit |
| `async_poll_start` | Reader-Aufruf, der die nächste Wandlung startet |
| `async_poll_result` | Reader-Aufruf, der ein Ergebnis ausliest/validiert |
| `async_request_interval` | Abstand zwischen zwei abgeschlossenen Startbefehlen |
| `async_result_interval` | Abstand zwischen zwei gültigen Ergebnissen |
| `async_request_to_result` | Start bis Übernahme inklusive Abfragetakt und Auslesen |
| `async_cycle_total` | Tatsächliche Laufzeit eines Beispielzyklus einschließlich Pause |

`async_request_to_result` ist nicht die reine physikalische Wandlungszeit. Bei
seltenen Abfragen wartet ein fertiger Sensor bis zum nächsten `poll()`; außerdem
kommt das Auslesen hinzu. Die schnelle Phase macht diesen Unterschied sichtbar.
Fehlende Statistikzeilen bedeuten, dass kein Aufruf dieses Typs stattfand.
Fehlerhafte Auslesevorgänge werden mit Fehlerzähler erfasst; die alte gültige
Temperatur wird vom Reader behalten. Ein mangels Ergebnissen abgebrochener Lauf
ist keine bestandene Funktionsprüfung.

Das Aufnahmeskript und der Ablauf bleiben gleich: USB-Aufnahme öffnen, dann
Autothrust-Schalter AUS und EIN. Weder USB allein noch der Sensor starten die
Messreihe automatisch. Ein Lauf enthält weiterhin die blockierenden Temperatur-
Einzelmessungen als direkte Vergleichswerte; das ist bei dieser Diagnose
beabsichtigt und sagt nichts über den Aufrufmodus im neuen Steuerprogramm aus.
