# O2 Analyzer Firmware

Firmware for an Arduino-based nitrox / oxygen analyzer with a 128x64 SSD1306 OLED display, ADS1115 ADC, buzzer feedback, EEPROM-backed calibration, and a single-button interface.

This repository is set up as a PlatformIO project and currently targets an `Arduino Uno`.

## What It Does

- Reads an oxygen sensor through an `ADS1115` differential input
- Smooths readings with a running average
- Displays the current O2 percentage on an OLED
- Shows measured sensor output in millivolts
- Calculates MOD values for the selected working `pO2`
- Stores calibration data in EEPROM so it persists across reboots
- Supports quick actions from one button:
  - short press: lock screen
  - hold ~2 seconds: calibrate
  - hold ~3 seconds: cycle working `pO2` between `1.3`, `1.4`, and `1.5`
  - hold ~4 seconds: clear max reading

## Hardware Used

- Arduino Uno
- SSD1306 OLED display (`128x64`, I2C, address `0x3C`)
- ADS1115 ADC
- Oxygen sensor wired to the ADS1115 differential input
- Push button on pin `2`
- Buzzer on pin `9`

Important firmware constants are defined in [src/main.cpp](/Users/katherine/Downloads/o2-analyzer/src/main.cpp:27).

## Build Setup

Project configuration lives in [platformio.ini](/Users/katherine/Downloads/o2-analyzer/platformio.ini:1):

```ini
[env:uno]
platform = atmelavr
board = uno
framework = arduino
monitor_speed = 9600
```

Install PlatformIO in either of these ways:

- VS Code extension: `PlatformIO IDE`
- CLI: `python3 -m pip install --user platformio`

## Build

From the repo root:

```sh
platformio run
```

## Upload

```sh
platformio run --target upload
```

If PlatformIO does not auto-detect the serial port on your machine, add `upload_port` in `platformio.ini`.

## Serial Monitor

```sh
platformio device monitor --baud 9600
```

## Dependencies

PlatformIO installs these libraries automatically:

- `Adafruit GFX Library`
- `Adafruit SSD1306`
- `Adafruit ADS1X15`
- `RunningAverage`

Core Arduino libraries used by the firmware:

- `SPI`
- `Wire`
- `EEPROM`

## Calibration Notes

- Calibration is based on ambient air and uses `20.9%` as the reference value.
- Calibration is saved to EEPROM with a validity marker.
- If saved calibration data is missing or invalid, the firmware forces a fresh calibration during startup.

## Project Origin

The source header references ejlabs' OLED nitrox analyzer project:

- http://ejlabs.net/arduino-oled-nitrox-analyzer

This repo contains a PlatformIO-based C++ firmware entrypoint in [src/main.cpp](/Users/katherine/Downloads/o2-analyzer/src/main.cpp:1).

## License

This project is licensed under the GNU GPL v3. See [LICENSE](/Users/katherine/Downloads/o2-analyzer/LICENSE:1).
