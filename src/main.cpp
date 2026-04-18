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
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Adafruit_ADS1X15.h>
#include <EEPROM.h>

#define VERSION "V0.25"

constexpr uint8_t kCalibrationMagic = 0xA5;
constexpr int kCalibrationAddress = 0;
constexpr int kRaSize = 20;
constexpr int kScreenWidth = 128;
constexpr int kScreenHeight = 64;
constexpr int kOledReset = 4;
constexpr uint8_t kScreenAddress = 0x3C;
constexpr uint8_t kButtonPin = 2;
constexpr uint8_t kBuzzerPin = 9;
constexpr unsigned long kCalHoldTimeSeconds = 2;
constexpr unsigned long kModHoldTimeSeconds = 3;
constexpr unsigned long kMaxHoldTimeSeconds = 4;
constexpr unsigned long kButtonDebounceMs = 200;
constexpr unsigned long kUiRefreshIntervalMs = 200;
constexpr unsigned long kStatusScreenMs = 1200;
constexpr unsigned long kLockScreenMs = 5000;
constexpr unsigned long kPausePollMs = 10;
constexpr unsigned long kMsPerSecond = 1000;
constexpr unsigned long kCalHoldTimeMs = kCalHoldTimeSeconds * kMsPerSecond;
constexpr unsigned long kModHoldTimeMs = kModHoldTimeSeconds * kMsPerSecond;
constexpr unsigned long kMaxHoldTimeMs = kMaxHoldTimeSeconds * kMsPerSecond;
constexpr unsigned long kMenuTimeoutMs = (kMaxHoldTimeSeconds + 1) * kMsPerSecond;
constexpr unsigned long kActionTimeoutMs = 10 * kMsPerSecond;
constexpr double kMinValidCalibration = 100.0;
constexpr double kMaxValidCalibration = 32000.0;
constexpr double kMinValidMillivolts = 0.02;
constexpr double kAirCalibrationPercent = 20.9;
constexpr float kAdsMultiplier = 0.0625F;
constexpr float kDefaultMaxPo2 = 1.60F;

struct CalibrationData {
  uint8_t magic;
  uint16_t value;
};

struct AnalyzerState {
  float calibrationValue = 0.0F;
  float resultMax = 0.0F;
  float maxPo1 = 1.30F;
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
void drawHoldMenuLabel(const __FlashStringHelper *label);
void invalidateDisplaySnapshot();
int16_t roundToTenths(float value);
int16_t roundToHundredths(float value);
uint8_t currentHoldMenu();
void printUnsignedTenths(uint16_t value);
void printUnsignedHundredths(uint16_t value);

SensorAverage sensorAverage;
AnalyzerState state;
DisplaySnapshot lastDisplaySnapshot;

Adafruit_ADS1115 ads;
Adafruit_SSD1306 display(kScreenWidth, kScreenHeight, &Wire, kOledReset);

/*
 Calculate MOD (Maximum Operating Depth)
*/
const float max_po2 = kDefaultMaxPo2;
float cal_mod (float percentage, float ppo2 = 1.4) {
  return 10 * ( (ppo2/(percentage/100)) - 1 );
}

void beep(int x=1) { // make beep for x time
  for(int i=0; i<x; i++) {
      tone(kBuzzerPin, 2800, 100);
      pauseWithPolling(200);
  }
  noTone(kBuzzerPin);
}

void pauseWithPolling(unsigned long durationMs) {
  const unsigned long startMs = millis();
  while ((millis() - startMs) < durationMs) {
    delay(kPausePollMs);
  }
}

void readSensor() {
  int16_t millivolts = 0;
  millivolts = ads.readADC_Differential_0_1();
  sensorAverage.addValue(millivolts);
}

void drawHoldMenuLabel(const __FlashStringHelper *label) {
  constexpr int kMenuX = 0;
  constexpr int kMenuY = 31;
  constexpr int kMenuWidth = 128;
  constexpr int kMenuHeight = 16;
  constexpr int kLabelX = 46;

  display.fillRect(kMenuX, kMenuY, kMenuWidth, kMenuHeight, WHITE);
  display.setTextSize(2);
  display.setTextColor(BLACK, WHITE);
  display.setCursor(kLabelX, kMenuY);
  display.print(label);
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

uint8_t currentHoldMenu() {
  if (state.activeFrames <= 16 || state.millisHeld >= kMenuTimeoutMs) {
    return 0;
  }
  if (state.millisHeld >= kCalHoldTimeMs && state.millisHeld < kModHoldTimeMs) {
    return 1;
  }
  if (state.millisHeld >= kModHoldTimeMs && state.millisHeld < kMaxHoldTimeMs) {
    return 2;
  }
  if (state.millisHeld >= kMaxHoldTimeMs && state.millisHeld < kActionTimeoutMs) {
    return 3;
  }
  return 0;
}

void printUnsignedTenths(uint16_t value) {
  display.print(value / 10);
  display.print('.');
  display.print(value % 10);
}

void printUnsignedHundredths(uint16_t value) {
  display.print(value / 100);
  display.print('.');
  const uint8_t fractional = value % 100;
  if (fractional < 10) {
    display.print('0');
  }
  display.print(fractional);
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
 Serial.begin(9600);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, kScreenAddress)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  display.clearDisplay();
  display.setTextColor(WHITE);

  display.setFont(&FreeSans9pt7b);
  display.setTextSize(1);
  display.setCursor(45,16);
  display.print(F("Nitrox"));

  display.setFont(&FreeSans9pt7b);
  display.setCursor(32,34);
  display.print(F("Analyzer"));

  display.setFont();
  display.setCursor(51,48);
  display.setTextSize(1);
  display.print(F(VERSION));
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
  display.setCursor(30,30);
  display.setFont(&FreeSans9pt7b);
  display.setTextSize(1);
  display.print(F("Calibrate"));
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
  snapshot.sensorError = mv < 0.06 || result <= 0;
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
    display.setCursor(36,22);
    display.setFont(&FreeSans9pt7b);
    display.setTextSize(1);
    display.print(F("Sensor"));
    display.setCursor(42,47);
    display.print(F("Error!"));
    display.display();
  } else {
    int16_t topLineX = 0;

    if (result >= state.resultMax) {
      state.resultMax = result;
    }

    snapshot.resultMaxTenths = roundToTenths(state.resultMax);
    snapshot.modPrimaryTenths = roundToTenths(cal_mod(result, state.maxPo1));
    snapshot.modSecondaryTenths = roundToTenths(cal_mod(result, max_po2));

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

    display.setCursor(30,0);
    display.setFont(&FreeSansBold18pt7b);
    display.setTextSize(1);
    topLineX = 18;
    if (result >= 10.0) {
      topLineX = 8;
    }
    if (result >= 100.0) {
      topLineX = 0;
    }
    display.setCursor(topLineX, 28);
    printUnsignedTenths(snapshot.resultTenths);
    display.print(F("%"));
    display.setFont();

    display.setTextSize(1);
    display.setTextColor(BLACK, WHITE);
    display.setCursor(0,31);
    display.print(F("  Max "));
    printUnsignedTenths(snapshot.resultMaxTenths);
    display.print(F("%  "));
    printUnsignedHundredths(snapshot.mvHundredths);
    display.print(F("mv  "));

    if (state.activeFrames % 4) {
      display.setCursor(118,10);
      display.setTextColor(WHITE);
      display.print(F("."));
    }

    display.setTextColor(WHITE);
    display.setCursor(21,40);
    display.print(F("pO2 "));
    printUnsignedTenths(snapshot.maxPo1Tenths);
    display.print(F("/"));
    printUnsignedTenths(roundToTenths(max_po2));
    display.print(F(" MOD"));

    display.setFont(&FreeSans9pt7b);
    display.setTextSize(1);
    display.setCursor(3,63);
    display.print(F(" "));
    printUnsignedTenths(snapshot.modPrimaryTenths);
    display.print(F("/"));
    printUnsignedTenths(snapshot.modSecondaryTenths);
    display.print(F("M "));
    display.setFont();

    // Show menu labels only after the display has settled to reduce flicker.
    if (snapshot.holdMenu == 1) {
        drawHoldMenuLabel(F("CAL"));
    } else if (snapshot.holdMenu == 2) {
        drawHoldMenuLabel(F("PO2"));
    } else if (snapshot.holdMenu == 3) {
        drawHoldMenuLabel(F("MAX"));
    }
    display.display();
  }
}

