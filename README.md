# ThermikatorControl

Thrust controller for the Thermikator electric paragliding power unit, with
barometric Autothrust limiting, motor-temperature limiting and an OLED UI.
The Thermikator was designed and first built by Markus Dörr:
http://www.paragliding-kochertal.de/aktuell/emot.html

Current branch: `NewUIDesing`. Current timing design: **800 ms fixed vario frames**.
The Waveshare conversion remains on the separate `feat/waveshare-154-epaper` branch.

## Hardware and build

Existing SparkFun Pro Micro / ATmega32U4 wiring:

| Function | Pin / address |
| --- | --- |
| 128 x 64 SSD1306 OLED | SDA 2, SCL 3, I2C 0x3C |
| BMP280 | same I2C bus, address 0x76 |
| OneWire motor temperature | 8 |
| Motor-regulator PWM | 9 |
| Autothrust switch | 10 |
| Throttle potentiometer | A0 / 18 |

No wiring changes are required. I2C remains at 100 kHz. The controller last
identified by USB as Arduino Yún uses the same ATmega32U4. Select the actual
board/bootloader and correct 8 MHz / 16 MHz hardware variant when uploading.
The verified compilation target is Arduino AVR Boards 1.8.6, `arduino:avr:yun`.

Libraries: Adafruit BMP280 Library, Adafruit Unified Sensor, Adafruit BusIO,
DallasTemperature, OneWire, Servo and SSD1306Ascii. Adafruit GFX is not required.

```sh
arduino-cli compile --fqbn arduino:avr:yun .
```

Normal build: 25,506 / 28,672 bytes flash (88%); 869 / 2,560 bytes static RAM
(33%), leaving 1,691 bytes for stack/runtime allocations.

## Fixed vario frames

The former loop combined sensor reads, display transfers and a 638 ms pause.
The old hardware benchmarks measured about 641 ms per static-display cycle and
789 ms per changing-display cycle. The new scheduler uses phase-locked
**800,000 us** pressure-conversion starts, independent of screen changes.
800 ms was chosen near the earlier full-text/OLED timing; this is a stable,
conservative cadence rather than a new 10 Hz vario algorithm.

`VarioSampler.h` explicitly selects BMP280 forced mode, temperature x16,
pressure x16, internal IIR off, preserving the previous oversampling defaults.
Each frame starts a fresh conversion. The readout slot is planned at +80 ms
based on the approximately 65.8 ms conversion measured on this hardware.
The status register must confirm completion. A still-busy conversion is retried
without blocking until 110 ms, then discarded. The first start is scheduled
100 ms after initialization to let the previous normal conversion finish.

[Bosch BMP280 datasheet, forced-mode operation and measurement timing](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf)
explains host-triggered conversions; the actual hardware must still be checked
with the configured oversampling and scheduler.

The driver retains the actual successful trigger timestamp. The height
smoothing and vario calculation use the difference between these timestamps,
not assumed display-loop duration. The original four-height average followed
by four-vario average is retained. At 800 ms this filter has roughly 2.8 s group
delay; changing its response is a separate tuning task. Gaps above 1.5 frames
reset history instead of producing a climb spike. Bad status/pressure/timeout
results invalidate vario data. Pressure must be finite and in 300..1100 hPa.

Late frames do not shift the whole schedule: future deadlines stay on the
original phase. Missed slots are counted and skipped, never caught up with a
burst of fake extra measurements. `FrameTiming.h` checks deadline boundaries,
phase retention and timer rollover at compile time.

This is cooperative, **not a hard real-time guarantee**. Sensor/I2C errors,
interrupts, unexpected library delays or future unbudgeted work can still
cause lateness. The hardware jitter benchmark below is required to characterize
actual frame regularity; a successful build alone does not establish it.

## Background work and display

The scheduler gives pressure trigger/read deadlines first priority. Control
updates are scheduled at a fixed 800 ms cadence, nominally 20 ms after the
pressure readout slot. They continue even if pressure acquisition fails, so
manual throttle and temperature limiting do not depend on a successful vario
read. Invalid vario freezes the existing Autothrust reduction; it does not
invent climb or remove the pilot's requested throttle.

The old 638 ms blocking pause is removed. Between deadlines:

- OneWire work is allowed only with at least 50 ms spare time. Cached temperature
  reads measured about 12.5 ms; the margin includes command/poll overhead.
- `display.update(...)` only queues desired state. `display.service()` renders
  at most one 16-byte transfer chunk. A page is rendered once, then sent in
  eight chunks. Every chunk closes its Wire transaction before sensor work.
- Display work is allowed only with at least 30 ms until the next pressure or
  control deadline. A stable snapshot prevents mixed rows if values change
  while a frame is being sent; newer desired values follow after completion.
- Wire transactions have a 5 ms timeout to bound a stuck bus. The software
  budgets are conservative estimates and must be verified on real hardware.

UI, top to bottom: vario arrows (-2..+2 m/s; 0.4 m/s per field), applied power
lightning bolt (0..100%), thermometer (20..90 C; 7 C per field, 80 C marker),
battery (0..100%). Symbols are 13 x 12 pixels and bars 10 pixels high.
The only text is `Autothrust`, blinking while its real switch is active.
Battery below 25% and temperature above 81 C flash their whole bar; icons remain
visible. Blink half-period is 500 ms, serviced by the background loop.
Battery is still the original **unmeasured 100% placeholder** in live operation.

## Control behavior

