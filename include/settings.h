#pragma once

#include <Arduino.h>

#define VERSION "V0.4"

constexpr uint8_t kCalibrationMagic = 0xA5;
constexpr int kCalibrationAddress = 0;

constexpr int kRaSize = 20;

constexpr int kScreenWidth = 128;
constexpr int kScreenHeight = 64;
constexpr int kOledReset = 4;
constexpr uint8_t kScreenAddress = 0x3C;

constexpr uint8_t kButtonPin = 2;
constexpr uint8_t kBuzzerPin = 3;
constexpr bool kBuzzerEnabledByDefault = true;
constexpr unsigned long kSerialBaudRate = 9600;
constexpr char kScreenshotCommand = 's';

constexpr unsigned long kMenuEntryHoldSeconds = 2;
constexpr unsigned long kMenuStepIntervalMs = 1100;
constexpr unsigned long kStatusScreenMs = 1200;
constexpr unsigned long kLockScreenMs = 5000;
constexpr bool kModInFeetByDefault = true;

constexpr double kMinValidCalibration = 100.0;
constexpr double kMaxValidCalibration = 32000.0;
constexpr double kMinValidMillivolts = 0.05;
constexpr double kAirCalibrationPercent = 20.9;

constexpr float kAdsMultiplier = 0.0625F;
constexpr float kFeetPerMeter = 3.28084F;
constexpr float kDefaultMinPo2 = 1.4F;
constexpr float kDefaultMaxPo2 = 1.6F;