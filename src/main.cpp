/*****************************************************************************
* ej's o2 oled analyzer
* http://ejlabs.net/arduino-oled-nitrox-analyzer
*
* License
* -------
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
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "FreeSansBold18pt7bSubset.h"
#include "FreeSans9pt7bSubset.h"
#include "settings.h"
#include <Adafruit_ADS1X15.h>
#include <EEPROM.h>

struct CalibrationData {
  uint8_t magic;
  uint16_t value;
};

struct AnalyzerState {
  float calibrationValue = 0.0F;
  float resultMax = 0.0F;
  float maxPo1 = kDefaultMinPo2;
  bool buzzerEnabled = kBuzzerEnabledByDefault;
  unsigned long millisHeld = 0;
  byte previousButtonState = HIGH;
  unsigned long firstPressTime = 0;
  int activeFrames = 0;
  unsigned long lastUiRefreshMs = 0;
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

struct DisplaySnapshot {
  bool initialized = false;
  bool sensorError = false;
  int16_t resultTenths = 0;
  int16_t resultMaxTenths = 0;
  int16_t mvHundredths = 0;
  int16_t maxPo1Tenths = 0;
  int16_t modPrimaryTenths = 0;
  int16_t modSecondaryTenths = 0;
  bool blinkVisible = false;
  uint8_t holdMenu = 0;
};

int calibrate();
bool readCalibration(float &value);
void writeCalibration(uint16_t value);
bool isCalibrationValid(float value);
void pauseWithPolling(unsigned long durationMs);
void readSensor();
void analyze();
void drawHoldMenuLabel(const char *label);
void invalidateDisplaySnapshot();
void drawCenteredText(const char *text, int16_t baselineY);
void handleSerialCommands();
void dumpDisplayBuffer();
int16_t roundToTenths(float value);
int16_t roundToHundredths(float value);
uint8_t menuIndexForHoldDuration(unsigned long heldDurationMs);
uint8_t currentHoldMenu();
void runHoldMenuAction(uint8_t menuIndex);
void po2_change();
void buzzer_toggle();
void max_clear();
char *appendText(char *buffer, const char *text);
char *appendTenthsText(char *buffer, uint16_t value);
void formatTenthsText(uint16_t value, char *buffer, bool appendPercent = false);
void formatHundredthsText(uint16_t value, char *buffer, const char *prefix = nullptr);

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
constexpr uint8_t kHoldMenuItemCount = 4;

static_assert(kMenuStepIntervalMs > 0, "kMenuStepIntervalMs must be greater than 0");

/*
 Calculate MOD (Maximum Operating Depth)
*/
float cal_mod (float percentage, float ppo2 = 1.4) {
  return 10 * ( (ppo2/(percentage/100)) - 1 );
}

void beep(int x=1) { // make beep for x time
  if (!state.buzzerEnabled) {
    return;
  }

  for(int i=0; i<x; i++) {
      tone(kBuzzerPin, 2800, 100);
      pauseWithPolling(200);
  }
  noTone(kBuzzerPin);
}

void pauseWithPolling(unsigned long durationMs) {
  const unsigned long startMs = millis();
  while ((millis() - startMs) < durationMs) {
    handleSerialCommands();
    delay(kPausePollMs);
  }
}

void dumpDisplayBuffer() {
  const uint8_t *buffer = display.getBuffer();
  const size_t bufferSize = static_cast<size_t>(kScreenWidth) * static_cast<size_t>(kScreenHeight) / 8U;

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
  int16_t millivolts = 0;
  millivolts = ads.readADC_Differential_0_1();
  sensorAverage.addValue(millivolts);
}

void drawCenteredText(const char *text, int16_t baselineY) {
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  display.getTextBounds(text, 0, baselineY, &x1, &y1, &width, &height);
  display.setCursor(((kScreenWidth - static_cast<int16_t>(width)) / 2) - x1, baselineY);
  display.print(text);
}

