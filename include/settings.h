#pragma once

#include <Arduino.h>

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