/*****************************************************************************
* 
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*****************************************************************************/

#include <Arduino.h>
#include <Wire.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_ADS1X15.h>
#include <EEPROM.h>
#include "FreeSansBold18pt7bSubset.h"
#include "FreeSans9pt7bSubset.h"
#include "display_ui.h"
#include "settings.h"

struct CalibrationData {
  uint8_t magic;
  uint16_t value;
};

struct SettingsData {
  uint8_t magic;
  uint8_t maxPo1Tenths;
  uint8_t flags;
};

constexpr uint8_t kSettingsBuzzerFlag = 0x01;
constexpr uint8_t kSettingsModInFeetFlag = 0x02;
constexpr uint8_t kMinPo2Tenths = 13;
constexpr uint8_t kMaxPo2Tenths = 15;

struct AnalyzerState {
  float calibrationValue = 0.0F;
  float calibrationScale = 0.0F;
  float resultMax = 0.0F;
  float maxPo1 = kDefaultMinPo2;
  bool buzzerEnabled = kBuzzerEnabledByDefault;
  bool modInFeet = kModInFeetByDefault;
  unsigned long millisHeld = 0;
  byte previousButtonState = HIGH;
  unsigned long firstPressTime = 0;
  int activeFrames = 0;
  unsigned long lastUiRefreshMs = 0;
  unsigned long lastSensorSampleMs = 0;
  unsigned long lastActivityMs = 0;
  int16_t activityResultTenths = 0;
  bool ignoreHeldButton = false;
};

struct SensorAverage {
  int16_t samples[kRaSize] = {};
  int32_t sum = 0;
  uint8_t count = 0;
  uint8_t nextIndex = 0;

  void clear() {
    sum = 0;
    count = 0;
    nextIndex = 0;
    for (uint8_t i = 0; i < kRaSize; ++i) {
      samples[i] = 0;
    }
  }

  void addValue(int16_t value) {
    if (count < kRaSize) {
      samples[nextIndex] = value;
      sum += value;
      ++count;
    } else {
      sum -= samples[nextIndex];
      samples[nextIndex] = value;
      sum += value;
    }

    ++nextIndex;
    if (nextIndex >= kRaSize) {
      nextIndex = 0;
    }
  }

  float average() const {
    if (count == 0) {
      return 0.0F;
    }
    return static_cast<float>(sum) / static_cast<float>(count);
  }
};

int calibrate();
bool readCalibration(float &value);
void writeCalibration(uint16_t value);
void readSettings();
void writeSettings();
void clearSettings();
bool consumeWakeMarker();
bool settingsResetRequested();
bool isCalibrationValid(float value);
void pauseWithPolling(unsigned long durationMs);
void readSensor();
void sampleSensorIfDue();
void powerDown();
bool powerOffWarningCancelled();
void analyze();
void invalidateDisplaySnapshot();
void handleSerialCommands();
void dumpDisplayBuffer();
void logBootMessage(const __FlashStringHelper *message);
void updateCalibrationScale(float calibrationValue);
int16_t roundToTenths(float value);
int16_t roundToHundredths(float value);
uint8_t menuIndexForHoldDuration(unsigned long heldDurationMs);
uint8_t currentHoldMenu();
void runHoldMenuAction(uint8_t menuIndex);
void po2_change();
void buzzer_toggle();
void mod_unit_toggle();
void max_clear();

SensorAverage sensorAverage;
AnalyzerState state;
DisplaySnapshot lastDisplaySnapshot;

Adafruit_ADS1115 ads;
Adafruit_SSD1306 display(kScreenWidth, kScreenHeight, &Wire, kOledReset);

