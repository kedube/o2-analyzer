#include "display_ui.h"

#include <Adafruit_GFX.h>

#include "FreeSans9pt7bSubset.h"
#include "FreeSansBold18pt7bSubset.h"
#include "settings.h"

namespace {

constexpr uint8_t kHoldMenuItemCount = 5;
constexpr const char *kHoldMenuLabels[kHoldMenuItemCount] = {
    "CAL", "PO2", "BUZ", "MOD", "MAX"};

void drawCenteredText(Adafruit_SSD1306 &display, const char *text, int16_t baselineY) {
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  display.getTextBounds(text, 0, baselineY, &x1, &y1, &width, &height);
  display.setCursor(((kScreenWidth - static_cast<int16_t>(width)) / 2) - x1, baselineY);
  display.print(text);
}

void drawHoldMenuLabel(Adafruit_SSD1306 &display, const char *label) {
  constexpr int kMenuX = 0;
  constexpr int kMenuY = 31;
  constexpr int kMenuWidth = 128;
  constexpr int kMenuHeight = 16;

  display.fillRect(kMenuX, kMenuY, kMenuWidth, kMenuHeight, WHITE);
  display.setTextSize(2);
  display.setTextColor(BLACK, WHITE);
  drawCenteredText(display, label, kMenuY);
}

char *appendText(char *buffer, const char *text) {
  while (*text != '\0') {
    *buffer++ = *text++;
  }
  *buffer = '\0';
  return buffer;
}

void formatTenthsText(uint16_t value, char *buffer, bool appendPercent = false) {
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

char *appendTenthsText(char *buffer, uint16_t value) {
  formatTenthsText(value, buffer, false);
  while (*buffer != '\0') {
    ++buffer;
  }
  return buffer;
}

void formatHundredthsText(uint16_t value, char *buffer, const char *prefix = nullptr) {
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

}  // namespace

void renderStartupScreen(Adafruit_SSD1306 &display, const char *version) {
  display.clearDisplay();
  display.setTextColor(WHITE);

  display.setFont(&FreeSans9pt7bSubset);
  display.setTextSize(1);
  drawCenteredText(display, "Nitrox", 18);
  drawCenteredText(display, "Analyzer", 36);

  display.setFont();
  display.setTextSize(1);
  drawCenteredText(display, version, 48);
  display.display();
}

void renderCalibrationScreen(Adafruit_SSD1306 &display) {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setFont(&FreeSans9pt7bSubset);
  display.setTextSize(1);
  drawCenteredText(display, "Calibrate", 34);
  display.display();
}

void renderSensorErrorScreen(Adafruit_SSD1306 &display) {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setFont(&FreeSans9pt7bSubset);
  display.setTextSize(1);
  drawCenteredText(display, "Sensor", 22);
  drawCenteredText(display, "Error!", 47);
  display.display();
}

void renderAnalyzerScreen(Adafruit_SSD1306 &display, const DisplaySnapshot &snapshot) {
  char maxLine[24] = {};
  char po2Line[20] = {};
  char modLine[18] = {};
  char resultText[8] = {};

  display.clearDisplay();
  display.setTextColor(WHITE);

  display.setFont(&FreeSansBold18pt7bSubset);
  display.setTextSize(1);
  formatTenthsText(static_cast<uint16_t>(snapshot.resultTenths), resultText, true);
  drawCenteredText(display, resultText, 28);
  display.setFont();

  display.setTextSize(1);
  display.fillRect(0, 31, kScreenWidth, 8, WHITE);
  display.setTextColor(BLACK, WHITE);
  char *maxLineEnd = appendText(maxLine, "Max ");
  maxLineEnd =
      appendTenthsText(maxLineEnd, static_cast<uint16_t>(snapshot.resultMaxTenths));
  maxLineEnd = appendText(maxLineEnd, "% ");
  formatHundredthsText(static_cast<uint16_t>(snapshot.mvHundredths), maxLineEnd);
  while (*maxLineEnd != '\0') {
    ++maxLineEnd;
  }
  appendText(maxLineEnd, "mv");
  drawCenteredText(display, maxLine, 31);

  if (snapshot.blinkVisible) {
    display.setCursor(122, 10);
    display.setTextColor(WHITE);
    display.print(F("."));
  }

  display.setTextColor(WHITE);
  char *po2LineEnd = appendText(po2Line, "pO2 ");
  po2LineEnd =
      appendTenthsText(po2LineEnd, static_cast<uint16_t>(snapshot.maxPo1Tenths));
  po2LineEnd = appendText(po2LineEnd, "/");
  po2LineEnd = appendTenthsText(
      po2LineEnd, static_cast<uint16_t>(roundToTenths(kDefaultMaxPo2)));
  appendText(po2LineEnd, " MOD");
  drawCenteredText(display, po2Line, 40);

  display.setFont(&FreeSans9pt7bSubset);
  display.setTextSize(1);
  char *modLineEnd =
      appendTenthsText(modLine, static_cast<uint16_t>(snapshot.modPrimaryTenths));
  modLineEnd = appendText(modLineEnd, "/");
  modLineEnd =
      appendTenthsText(modLineEnd, static_cast<uint16_t>(snapshot.modSecondaryTenths));
  appendText(modLineEnd, snapshot.modInFeet ? "FT" : "M");
  drawCenteredText(display, modLine, 61);
  display.setFont();

  if (snapshot.holdMenu >= 1 && snapshot.holdMenu <= kHoldMenuItemCount) {
    drawHoldMenuLabel(display, kHoldMenuLabels[snapshot.holdMenu - 1]);
  }

  display.display();
}

void renderLockScreen(Adafruit_SSD1306 &display) {
  display.setTextSize(1);
  display.fillRect(0, 31, kScreenWidth, 8, WHITE);
  display.setTextColor(BLACK, WHITE);
  drawCenteredText(display, "LOCK", 31);
  display.display();
}

void renderPo2Screen(Adafruit_SSD1306 &display, uint16_t po2Tenths) {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setFont(&FreeSans9pt7bSubset);
  display.setTextSize(1);
  char po2Text[10] = {};
  char *po2End = appendText(po2Text, "pO2: ");
  formatTenthsText(po2Tenths, po2End);
  drawCenteredText(display, po2Text, 34);
  display.display();
}

void renderBuzzerScreen(Adafruit_SSD1306 &display, bool buzzerEnabled) {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setFont(&FreeSans9pt7bSubset);
  display.setTextSize(1);
  drawCenteredText(display, "Buzzer", 22);
  drawCenteredText(display, buzzerEnabled ? "Enabled" : "Disabled", 47);
  display.display();
}

void renderModUnitScreen(Adafruit_SSD1306 &display, bool modInFeet) {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setFont(&FreeSans9pt7bSubset);
  display.setTextSize(1);
  drawCenteredText(display, "MOD Units", 22);
  drawCenteredText(display, modInFeet ? "Feet" : "Meters", 47);
  display.display();
}

void renderMaxClearedScreen(Adafruit_SSD1306 &display) {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setFont(&FreeSans9pt7bSubset);
  display.setTextSize(1);
  drawCenteredText(display, "Max Result", 22);
  drawCenteredText(display, "Cleared", 47);
  display.display();
}