void drawHoldMenuLabel(const char *label) {
  constexpr int kMenuX = 0;
  constexpr int kMenuY = 31;
  constexpr int kMenuWidth = 128;
  constexpr int kMenuHeight = 16;

  display.fillRect(kMenuX, kMenuY, kMenuWidth, kMenuHeight, WHITE);
  display.setTextSize(2);
  display.setTextColor(BLACK, WHITE);
  drawCenteredText(label, kMenuY);
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
  const uint8_t cycleSlot = static_cast<uint8_t>((menuElapsedMs / kMenuStepIntervalMs) % kMenuCycleSlots);
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
    max_clear();
  }
}

char *appendText(char *buffer, const char *text) {
  while (*text != '\0') {
    *buffer++ = *text++;
  }
  *buffer = '\0';
  return buffer;
}

char *appendTenthsText(char *buffer, uint16_t value) {
  formatTenthsText(value, buffer, false);
  while (*buffer != '\0') {
    ++buffer;
  }
  return buffer;
}

void formatTenthsText(uint16_t value, char *buffer, bool appendPercent) {
  uint16_t whole = value / 10;
  const uint8_t fractional = value % 10;
  char digits[5] = {};
  uint8_t digitCount = 0;

  do {
    digits[digitCount++] = static_cast<char>('0' + (whole % 10));
    whole /= 10;
  } while (whole > 0);

  for (int8_t i = digitCount - 1; i >= 0; --i) {
    *buffer++ = digits[i];
  }
  *buffer++ = '.';
  *buffer++ = static_cast<char>('0' + fractional);
  if (appendPercent) {
    *buffer++ = '%';
  }
  *buffer = '\0';
}

void formatHundredthsText(uint16_t value, char *buffer, const char *prefix) {
  if (prefix != nullptr) {
    while (*prefix != '\0') {
      *buffer++ = *prefix++;
    }
  }

  uint16_t whole = value / 100;
  const uint8_t fractional = value % 100;
  char digits[5] = {};
  uint8_t digitCount = 0;

  do {
    digits[digitCount++] = static_cast<char>('0' + (whole % 10));
    whole /= 10;
  } while (whole > 0);

  for (int8_t i = digitCount - 1; i >= 0; --i) {
    *buffer++ = digits[i];
  }
  *buffer++ = '.';
  *buffer++ = static_cast<char>('0' + (fractional / 10));
  *buffer++ = static_cast<char>('0' + (fractional % 10));
  *buffer = '\0';
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

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, kScreenAddress)) {
    for(;;); // Don't proceed, loop forever
  }

  display.clearDisplay();
  display.setTextColor(WHITE);

  display.setFont(&FreeSans9pt7bSubset);
  display.setTextSize(1);
  drawCenteredText("Nitrox", 18);
  drawCenteredText("Analyzer", 36);

  display.setFont();
  display.setTextSize(1);
  drawCenteredText(VERSION, 48);
  display.display();
  invalidateDisplaySnapshot();

  delay(2000); // Pause for 2 seconds

  ads.setGain(GAIN_TWO);
  ads.begin(); // ads1115 start

  pinMode(kButtonPin,INPUT_PULLUP);

    sensorAverage.clear();
  for(int cx=0; cx< kRaSize; cx++) {
     readSensor();
  }

  if (!readCalibration(state.calibrationValue)) {
    state.calibrationValue = calibrate();
  }

  beep(1);
}