void lock_screen(unsigned long pause = kLockScreenMs) {
  beep(1);
  display.setTextSize(1);
  display.setCursor(0,31);
  display.setTextColor(0xFFFF, 0);
  display.print(F("                 "));
  display.setTextColor(BLACK, WHITE);
  display.setCursor(0,31);
  display.print(F(" ====== LOCK ======= "));
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
  if (state.maxPo1 == 1.3) state.maxPo1 = 1.4;
  else if (state.maxPo1 == 1.4) state.maxPo1 = 1.5;
  else if (state.maxPo1 == 1.5) state.maxPo1 = 1.3;

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setCursor(24,30);
  display.setFont(&FreeSans9pt7b);
  display.setTextSize(1);
  display.print(F("pO2: "));
  printUnsignedHundredths(static_cast<uint16_t>(state.maxPo1 * 100.0F + 0.5F));
  display.display();
  beep(1);
  pauseWithPolling(kStatusScreenMs);
  invalidateDisplaySnapshot();
  state.activeFrames = 0;
}

void max_clear() {
  state.resultMax = 0;
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setCursor(21,22);
  display.setFont(&FreeSans9pt7b);
  display.setTextSize(1);
  display.print(F("Max Result"));
  display.setCursor(34,47);
  display.print(F("Cleared"));
  display.display();
  beep(1);
  pauseWithPolling(kStatusScreenMs);
  invalidateDisplaySnapshot();
  state.activeFrames = 0;
}

void loop(void) {
  const unsigned long now = millis();
  int current = digitalRead(kButtonPin);

  if (current == LOW && state.previousButtonState == HIGH && (now - state.firstPressTime) > kButtonDebounceMs) {
    state.firstPressTime = now;
    state.activeFrames = 17;
  }

  state.millisHeld = now - state.firstPressTime;
  if (state.millisHeld > 2) {
    if (current == HIGH && state.previousButtonState == LOW) {
      if (state.millisHeld < kCalHoldTimeMs) {
        lock_screen();
      }
      if (state.millisHeld >= kCalHoldTimeMs &&
          state.millisHeld < kModHoldTimeMs) {
        state.calibrationValue = calibrate();
      }
      if (state.millisHeld >= kModHoldTimeMs &&
          state.millisHeld < kMaxHoldTimeMs) {
        po2_change();
      }
      if (state.millisHeld >= kMaxHoldTimeMs &&
          state.millisHeld < kActionTimeoutMs) {
        max_clear();
      }
    }
  }

  state.previousButtonState = current;

  if ((now - state.lastUiRefreshMs) >= kUiRefreshIntervalMs) {
    state.lastUiRefreshMs = now;
    analyze();
    state.activeFrames++;
  }
}
