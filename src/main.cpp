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
#include <RunningAverage.h>

#define VERSION "Firmware 0.22"

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
constexpr unsigned long kStatusScreenMs = 1000;
constexpr unsigned long kLockScreenMs = 5000;
constexpr unsigned long kPausePollMs = 10;
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
  double calibrationValue = 0.0;
  double resultMax = 0.0;
  float maxPo1 = 1.30F;
  unsigned long millisHeld = 0;
  unsigned long secsHeld = 0;
  byte previousButtonState = HIGH;
  unsigned long firstPressTime = 0;
  int activeFrames = 0;
  unsigned long lastUiRefreshMs = 0;
};

int calibrate();
bool readCalibration(double &value);
void writeCalibration(uint16_t value);
bool isCalibrationValid(double value);
void pauseWithPolling(unsigned long durationMs);
void readSensor();
void analyze();

RunningAverage RA(kRaSize);
AnalyzerState state;

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
  RA.addValue(millivolts);
}

bool isCalibrationValid(double value) {
  return value >= kMinValidCalibration && value <= kMaxValidCalibration;
}

void writeCalibration(uint16_t value) {
  const CalibrationData data = {kCalibrationMagic, value};
  const uint8_t *raw = reinterpret_cast<const uint8_t *>(&data);
  for (unsigned int i = 0; i < sizeof(CalibrationData); ++i) {
    EEPROM.update(kCalibrationAddress + i, raw[i]);
  }
}

bool readCalibration(double &value) {
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

  display.setCursor(31,6);
  display.setTextSize(2);
  display.print(F("Nitrox"));

  display.setCursor(18,26);
  display.setTextSize(2);
  display.print(F("Analyzer"));

  display.setCursor(26,52);
  display.setTextSize(1);
  display.print(F(VERSION));
  display.display();

  delay(2000); // Pause for 2 seconds

  ads.setGain(GAIN_TWO);
  ads.begin(); // ads1115 start

  pinMode(kButtonPin,INPUT_PULLUP);

  RA.clear();
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
  display.setCursor(0,0);
  display.setTextSize(2);
  display.print(F("Calibrate"));
  display.display();

  RA.clear();
  double result;
  for(int cx=0; cx< kRaSize; cx++) {
    readSensor();
  }
  result = RA.getAverage();
  result = abs(result);
  if (result < kMinValidCalibration) {
    result = kMinValidCalibration;
  } else if (result > kMaxValidCalibration) {
    result = kMaxValidCalibration;
  }
  writeCalibration(static_cast<uint16_t>(result));

  beep(1);
  pauseWithPolling(kStatusScreenMs);
  state.activeFrames = 0;
  return result;
}

void analyze() {
  double currentmv=0;
  double result;
  double mv = 0.0;

  readSensor();
  currentmv = RA.getAverage();
  currentmv = abs(currentmv);
  mv = currentmv * kAdsMultiplier;

  if (!isCalibrationValid(state.calibrationValue) || mv < kMinValidMillivolts) {
    result = 0;
  } else {
    result = (currentmv / state.calibrationValue) * kAirCalibrationPercent;
  }
  if (result > 99.9) result = 99.9;

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setCursor(0,0);

  if (mv < 0.02 || result <= 0) {
     display.setTextSize(2);
     display.println(F("Sensor"));
     display.print(F("Error!"));
  } else {
    display.setFont(&FreeSansBold18pt7b);
    display.setTextSize(1);
    display.setCursor(0,28);
    display.print(result,1);
    display.println(F("%"));
    display.setFont();

    if (result >= state.resultMax) {
      state.resultMax = result;
    }

    display.setTextSize(1);
    display.setCursor(0,31);
    display.setTextColor(BLACK, WHITE);
    display.print(F("Max "));
    display.print(state.resultMax,1);
    display.print(F("%   "));
    display.print(mv,2);
    display.print(F("mv"));

    if (state.activeFrames % 4) {
      display.setCursor(115,29);
      display.setTextColor(WHITE);
      display.print(F("."));
    }

    display.setTextColor(WHITE);
    display.setCursor(0,40);
    display.print(F("pO2 "));
    display.print(state.maxPo1,1);
    display.print(F("/"));
    display.print(max_po2,1);
    display.print(F(" MOD"));

    display.setFont(&FreeSans9pt7b);
    display.setTextSize(1);
    display.setCursor(0,63);
    display.print(cal_mod(result,state.maxPo1),1);
    display.print(F("/"));
    display.print(cal_mod(result,max_po2),1);
    display.print(F("M "));
    display.setFont();

    // Show menu labels only after the display has settled to reduce flicker.
    if (state.secsHeld < 5 && state.activeFrames > 16) {
      display.setTextSize(2);
      display.setCursor(0,31);
      display.setTextColor(BLACK, WHITE);
      if (state.secsHeld >= kCalHoldTimeSeconds && state.secsHeld < kModHoldTimeSeconds) {
        display.print(F("   CAL    "));
      }
      if (state.secsHeld >= kModHoldTimeSeconds && state.secsHeld < kMaxHoldTimeSeconds) {
        display.print(F("   PO2    "));
      }
      if (state.secsHeld >= kMaxHoldTimeSeconds && state.secsHeld < 10) {
        display.print(F("   MAX    "));
      }
    }

  }
  display.display();
}

void lock_screen(unsigned long pause = kLockScreenMs) {
  beep(1);
  display.setTextSize(1);
  display.setCursor(0,31);
  display.setTextColor(0xFFFF, 0);
  display.print(F("                "));
  display.setTextColor(BLACK, WHITE);
  display.setCursor(0,31);
  display.print(F("======= LOCK ======="));
  display.display();
  const unsigned long startMs = millis();
  while ((millis() - startMs) < pause) {
    if (digitalRead(kButtonPin) == LOW) {
      break;
    }
    delay(kPausePollMs);
   }
   state.activeFrames = 0;
   state.firstPressTime = millis();
}

void po2_change() {
  if (state.maxPo1 == 1.3) state.maxPo1 = 1.4;
  else if (state.maxPo1 == 1.4) state.maxPo1 = 1.5;
  else if (state.maxPo1 == 1.5) state.maxPo1 = 1.3;

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.setTextSize(2);
  display.print(F("pO2: "));
  display.print(state.maxPo1);
  display.display();
  beep(1);
  pauseWithPolling(kStatusScreenMs);
  state.activeFrames = 0;
}

void max_clear() {
  state.resultMax = 0;
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.setTextSize(2);
  display.println(F("Max result"));
  display.print(F("cleared"));
  display.display();
  beep(1);
  pauseWithPolling(kStatusScreenMs);
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
  state.secsHeld = state.millisHeld / 1000;

  if (state.millisHeld > 2) {
    if (current == HIGH && state.previousButtonState == LOW) {
      if (state.secsHeld <= 0) {
        lock_screen();
      }
      if (state.secsHeld >= kCalHoldTimeSeconds && state.secsHeld < kModHoldTimeSeconds) {
        state.calibrationValue = calibrate();
      }
      if (state.secsHeld >= kModHoldTimeSeconds && state.secsHeld < kMaxHoldTimeSeconds) {
        po2_change();
      }
      if (state.secsHeld >= kMaxHoldTimeSeconds && state.secsHeld < 10) {
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