int calibrate() {

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setFont(&FreeSans9pt7bSubset);
  display.setTextSize(1);
  drawCenteredText("Calibrate", 34);
  display.display();

  sensorAverage.clear();
  float result;
  for(int cx=0; cx< kRaSize; cx++) {
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

  readSensor();
  currentmv = sensorAverage.average();
  if (currentmv < 0.0F) {
    currentmv = -currentmv;
  }
  mv = currentmv * kAdsMultiplier;

  if (!isCalibrationValid(state.calibrationValue) || mv < kMinValidMillivolts) {
    result = 0;
  } else {
    result = (currentmv / state.calibrationValue) * kAirCalibrationPercent;
  }
  if (result > 99.9) result = 99.9;

  snapshot.initialized = true;
  snapshot.sensorError = mv < kMinValidMillivolts || result <= 0;
  snapshot.resultTenths = roundToTenths(result);
  snapshot.mvHundredths = roundToHundredths(mv);
  snapshot.maxPo1Tenths = roundToTenths(state.maxPo1);
  snapshot.blinkVisible = (state.activeFrames % 4) != 0;
  snapshot.holdMenu = currentHoldMenu();

  if (snapshot.sensorError) {
    if (lastDisplaySnapshot.initialized &&
        lastDisplaySnapshot.sensorError == snapshot.sensorError &&
        lastDisplaySnapshot.mvHundredths == snapshot.mvHundredths) {
      return;
    }

    lastDisplaySnapshot = snapshot;
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setFont(&FreeSans9pt7bSubset);
    display.setTextSize(1);
    drawCenteredText("Sensor", 22);
    drawCenteredText("Error!", 47);
    display.display();
  } else {
    char maxLine[24] = {};
    char po2Line[20] = {};
    char modLine[16] = {};
    char resultText[8] = {};

    if (result >= state.resultMax) {
      state.resultMax = result;
    }

    snapshot.resultMaxTenths = roundToTenths(state.resultMax);
    snapshot.modPrimaryTenths = roundToTenths(cal_mod(result, state.maxPo1));
    snapshot.modSecondaryTenths = roundToTenths(cal_mod(result, kDefaultMaxPo2));

    if (lastDisplaySnapshot.initialized &&
        !lastDisplaySnapshot.sensorError &&
        lastDisplaySnapshot.resultTenths == snapshot.resultTenths &&
        lastDisplaySnapshot.resultMaxTenths == snapshot.resultMaxTenths &&
        lastDisplaySnapshot.mvHundredths == snapshot.mvHundredths &&
        lastDisplaySnapshot.maxPo1Tenths == snapshot.maxPo1Tenths &&
        lastDisplaySnapshot.modPrimaryTenths == snapshot.modPrimaryTenths &&
        lastDisplaySnapshot.modSecondaryTenths == snapshot.modSecondaryTenths &&
        lastDisplaySnapshot.blinkVisible == snapshot.blinkVisible &&
        lastDisplaySnapshot.holdMenu == snapshot.holdMenu) {
      return;
    }

    lastDisplaySnapshot = snapshot;
    display.clearDisplay();
    display.setTextColor(WHITE);

    display.setFont(&FreeSansBold18pt7bSubset);
    display.setTextSize(1);
    formatTenthsText(snapshot.resultTenths, resultText, true);
    drawCenteredText(resultText, 28);
    display.setFont();

    display.setTextSize(1);
    display.fillRect(0, 31, kScreenWidth, 8, WHITE);
    display.setTextColor(BLACK, WHITE);
    char *maxLineEnd = appendText(maxLine, "Max ");
    maxLineEnd = appendTenthsText(maxLineEnd, snapshot.resultMaxTenths);
    maxLineEnd = appendText(maxLineEnd, "% ");
    formatHundredthsText(snapshot.mvHundredths, maxLineEnd);
    while (*maxLineEnd != '\0') {
      ++maxLineEnd;
    }
    appendText(maxLineEnd, "mv");
    drawCenteredText(maxLine, 31);

    if (state.activeFrames % 4) {
      display.setCursor(118,10);
      display.setTextColor(WHITE);
      display.print(F("."));
    }

    display.setTextColor(WHITE);
  char *po2LineEnd = appendText(po2Line, "pO2 ");
  po2LineEnd = appendTenthsText(po2LineEnd, snapshot.maxPo1Tenths);
  po2LineEnd = appendText(po2LineEnd, "/");
  po2LineEnd = appendTenthsText(po2LineEnd, roundToTenths(kDefaultMaxPo2));
  appendText(po2LineEnd, " MOD");
  drawCenteredText(po2Line, 40);

    display.setFont(&FreeSans9pt7bSubset);
    display.setTextSize(1);
  char *modLineEnd = appendTenthsText(modLine, snapshot.modPrimaryTenths);
  modLineEnd = appendText(modLineEnd, "/");
  modLineEnd = appendTenthsText(modLineEnd, snapshot.modSecondaryTenths);
  appendText(modLineEnd, "M");
  drawCenteredText(modLine, 63);
    display.setFont();

    // Show menu labels only after the display has settled to reduce flicker.
    if (snapshot.holdMenu == 1) {
      drawHoldMenuLabel("CAL");
    } else if (snapshot.holdMenu == 2) {
      drawHoldMenuLabel("PO2");
    } else if (snapshot.holdMenu == 3) {
      drawHoldMenuLabel("BUZ");
    } else if (snapshot.holdMenu == 4) {
      drawHoldMenuLabel("MAX");
    }
    display.display();
  }
}

void lock_screen(unsigned long pause = kLockScreenMs) {
  beep(1);
  display.setTextSize(1);
  display.fillRect(0, 31, kScreenWidth, 8, WHITE);
  display.setTextColor(BLACK, WHITE);
  drawCenteredText("LOCK", 31);
  display.display();
  const unsigned long startMs = millis();
  while ((millis() - startMs) < pause) {
    if (digitalRead(kButtonPin) == LOW) {
      break;
    }
    delay(kPausePollMs);
   }
  invalidateDisplaySnapshot();
   state.activeFrames = 0;
   state.firstPressTime = millis();
}

void po2_change() {
  if (state.maxPo1 == 1.3F) {
    state.maxPo1 = 1.4F;
  } else if (state.maxPo1 == 1.4F) {
    state.maxPo1 = 1.5F;
  } else {
    state.maxPo1 = 1.3F;
  }

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setFont(&FreeSans9pt7bSubset);
  display.setTextSize(1);
  char po2Text[12] = {};
  formatHundredthsText(static_cast<uint16_t>(state.maxPo1 * 100.0F + 0.5F), po2Text, "pO2: ");
  drawCenteredText(po2Text, 34);
  display.display();
  beep(1);
  pauseWithPolling(kStatusScreenMs);
  invalidateDisplaySnapshot();
  state.activeFrames = 0;
}

void buzzer_toggle() {
  state.buzzerEnabled = !state.buzzerEnabled;

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setFont(&FreeSans9pt7bSubset);
  display.setTextSize(1);
  drawCenteredText("Buzzer", 22);
  drawCenteredText(state.buzzerEnabled ? "Enabled" : "Disabled", 47);
  display.display();

  if (state.buzzerEnabled) {
    beep(1);
  }

  pauseWithPolling(kStatusScreenMs);
  invalidateDisplaySnapshot();
  state.activeFrames = 0;
}

void max_clear() {
  state.resultMax = 0;
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setFont(&FreeSans9pt7bSubset);
  display.setTextSize(1);
  drawCenteredText("Max Result", 22);
  drawCenteredText("Cleared", 47);
  display.display();
  beep(1);
  pauseWithPolling(kStatusScreenMs);
  invalidateDisplaySnapshot();
  state.activeFrames = 0;
}

void loop(void) {
  handleSerialCommands();

  const unsigned long now = millis();
  int current = digitalRead(kButtonPin);

  if (current == LOW && state.previousButtonState == HIGH && (now - state.firstPressTime) > kButtonDebounceMs) {
    state.firstPressTime = now;
    state.activeFrames = 17;
  }

  const unsigned long releasedHoldMs = (current == HIGH && state.previousButtonState == LOW)
      ? (now - state.firstPressTime)
      : 0;
  state.millisHeld = (current == LOW) ? (now - state.firstPressTime) : 0;

  if (releasedHoldMs > 2) {
    if (releasedHoldMs < kMenuEntryHoldMs) {
        lock_screen();
    } else {
      runHoldMenuAction(menuIndexForHoldDuration(releasedHoldMs));
    }

    state.millisHeld = 0;
    state.firstPressTime = now;
  }

  state.previousButtonState = current;

  if ((now - state.lastUiRefreshMs) >= kUiRefreshIntervalMs) {
    state.lastUiRefreshMs = now;
    analyze();
    state.activeFrames++;
  }
}