constexpr unsigned long kMsPerSecond = 1000;
constexpr unsigned long kMenuEntryHoldMs = kMenuEntryHoldSeconds * kMsPerSecond;
constexpr unsigned long kButtonDebounceMs = 200;
constexpr unsigned long kUiRefreshIntervalMs = 200;
constexpr unsigned long kPausePollMs = 10;
// One ADS1115 conversion takes 4ms at 250SPS; spacing warmup reads by a bit
// more guarantees each read sees a fresh conversion in continuous mode.
constexpr unsigned long kAdsConversionSpacingMs = 5;
constexpr uint8_t kHoldMenuItemCount = 5;
static_assert(kMenuStepIntervalMs > 0, "kMenuStepIntervalMs must be greater than 0");

/*
 Calculate MOD (Maximum Operating Depth)
*/
float calculateModDepth(float percentage, float ppo2 = 1.4F) {
  if (percentage <= 0.0F) {
    return 0.0F;
  }

  const float depthMeters = 10.0F * ((ppo2 / (percentage / 100.0F)) - 1.0F);
  if (!state.modInFeet) {
    return depthMeters;
  }

  return depthMeters * kFeetPerMeter;
}

void beep(int x = 1) {  // make beep for x time
  if (!state.buzzerEnabled) {
    return;
  }

  for (int i = 0; i < x; i++) {
    tone(kBuzzerPin, 2800, 100);
    pauseWithPolling(200);
  }
  noTone(kBuzzerPin);
}

void pauseWithPolling(unsigned long durationMs) {
  const unsigned long startMs = millis();
  while ((millis() - startMs) < durationMs) {
    handleSerialCommands();
    sampleSensorIfDue();
    delay(kPausePollMs);
  }
}

void logBootMessage(const __FlashStringHelper *message) {
  if (!kBootDebugLogging) {
    return;
  }

  Serial.println(message);
}

void dumpDisplayBuffer() {
  const uint8_t *buffer = display.getBuffer();
  const size_t bufferSize =
      static_cast<size_t>(kScreenWidth) * static_cast<size_t>(kScreenHeight) / 8U;

  Serial.print(F("OLED_FRAME "));
  Serial.print(kScreenWidth);
  Serial.print(' ');
  Serial.println(kScreenHeight);
  Serial.write(buffer, bufferSize);
  Serial.flush();
}

void handleSerialCommands() {
  while (Serial.available() > 0) {
    const int input = Serial.read();
    if (input == kScreenshotCommand || input == 'S') {
      dumpDisplayBuffer();
    }
  }
}

void readSensor() {
  // The ADS1115 free-runs in continuous mode; this just fetches the latest
  // completed conversion instead of blocking on a new one.
  sensorAverage.addValue(ads.getLastConversionResults());
}

void sampleSensorIfDue() {
  const unsigned long now = millis();
  if ((now - state.lastSensorSampleMs) >= kSensorSampleIntervalMs) {
    state.lastSensorSampleMs = now;
    readSensor();
  }
}

void invalidateDisplaySnapshot() {
  lastDisplaySnapshot.initialized = false;
}

int16_t roundToTenths(float value) {
  const float scaled = value * 10.0F;
  return static_cast<int16_t>(scaled >= 0.0F ? scaled + 0.5F : scaled - 0.5F);
}

int16_t roundToHundredths(float value) {
  const float scaled = value * 100.0F;
  return static_cast<int16_t>(scaled >= 0.0F ? scaled + 0.5F : scaled - 0.5F);
}

uint8_t menuIndexForHoldDuration(unsigned long heldDurationMs) {
  constexpr uint8_t kMenuCycleSlots = kHoldMenuItemCount + 1;

  if (heldDurationMs < kMenuEntryHoldMs) {
    return 0;
  }

  const unsigned long menuElapsedMs = heldDurationMs - kMenuEntryHoldMs;
  const uint8_t cycleSlot =
      static_cast<uint8_t>((menuElapsedMs / kMenuStepIntervalMs) % kMenuCycleSlots);
  if (cycleSlot == kHoldMenuItemCount) {
    return 0;
  }

  return cycleSlot + 1;
}

uint8_t currentHoldMenu() {
  return menuIndexForHoldDuration(state.millisHeld);
}

