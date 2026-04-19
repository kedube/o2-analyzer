#pragma once

#include <Arduino.h>
#include <Adafruit_SSD1306.h>

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

void renderStartupScreen(Adafruit_SSD1306 &display, const char *version);
void renderCalibrationScreen(Adafruit_SSD1306 &display);
void renderSensorErrorScreen(Adafruit_SSD1306 &display);
void renderAnalyzerScreen(Adafruit_SSD1306 &display, const DisplaySnapshot &snapshot);
void renderLockScreen(Adafruit_SSD1306 &display);
void renderPo2Screen(Adafruit_SSD1306 &display, uint16_t po2Tenths);
void renderBuzzerScreen(Adafruit_SSD1306 &display, bool buzzerEnabled);
void renderMaxClearedScreen(Adafruit_SSD1306 &display);