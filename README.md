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
motor temperature (°C)
currently applied power (%)
remaining battery capacity (%) [feature not yet enabled]