void runHoldMenuAction(uint8_t menuIndex) {
  if (menuIndex == 1) {
    state.calibrationValue = calibrate();
  } else if (menuIndex == 2) {
    po2_change();
  } else if (menuIndex == 3) {
    buzzer_toggle();
  } else if (menuIndex == 4) {
    mod_unit_toggle();
  } else if (menuIndex == 5) {
    max_clear();
  }
}

bool isCalibrationValid(float value) {
  return value >= kMinValidCalibration && value <= kMaxValidCalibration;
}

void writeCalibration(uint16_t value) {
  const CalibrationData data = {kCalibrationMagic, value};
  const uint8_t *raw = reinterpret_cast<const uint8_t *>(&data);
  for (unsigned int i = 0; i < sizeof(CalibrationData); ++i) {
    EEPROM.update(kCalibrationAddress + i, raw[i]);
  }
}

void updateCalibrationScale(float calibrationValue) {
  state.calibrationValue = calibrationValue;
  state.calibrationScale =
      isCalibrationValid(calibrationValue)
          ? static_cast<float>(kAirCalibrationPercent) / calibrationValue
          : 0.0F;
}

void writeSettings() {
  SettingsData data = {kSettingsMagic,
                       static_cast<uint8_t>(roundToTenths(state.maxPo1)), 0};
  if (state.buzzerEnabled) {
    data.flags |= kSettingsBuzzerFlag;
  }
  if (state.modInFeet) {
    data.flags |= kSettingsModInFeetFlag;
  }
  EEPROM.put(kSettingsAddress, data);
}

void readSettings() {
  SettingsData data = {};
  EEPROM.get(kSettingsAddress, data);
  if (data.magic != kSettingsMagic) {
    logBootMessage(F("BOOT: settings defaults"));
    return;
  }

  if (data.maxPo1Tenths >= kMinPo2Tenths && data.maxPo1Tenths <= kMaxPo2Tenths) {
    state.maxPo1 = static_cast<float>(data.maxPo1Tenths) / 10.0F;
  }
  state.buzzerEnabled = (data.flags & kSettingsBuzzerFlag) != 0;
  state.modInFeet = (data.flags & kSettingsModInFeetFlag) != 0;
  logBootMessage(F("BOOT: settings loaded"));
}

void clearSettings() {
  EEPROM.update(kSettingsAddress, static_cast<uint8_t>(~kSettingsMagic));
}

bool consumeWakeMarker() {
  if (EEPROM.read(kWakeMarkerAddress) != kWakeMarkerMagic) {
    return false;
  }
  EEPROM.update(kWakeMarkerAddress, static_cast<uint8_t>(~kWakeMarkerMagic));
  return true;
}

bool settingsResetRequested() {
  const unsigned long startMs = millis();
  while (digitalRead(kButtonPin) == LOW) {
    if ((millis() - startMs) >= kSettingsResetHoldMs) {
      return true;
    }
    delay(kPausePollMs);
  }
  return false;
}

bool readCalibration(float &value) {
  CalibrationData data = {};
  uint8_t *raw = reinterpret_cast<uint8_t *>(&data);
  for (unsigned int i = 0; i < sizeof(CalibrationData); ++i) {
    raw[i] = EEPROM.read(kCalibrationAddress + i);
  }

  if (data.magic != kCalibrationMagic || !isCalibrationValid(data.value)) {
    return false;
  }

  value = data.value;
  return true;
}

