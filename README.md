# ThermikatorControl
Thrust controller with auto thrust limiter feature for climbrate

The Thermikator is an electric paragliding power unit designed and first build by Markus Dörr.
http://www.paragliding-kochertal.de/aktuell/emot.html

The 5KW electric motor is controlled by a brushless motor controller that can handle currents of up to 170A. The motor controller accepts PWM signals to set the desired power. The ThermicatorControl hardware mainly consists of an Ardunino compatible Sparkfun Micro Pro, Bosch BMP280 humidity, temperature & pressure sensor, OLED display and potentiometer with mechanical unit.

The software in this repository programs the Arduino to read the desired power setting from the potentiometer and generate an correspondig PWM signal in a linear manner to the potentiometer setting.

The BPM280 is used to determine the current climbrate. If autothrust is activated, the climbrate is limited to an average of 0,5m/s by reducing the desired power if the climbrate is exceeded. If the climrate drops below 0,5 m/s the power is increased again to the desired power. With this feature engaged the flight in thermal active air is a lot more smoother. But the main advantage is that the pilot does not need to adapt his desired power setting all the time when trying to find the core of a thermal: The engine will automatically fade out once the rising air can lift the paraglider without added power. This makes best use of battery capacity and prevents the motor from heating up unnecessary.

An extra thermal sensor attached to the motor casing is used to read motor temperature. The desired power is also reduced linear once the motor is exceeding 80°C, which might happen if full power is applied continuosly.

The PWM signal is always generated from currently allowed power setting, respecting desired power, current climbrate and engine temperature. This power setting is the currently applied power.

The OLED Display is used to display the following parameters in this order:
average climbrate (m/s)
currently applied power (%)
motor temperature (°C)
remaining battery capacity (%) [feature not yet enabled]

28.07.2021


## OLED gauge design (branch NewUIDesing)

This branch starts from `origin/main` (`c1afbf1`) and implements the approved
bar-gauge design on the existing **128 x 64 SSD1306 I2C OLED**. No E-paper
hardware, SPI wiring or E-paper driver is included.

### Wiring and dependencies

Keep the existing wiring: OLED SDA on Pro Micro pin 2, SCL on pin 3,
VCC and GND as before. I2C address is `0x3C` (`I2C_ADDRESS` in the sketch).
The BMP280 remains on the same bus. OneWire (8), motor PWM (9), autothrust
switch (10) and potentiometer A0 (18) retain their pin assignments.

Use the same libraries as main: SSD1306Ascii, Adafruit BMP280 Library,
Adafruit Unified Sensor, Adafruit BusIO, DallasTemperature, OneWire and Servo.
Adafruit GFX and Adafruit SSD1306 are not required.

### Layout

The compact OLED layout uses white pixels on the existing dark background:

| Top to bottom | Symbol | Gauge scale |
| --- | --- | --- |
| Vario | Up/down arrows | -2 to +2 m/s, zero centered |
| Applied motor power | Lightning bolt | 0 to 100% |
| Motor temperature | Thermometer | 20 to 90 C; fixed 80 C marker |
| Battery | Battery outline | 0 to 100% |

The header blinks the full word `Autothrust` only while the existing switch is
active. No other text, numeric scale labels or units are displayed. Symbols are 13 x 12 pixels so four rows plus the header fit in 64
scanlines without overlap. The bars are 10 pixels high (previously 5); vario zero and the 80 C threshold
remain marked without text. Battery remains the original **unmeasured 100% placeholder**.

Gauges round to ten segments: 10 percentage points, 7 C, or 0.4 m/s per
vario segment. Vario fills left for sinking and right for climbing. Out-of-range
values saturate the displayed scale; display scaling does not clamp the
values used for power regulation.

`OledGaugeDisplay` uses one 128-byte, eight-scanline page buffer and keeps
symbols in flash. The eight pages are written through the existing
SSD1306Ascii library, without clearing the screen before each frame.
The final buffered I2C transaction is flushed before returning to sensor code.
Updates are checked **every control loop**, with no five-second E-paper delay;
unchanged visible segments/header skip the display transfer. The blink timer
is non-blocking: after at least 500 ms the next control loop toggles the header.
The existing sensor conversion/delay cadence determines the actual blink rate;
no additional delay is inserted. A header-only change transfers just page 0. Updating all
pages takes I2C transfer time and is not instantaneous. The existing 100 kHz
bus clock and temperature conversion/delay cadence remain in use.

The new UI is updated after applying the current motor command. Loop timing
uses unsigned `millis()` values and guarded floating-point division, avoiding
the old signed 16-bit rollover, division by zero and integer truncation. The
temperature scheduling counter wraps every four cycles. These timing fixes
can affect climb-rate calculation compared with main and should be checked
on hardware alongside the display. Autothrust thresholds, temperature power
limiting and PWM mapping retain their existing settings.

