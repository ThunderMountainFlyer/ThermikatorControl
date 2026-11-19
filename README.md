# ThermikatorControl

Thrust controller for the Thermikator electric paragliding power unit, with
barometric Autothrust limiting, motor-temperature limiting and an OLED UI.
The Thermikator was designed and first built by Markus Dörr:
http://www.paragliding-kochertal.de/aktuell/emot.html

The firmware on `main` uses the existing SSD1306 OLED and
**800 ms fixed vario frames**.

## Firmware versions

The current firmware version is **2.0.2**, defined in `FirmwareVersion.h`.
At boot the OLED shows the version centered at 4x font scale (116 x 28 pixels
for `2.0.2`). Once the complete image has been transmitted, one common
**1,000 ms** window holds the version on screen and detects USB enumeration
plus VBUS. The window ends before normal gauges/control start. Motor output
stays at minimum throughout startup. Rendering is incremental; there is no
additional blocking one-second delay. Longer version strings scale down to fit.

For every subsequent delivered firmware revision, update this version using
Semantic Versioning: PATCH for fixes, MINOR for compatible features, MAJOR for
incompatible changes. Build and check the final revision, commit it, then create
an annotated tag `vMAJOR.MINOR.PATCH` matching `FIRMWARE_VERSION` on that commit.
Push the branch and its tag. Keep published tags immutable; use a new version
for later changes. Documentation-only changes do not require a firmware version.

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

Build size is reported by the compiler; check flash and RAM headroom for each
release. The ATmega32U4 target provides 28,672 bytes flash and 2,560 bytes RAM.

## Fixed vario frames

The scheduler uses phase-locked **800,000 us** pressure-conversion starts,
independent of screen changes. Timing constants are in [FrameTiming.h](FrameTiming.h).

`VarioSampler.h` explicitly selects BMP280 forced mode, temperature x16,
pressure x16 and internal IIR off.
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

Between deadlines:

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

UI, top to bottom: vario arrows (-0.6..+1.4 m/s; 0.2 m/s per field), applied power
lightning bolt (0..100%), thermometer (20..90 C; 7 C per field, 80 C marker),
battery (0..100%). Symbols are 13 x 12 pixels and bars 10 pixels high.
After the startup version screen, the only text is `Autothrust`, blinking
while its real switch is active.
Vario zero is marked between three sink blocks on the left and seven climb
blocks on the right. At zero no blocks are filled; values round to the nearest
block (first block at +/-0.1 m/s) and saturate at the displayed endpoints.
This display scale leaves the measured vario and Autothrust settings unchanged.
Battery below 25% and temperature above 81 C flash their whole bar; icons remain
visible. Blink half-period is 500 ms, serviced by the background loop.
Battery is still the original **unmeasured 100% placeholder** in live operation.

## Control behavior

The calibrated potentiometer range remains 370..980, mapped to 0..100% request.
Each 800 ms control step uses the current analog reading directly, without a
moving average. A throttle change therefore reaches the next control step
(up to about 800 ms later); the motor controller applies its own configured
acceleration/deceleration ramp. Motor commands remain Servo.write(50)..
Servo.write(140) on pin 9.

Autothrust targets 0.6 m/s with a 0.2 m/s band: above 0.8 m/s it increases
reduction; below 0.4 m/s it decreases reduction. Adjustment is
**10 percentage points per second**, equivalent to eight points per 800 ms,
in both directions. Elapsed time is used and capped at one frame; missed iterations are not accumulated into a large power
jump. While Autothrust is off, its stored reduction is retained but not applied;
switching it off can therefore immediately restore the pilot's requested power,
subject to temperature limiting. Firmware has no common output ramp; the motor
controller applies its configured ramp.

Temperature above 80 C reduces the maximum power by ten percentage points per
degree, with a floor of 20% maximum power at 88 C and above. Pilot/Autothrust
requests below that cap remain effective. Until the first valid motor
temperature exists, permitted power is zero and the physical motor command is
the existing minimum. The control/UI use whole degrees Celsius, truncating the
temperature reader's floating-point value. There is no separate sensor-fault
indicator on the OLED.

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

## USB display/logic test

The native USB test mode is selectable only during the shared 1,000 ms startup
window, starting when the version image is fully visible (after OLED
initialization/transmission), using USB enumeration plus VBUS. Even an early
USB detection waits for the full window before starting simulated control.
Normal operation is latched until reset, so later USB attachment cannot
activate test mode. A detected disconnect from test mode latches normal
operation too. This is a software interlock,
not an independent hardware safety mechanism.

In USB test mode simulated temperature, battery, throttle request and vario feed
the normal limiting logic. The real Autothrust switch remains active. The
27.5-second sequence cycles temperature 20..90 C, battery 100..0%, requested
power up/down, and vario 0 -> +1.4 -> 0 -> -0.6 -> 0 m/s in 0.2 m/s steps,
holding the endpoints so all blocks can be seen. Background display updates can
show each input step; power limiting itself now follows the same 800 ms control
cadence as live operation. Physical motor output is always minimum in this mode.
After leaving demo, sensors/history are reinitialized and the original battery
placeholder is restored.

## Diagnostic programs

| Program | Purpose and start condition |
| --- | --- |
| `VARIO_TIMING_BENCH` build of the root sketch | Current scheduler with real sensors and display stress; starts recording when the serial port opens |
| [HardwareTiming](diagnostics/HardwareTiming/README.md) | Standalone component/conversion measurements with an older synchronous OLED snapshot; starts on Autothrust OFF/ON with serial open |
| [MotorTemperatureCheck](diagnostics/MotorTemperatureCheck/MotorTemperatureCheck.ino) | 21 temperature-reader checks using a simulated bus/clock; starts when the serial port opens |

The two standalone sketches replace the controller firmware when uploaded.
They hold pin 9 at the minimum motor command. After testing, upload the root
controller sketch again. HardwareTiming's detailed instructions and capture
script are documented in its linked README. The temperature self-test needs
no sensor; its local reader is a copy of the production `MotorTemperature.h`.
Its serial output ends with `CHECKS=21`, `FAILURES=0`, `# DONE.` on success.

Build the self-test from the repository root:

```sh
arduino-cli compile --fqbn arduino:avr:yun diagnostics/MotorTemperatureCheck
```

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

### Recorded scheduler measurement (18.11.2026)

The run at commit `13c8676` used the BMP280, OLED and motor-temperature sensor,
with artificial redraw/blink stress and motor output held at minimum.
These are recorded measurements of that revision, not a new test of 2.0.2:

- 24 vario intervals: **799.040 / 799.967 / 800.608 ms** (min/mean/max).
- No skipped frames or pressure errors; temperature valid at 16 C, no reader fault.
- Maximum temperature poll: 12.556 ms (50 ms reserved budget).
- Maximum OLED chunk: 6.660 ms (30 ms reserved budget).
- Control intervals: 798.964..801.080 ms.
- Result publication: 82.800..83.948 ms after pressure start.

These short-run observations are not hard worst-case bounds or flight validation.