void setup(void) {
  Serial.begin(kSerialBaudRate);
  logBootMessage(F("BOOT: serial"));

  pinMode(kButtonPin, INPUT_PULLUP);
  pinMode(kBuzzerPin, OUTPUT);
  digitalWrite(kBuzzerPin, LOW);
  delay(10);

  // A wake press from auto power-off is still held during this reboot; it
  // must not be mistaken for the hold-to-reset gesture.
  const bool wokeFromAutoOff = consumeWakeMarker();

  if (!wokeFromAutoOff && settingsResetRequested()) {
    // Button held through power-on: factory-reset the saved settings.
    clearSettings();
    logBootMessage(F("BOOT: settings reset"));
    while (digitalRead(kButtonPin) == LOW) {
      delay(kPausePollMs);
    }
  } else {
    readSettings();
  }

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, kScreenAddress)) {
    logBootMessage(F("BOOT: oled fail"));
    for (;;) {
    }  // Don't proceed, loop forever
  }
  logBootMessage(F("BOOT: oled ok"));

  if (!wokeFromAutoOff) {
    renderStartupScreen(display, VERSION);
    logBootMessage(F("BOOT: startup shown"));
    delay(kSplashScreenMs);
  }
  invalidateDisplaySnapshot();

  ads.setGain(GAIN_TWO);
  ads.setDataRate(RATE_ADS1115_250SPS);
  ads.begin(); // ads1115 start
  ads.startADCReading(ADS1X15_REG_CONFIG_MUX_DIFF_0_1, /*continuous=*/true);
  logBootMessage(F("BOOT: ads ok"));

  sensorAverage.clear();
  for (int cx = 0; cx < kRaSize; cx++) {
    delay(kAdsConversionSpacingMs);
    readSensor();
  }
  logBootMessage(F("BOOT: warmup ok"));

  if (!readCalibration(state.calibrationValue)) {
    logBootMessage(F("BOOT: calibrating"));
    updateCalibrationScale(calibrate());
    logBootMessage(F("BOOT: calibration done"));
  } else {
    updateCalibrationScale(state.calibrationValue);
    logBootMessage(F("BOOT: calibration loaded"));
  }

  if (wokeFromAutoOff) {
    // The wake press may still be held; ignore it until released so it does
    // not register as a new press (which would trigger the lock screen).
    state.ignoreHeldButton = true;
  }

  beep(1);
  logBootMessage(F("BOOT: ready"));
}

int calibrate() {
  renderCalibrationScreen(display);

  sensorAverage.clear();
  float result = 0.0F;
  for (int cx = 0; cx < kRaSize; cx++) {
    delay(kAdsConversionSpacingMs);
    readSensor();
  }
  result = sensorAverage.average();
  if (result < 0.0F) {
    result = -result;
  }
  if (result < kMinValidCalibration) {
    result = kMinValidCalibration;
  } else if (result > kMaxValidCalibration) {
    result = kMaxValidCalibration;
  }
  writeCalibration(static_cast<uint16_t>(result));
  updateCalibrationScale(result);

  beep(1);
  pauseWithPolling(kStatusScreenMs);
  invalidateDisplaySnapshot();
  state.activeFrames = 0;
  return result;
}