### Build and validation

Compile using the actual Pro Micro board and its clock/voltage setting. For a
16 MHz ATmega32U4 compilation check with Arduino AVR Boards 1.8.6:

```sh
arduino-cli compile --fqbn arduino:avr:leonardo .
```

Leonardo verifies the MCU build; select the actual Pro Micro bootloader and
8 MHz/16 MHz variant when uploading. Tested with SSD1306Ascii from the local
Arduino installation. Latest build target: `arduino:avr:yun` (the connected ATmega32U4 board on COM4).
Build result: **23,472 / 28,672 bytes flash (81%)** and
**808 / 2,560 bytes static RAM (31%)**, leaving 1,752 bytes for stack/runtime.
The layout and symbol/font bitmaps were also inspected as a pixel preview.
No physical OLED or propulsion hardware was available.

Before motor operation, test with the motor power circuit disconnected:

1. Blinking Autothrust header, four symbols and thicker bars, including the bottom
   battery row; no scale labels or other text.
2. Vario below/above zero, empty/full gauges, and fixed 80 C marker on the
   20..90 C scale; old filled segments disappear when values decrease.
3. Toggle autothrust: the full word starts blinking when enabled and
   disappears completely when disabled, without remnants.
4. Confirm BMP280 readings remain available after an OLED update, and check
   PWM output/control cadence during changing gauges and temperature limiting.
5. Check the climb-rate timing beyond the former 16-bit overflow boundary.


### OLED warning blink

Battery below 25% and motor temperature above 81 C flash their entire gauge
(including empty segment outlines) on/off; the identifying symbols remain
visible. Exactly 25% and exactly 81 C do not activate the warning. Thresholds
use the unrounded input values, not the ten-segment display state. Temperature
is currently read into an integer, so the first warning value is 82 C.

Warnings share the existing non-blocking Autothrust blink phase, serviced once
per control loop. They also blink when Autothrust is off, and return to a steady
bar when the warning clears. A gauge blink invalidates the affected display
state even if the segment count has not changed. Header-only blink updates
still transfer just page 0. The existing motor power reduction above 80 C is
unchanged; the warning threshold is independent of that control threshold.

Temperature fill is clamped to 20..90 C: empty at/below 20 C, full at/above
90 C, rounded to the nearest 7 C step. The 80 C marker is repositioned to
match the new scale. Battery still has no sensor or capacity calculation:
its original 100% placeholder means the low-battery warning will not occur
until real capacity data is supplied.

Hardware checks: exercise battery 24/25%, temperature 81/82 C, temperature
20/90 C, and simultaneous warnings with Autothrust on and off. Verify a warning
clears even when both input values round to the same number of segments.


### Automatic USB bench test

Connecting the native ATmega32U4 USB port to a computer automatically selects
bench mode, without opening Serial Monitor. Detection requires both USB host
enumeration and VBUS; a power-only USB charger does not enable it. The mode
starts a repeating 27.5-second sequence with a new input step every 250 ms:

- Motor temperature: 20 to 90 C, then hold until the cycle restarts at 20 C.
- Battery input: 100 down to 0%, hold empty, then restart at 100%.
- Requested power: 0 to 100%, hold full, then back down, repeatedly.
- Vario input: zero to +2 m/s, hold, through zero to -2 m/s, hold, back to zero.

The real Autothrust switch remains active; it is never mocked. Test values
are assigned to `Temperature`, `RemainingAkku`, `PotiSetPower` and
`VarioAverage`, before the existing Autothrust and temperature limiting code.
The displayed power is **AllowedPower**, not the requested test value. Full
requested power can therefore be reduced by climb/Autothrust or by simulated
motor heat; at 90 C thermal limiting permits at most 20%. Warning blinks use
the same simulated temperature and battery inputs. With Autothrust off, the
initial cool sweep can show the full power range; with it on, a full gauge
is intentionally not guaranteed because the real limiter is working.

`PowerPWM` is calculated as usual for inspection, but the physical servo/PWM
output on pin 9 is held at `MinPowerPWM` throughout USB bench mode. No test
value commands the real motor. Sensors need not be attached for this mode;
sensor initialization/conversion is skipped. The animation loop uses a short
20 ms pause instead of live sensor-conversion delays, so cycle-dependent
Autothrust adjustments run faster than in live operation. It demonstrates the
logic and UI, not the real-time flight response.

When USB is disconnected (with separate power maintained), normal sensor
operation resumes. Measurement/vario history and power reduction state are
reinitialized and the previous battery value is restored, so simulated values
do not leak into live measurement history. A reconnect starts the demo from
the beginning. If USB is the only power source, unplugging simply switches
the board off. This mode is specific to the current native-USB ATmega32U4
boards, not ESP8266/NodeMCU USB-serial adapters.
