# Build This Project In VS Code With PlatformIO

This workspace has been converted from an Arduino `.ino` sketch to a PlatformIO project with a standard C++ entrypoint at `src/main.cpp`.

## 1. Install PlatformIO in VS Code

Install the recommended extension:

- `PlatformIO IDE`

Or install the CLI separately if you prefer terminal-based builds:

```sh
python3 -m pip install --user platformio
```

## 2. Open the folder in VS Code

Open this folder as the workspace root:

```sh
/Users/katherine/Downloads/o2_analyzer
```

## 3. Build the firmware

Default environment in `platformio.ini`:

```ini
[env:uno]
platform = atmelavr
board = uno
framework = arduino
```

In VS Code you can build with:

- `PlatformIO: Build`
- `Terminal: Run Build Task`

Or from a terminal:

```sh
platformio run
```

## 4. Upload the firmware

Use:

- `PlatformIO: Upload`

Or from a terminal:

```sh
platformio run --target upload
```

## 5. Serial monitor

The monitor speed is set to `9600` in `platformio.ini`.

From VS Code:

- `PlatformIO: Monitor`

Or from a terminal:

```sh
platformio device monitor --baud 9600
```

## Dependencies

PlatformIO installs these automatically from `platformio.ini`:

- `Adafruit GFX Library`
- `Adafruit SSD1306`
- `Adafruit ADS1X15`
- `RunningAverage`

`SPI`, `Wire`, and `EEPROM` come from the Arduino framework package for the selected board.

## Notes

- The project is currently configured for Arduino Uno because that was the last successful compile target.
- If you switch boards later, update the `board` value in `platformio.ini`.
- The old sketch file has been replaced by `src/main.cpp`, which required adding explicit forward declarations that Arduino normally generates automatically for `.ino` files.