void analyze() {
  float currentmv = 0.0F;
  float result = 0.0F;
  float mv = 0.0F;
  DisplaySnapshot snapshot = {};

  currentmv = sensorAverage.average();
  if (currentmv < 0.0F) {
    currentmv = -currentmv;
  }
  mv = currentmv * kAdsMultiplier;

  if (state.calibrationScale <= 0.0F || mv < kMinValidMillivolts) {
    result = 0;
  } else {
    result = currentmv * state.calibrationScale;
  }
  if (result > 99.9F) {
    result = 99.9F;
  }

  snapshot.initialized = true;
  snapshot.sensorError = mv < kMinValidMillivolts || result <= 0;
  snapshot.resultTenths = roundToTenths(result);
  snapshot.mvHundredths = roundToHundredths(mv);
  snapshot.maxPo1Tenths = roundToTenths(state.maxPo1);
  snapshot.modInFeet = state.modInFeet;
  snapshot.blinkVisible = (state.activeFrames % 4) != 0;
  snapshot.holdMenu = currentHoldMenu();

  if (snapshot.sensorError) {
    if (lastDisplaySnapshot.initialized &&
        lastDisplaySnapshot.sensorError == snapshot.sensorError &&
        lastDisplaySnapshot.mvHundredths == snapshot.mvHundredths) {
      return;
    }

    lastDisplaySnapshot = snapshot;
    renderSensorErrorScreen(display);
  } else {
    if (result >= state.resultMax) {
      state.resultMax = result;
    }

    // A meaningful O2 change means the analyzer is in use; hold off auto power-off.
    const int16_t activityDelta = snapshot.resultTenths - state.activityResultTenths;
    if (activityDelta >= kActivityDeltaTenths || activityDelta <= -kActivityDeltaTenths) {
      state.activityResultTenths = snapshot.resultTenths;
      state.lastActivityMs = millis();
    }

    snapshot.resultMaxTenths = roundToTenths(state.resultMax);
    snapshot.modPrimaryTenths = roundToTenths(calculateModDepth(result, state.maxPo1));
    snapshot.modSecondaryTenths =
        roundToTenths(calculateModDepth(result, kDefaultMaxPo2));

    if (lastDisplaySnapshot.initialized &&
        !lastDisplaySnapshot.sensorError &&
        lastDisplaySnapshot.resultTenths == snapshot.resultTenths &&
        lastDisplaySnapshot.resultMaxTenths == snapshot.resultMaxTenths &&
        lastDisplaySnapshot.mvHundredths == snapshot.mvHundredths &&
        lastDisplaySnapshot.maxPo1Tenths == snapshot.maxPo1Tenths &&
        lastDisplaySnapshot.modInFeet == snapshot.modInFeet &&
        lastDisplaySnapshot.modPrimaryTenths == snapshot.modPrimaryTenths &&
        lastDisplaySnapshot.modSecondaryTenths == snapshot.modSecondaryTenths &&
        lastDisplaySnapshot.blinkVisible == snapshot.blinkVisible &&
        lastDisplaySnapshot.holdMenu == snapshot.holdMenu) {
      return;
    }
    lastDisplaySnapshot = snapshot;
    renderAnalyzerScreen(display, snapshot);
  }
}

void lock_screen(unsigned long pause = kLockScreenMs) {
  beep(1);
  renderLockScreen(display);
  const unsigned long startMs = millis();
  while ((millis() - startMs) < pause) {
    if (digitalRead(kButtonPin) == LOW) {
      break;
    }
    sampleSensorIfDue();
    delay(kPausePollMs);
  }
  invalidateDisplaySnapshot();
  state.activeFrames = 0;
  state.firstPressTime = millis();
}

void po2_change() {
  const int16_t t = roundToTenths(state.maxPo1);
  state.maxPo1 =
      (t == roundToTenths(1.3F)) ? 1.4F : (t == roundToTenths(1.4F)) ? 1.5F : 1.3F;
  const uint16_t newTenths = static_cast<uint16_t>(roundToTenths(state.maxPo1));
  writeSettings();

  renderPo2Screen(display, newTenths);
  beep(1);
  pauseWithPolling(kStatusScreenMs);
  invalidateDisplaySnapshot();
  state.activeFrames = 0;
}

void buzzer_toggle() {
  state.buzzerEnabled = !state.buzzerEnabled;
  writeSettings();

  renderBuzzerScreen(display, state.buzzerEnabled);

  if (state.buzzerEnabled) {
    beep(1);
  }

  pauseWithPolling(kStatusScreenMs);
  invalidateDisplaySnapshot();
  state.activeFrames = 0;
}

void mod_unit_toggle() {
  state.modInFeet = !state.modInFeet;
  writeSettings();

  renderModUnitScreen(display, state.modInFeet);
  beep(1);
  pauseWithPolling(kStatusScreenMs);
  invalidateDisplaySnapshot();
  state.activeFrames = 0;
}

void onPowerOffWake() {}