The calibrated potentiometer range remains 370..980, mapped to 0..100% request.
Its four-sample smoothing runs at the 800 ms control cadence. Motor commands
remain Servo.write(50)..Servo.write(140) on pin 9.

Autothrust targets 0.6 m/s with a 0.2 m/s band: above 0.8 m/s it increases
reduction; below 0.4 m/s it decreases reduction. Adjustment is now
**2.5 percentage points per second**, equivalent to two points per 800 ms,
close to the original measured text-loop cadence. Elapsed time is used and
capped at one frame; missed iterations are not accumulated into a large power
jump. This is intentionally different from applying two points on every fast
background-loop iteration. It needs a hardware/control behavior check before
propulsion use. An existing reduction is retained while Autothrust is switched
off, as before.

Temperature above 80 C reduces the maximum power by ten percentage points per
degree, with a floor of 20% maximum power at 88 C and above. Pilot/Autothrust
requests below that cap remain effective. Until the first valid motor
temperature exists, permitted power is zero and the physical motor command is
the existing minimum. This is not a complete sensor-failure strategy.

## Asynchronous motor temperature

`MotorTemperature.h` starts immediately at live initialization, then schedules
requests at least 5,000 ms apart. Its ROM address is cached. Completed values
are read/CRC-checked without waiting through the sensor's conversion.
The scheduler's guarded background slots determine the remaining timing jitter.

The measured sensor uses 12 bits and external power. Parasite-powered devices
avoid readiness read slots during the resolution-dependent conversion interval.
A still-busy sensor is timed out at maximum conversion time plus 250 ms, checked
when the reader is serviced. Invalid readings never replace a valid temperature;
later failures retain the last valid value and expose `faulted()`. There is no
separate stale-temperature cutoff or displayed fault indicator. A sensor missing
at initialization leaves output at minimum until restart/reinitialization.

The existing `diagnostics/MotorTemperatureCheck` ran 21 state-machine checks
on the ATmega32U4 using the production reader with a simulated bus/clock. These
covered timing, invalid values, missing sensor, recovery and timer rollover.
The real acquisition times were subsequently measured in HardwareTiming v2.

## USB display/logic test

The native USB test mode is selectable only during the first 1,000 ms of sketch
startup, using USB enumeration plus VBUS. Normal operation is latched until
reset, so later USB attachment cannot activate test mode. A detected disconnect
from test mode latches normal operation too. This is a software interlock,
not an independent hardware safety mechanism.

In USB test mode simulated temperature, battery, throttle request and vario feed
the normal limiting logic. The real Autothrust switch remains active. The
27.5-second sequence cycles temperature 20..90 C, battery 100..0%, requested
power up/down, and both climb/sink directions. Background display updates can
show each input step; power limiting itself now follows the same 800 ms control
cadence as live operation. Physical motor output is always minimum in this mode.
After leaving demo, sensors/history are reinitialized and the original battery
placeholder is restored.

## Hardware timing verification

Build the same source with `VARIO_TIMING_BENCH` to measure actual conversion
start intervals under redraw/blink stress and real motor-temperature work:

```sh
arduino-cli compile --fqbn arduino:avr:yun --build-property compiler.cpp.extra_flags=-DVARIO_TIMING_BENCH .
```

This variant uses real sensors, bypasses USB simulated values and **always
writes minimum motor output**, regardless of USB state or sensor readings.
It reports 24 frame intervals (or times out after 30 seconds) after the host
opens Serial Monitor at 115200 baud. It uses non-blocking `Serial.dtr()`;
AVR `Serial::operator bool()` must not be used in a timed loop because it adds
10 ms. Reports include interval min/mean/max, start lateness, display chunk
costs, temperature poll costs, skipped slots and sensor errors. It also reports
`temperature_valid`, `temperature_fault` and the last integer temperature.
A missing temperature sensor means temperature-read workload was NOT tested.
A missing pressure sensor
or fewer than 24 intervals is an incomplete test, even if errors show zero.

Verify interval regularity, no missed slots, completed pressure conversions,
and maximum background-operation times below their reserved budgets. Repeat
with the real wiring, displays and sensors. This benchmark is not a flight
certification; verify PWM behavior and all limiting functions with propulsion
power disconnected before operation.

The older independent `diagnostics/HardwareTiming` and its bundled OLED/reader
snapshots intentionally remain on the previous code for comparison. Its v2
cycle report omits a 10 ms `Serial` bool check outside the measurement window;
see the 18.11.2026 log analysis. `diagnostics/Messung-speichern.ps1` captures that
program's switch-started measurements. Do not mistake old snapshot update
costs for the new incremental renderer's chunk costs.


### First real scheduler check (18.11.2026)

With the BMP280 and OLED connected, redraw/blink stress enabled and physical
motor output compile-time held at minimum, 24 consecutive intervals measured:

- Vario conversion starts: min 798.964 ms, mean 800.028 ms, max 801.200 ms.
- Start lateness: max 1.300 ms; no missed frames and no pressure errors.
- Result publication offset after conversion start: 82.764..84.064 ms.
- Control intervals: 798.908..800.620 ms.
- Background display service call: maximum 6.656 ms (30 ms budget).

The temperature sensor was absent (`temperature_valid=0`, `temperature_fault=1`),
so this run is not evidence of regularity during an actual temperature read.
Repeat with that sensor connected. These short-run min/max observations are
not guaranteed worst-case bounds or a flight validation.
