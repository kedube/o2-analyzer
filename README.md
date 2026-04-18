# O2 Analyzer Firmware

PlatformIO firmware for an Arduino-based nitrox / oxygen analyzer with a 128x64 SSD1306 OLED, ADS1115 ADC, buzzer feedback, EEPROM-backed calibration, and a single-button UI.

## Images

![O2 analyzer front view](images/nitrox_analyzer-1.jpeg)

![O2 analyzer display close-up](images/nitrox_analyzer-2.jpeg)

![O2 analyzer internal wiring](images/nitrox_analyzer-3.jpeg)

![O2 analyzer alternate assembly view](images/nitrox_analyzer-4.jpeg)

The current project target is an Arduino Nano ATmega328P with the new bootloader configuration.

## Features

- Reads the oxygen sensor through an ADS1115 differential input
- Smooths sensor readings with a lightweight local moving average
- Displays live O2 percentage, max reading, sensor millivolts, and MOD values
- Stores calibration data in EEPROM so it survives power cycles
- Uses a single button for lock, calibration, pO2 selection, and max-clear actions
- Uses centered OLED layouts and subsetted Adafruit GFX fonts to preserve the condensed UI while reducing flash usage
- Skips redundant OLED redraws to reduce display flicker and unnecessary work

## Hardware

- Arduino Nano ATmega328P (new bootloader)
- SSD1306 OLED display, 128x64, I2C, address `0x3C`
- ADS1115 ADC
- Oxygen sensor wired to ADS1115 differential input `A0/A1`
- Push button on digital pin `2`
- Buzzer on digital pin `9`

## Button Actions

- Tap: lock screen
- Hold about 2 seconds: calibrate in air
- Hold about 3 seconds: cycle working pO2 between `1.3`, `1.4`, and `1.5`
- Hold about 4 seconds: clear the max reading

## Project Layout

```text
o2-analyzer/
├── LICENSE
├── README.md
├── RELEASE_NOTES_0.25.md
├── platformio.ini
├── images/
│   ├── nitrox_analyzer-1.jpeg
│   ├── nitrox_analyzer-2.jpeg
│   ├── nitrox_analyzer-3.jpeg
│   └── nitrox_analyzer-4.jpeg
├── include/
│   ├── FreeSans9pt7bSubset.h
│   └── FreeSansBold18pt7bSubset.h
└── src/
  └── main.cpp
```

- [src/main.cpp](src/main.cpp): firmware logic, UI rendering, button handling, calibration, and sensor processing
- [include/FreeSans9pt7bSubset.h](include/FreeSans9pt7bSubset.h): subset small UI font
- [include/FreeSansBold18pt7bSubset.h](include/FreeSansBold18pt7bSubset.h): subset large percentage font
- [platformio.ini](platformio.ini): PlatformIO target, libraries, and build flags

## Build Configuration

Current PlatformIO configuration:

```ini
[platformio]
default_envs = nano

[env]
framework = arduino
monitor_speed = 9600
upload_speed = 115200
build_flags =
  -D SSD1306_NO_SPLASH

[env:nano]
platform = atmelavr
board = nanoatmega328new

[env:uno]
extends = env:nano
```

Install PlatformIO with either the VS Code PlatformIO extension or the CLI:

```sh
python3 -m pip install --user platformio
```

## Build

From the repository root:

```sh
platformio run
```

## Upload

```sh
platformio run --target upload
```

If PlatformIO does not auto-detect the serial port on your machine, add `upload_port` to [platformio.ini](platformio.ini).

## Serial Monitor

```sh
platformio device monitor --baud 9600
```

## Dependencies

PlatformIO installs these libraries automatically:

- Adafruit GFX Library
- Adafruit SSD1306
- Adafruit ADS1X15

Core Arduino libraries used directly by the firmware:

- Wire
- EEPROM

## Calibration Notes

- Calibration is based on ambient air and uses `20.9%` oxygen as the reference point.
- Calibration is stored in EEPROM with a validation marker.
- If saved calibration data is missing or invalid, the firmware forces a new calibration during startup.
- The firmware constrains calibration input to a sane range before saving it.

## Firmware Notes

- The splash bitmap in Adafruit SSD1306 is disabled at build time to save flash.
- The small and large UI fonts are subsetted to only the glyphs used by the analyzer screens.
- Display updates are cached so unchanged frames are not redrawn.
- Current builds are comfortably within ATmega328P limits.

## Origin

The source header references ejlabs' OLED nitrox analyzer project:

- http://ejlabs.net/arduino-oled-nitrox-analyzer

## License

Licensed under GNU GPL v3. See [LICENSE](LICENSE).