bool powerOffWarningCancelled() {
  for (uint8_t secondsLeft = kPowerOffWarningSeconds; secondsLeft > 0; --secondsLeft) {
    const unsigned long cycleStartMs = millis();
    renderPowerOffWarningScreen(display, secondsLeft);
    if (secondsLeft == kPowerOffWarningSeconds) {
      beep(1);
    }

    while ((millis() - cycleStartMs) < kMsPerSecond) {
      if (digitalRead(kButtonPin) == LOW) {
        while (digitalRead(kButtonPin) == LOW) {
          delay(kPausePollMs);
        }
        return true;
      }
      handleSerialCommands();
      sampleSensorIfDue();
      delay(kPausePollMs);
    }
  }

  return false;
}

void powerDown() {
  if (powerOffWarningCancelled()) {
    const unsigned long now = millis();
    state.lastActivityMs = now;
    state.firstPressTime = now;
    state.activeFrames = 0;
    invalidateDisplaySnapshot();
    return;
  }

  renderAutoOffScreen(display);
  beep(2);
  pauseWithPolling(kStatusScreenMs);

  display.ssd1306_command(SSD1306_DISPLAYOFF);
  // Drop the ADS1115 back to single-shot; it powers itself down after one
  // final conversion.
  ads.startADCReading(ADS1X15_REG_CONFIG_MUX_DIFF_0_1, /*continuous=*/false);
  // Mark the next reboot as a wake so the held wake press is not treated as
  // a settings-reset hold.
  EEPROM.update(kWakeMarkerAddress, kWakeMarkerMagic);

  attachInterrupt(digitalPinToInterrupt(kButtonPin), onPowerOffWake, LOW);
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  noInterrupts();
  sleep_enable();
#if defined(BODS) && defined(BODSE)
  sleep_bod_disable();
#endif
  interrupts();
  sleep_cpu();
  sleep_disable();
  detachInterrupt(digitalPinToInterrupt(kButtonPin));

  // Button pressed: reboot through the watchdog for a clean re-init.
  wdt_enable(WDTO_15MS);
  for (;;) {
  }
}

void max_clear() {
  state.resultMax = 0;
  renderMaxClearedScreen(display);
  beep(1);
  pauseWithPolling(kStatusScreenMs);
  invalidateDisplaySnapshot();
  state.activeFrames = 0;
}

void loop(void) {
  handleSerialCommands();
  sampleSensorIfDue();

  const unsigned long now = millis();
  const int current = digitalRead(kButtonPin);

  if (state.ignoreHeldButton) {
    // Consume the still-held wake press from auto power-off; treat the
    // button as inactive until it has been released once.
    if (current == HIGH) {
      state.ignoreHeldButton = false;
      state.firstPressTime = now;
      state.lastActivityMs = now;
    }
    state.millisHeld = 0;
    state.previousButtonState = current;
  } else {
    if (current == LOW && state.previousButtonState == HIGH &&
        (now - state.firstPressTime) > kButtonDebounceMs) {
      state.firstPressTime = now;
      state.activeFrames = 17;
      state.lastActivityMs = now;
    }

    const unsigned long releasedHoldMs =
        (current == HIGH && state.previousButtonState == LOW)
            ? (now - state.firstPressTime)
            : 0;
    state.millisHeld = (current == LOW) ? (now - state.firstPressTime) : 0;

    if (releasedHoldMs > 2) {
      state.lastActivityMs = now;
      if (releasedHoldMs < kMenuEntryHoldMs) {
        lock_screen();
      } else {
        runHoldMenuAction(menuIndexForHoldDuration(releasedHoldMs));
      }

      state.millisHeld = 0;
      state.firstPressTime = now;
    }

    state.previousButtonState = current;
  }

  if ((now - state.lastActivityMs) >= kAutoPowerOffMs) {
    // Shows a cancellable countdown; if not cancelled, sleeps and only
    // returns via a button-press watchdog reboot.
    powerDown();
  }

  if ((now - state.lastUiRefreshMs) >= kUiRefreshIntervalMs) {
    state.lastUiRefreshMs = now;
    analyze();
    state.activeFrames++;
  }

  // Idle until the next timer tick (~1ms); millis, serial, and tone keep running.
  set_sleep_mode(SLEEP_MODE_IDLE);
  sleep_mode();
}
