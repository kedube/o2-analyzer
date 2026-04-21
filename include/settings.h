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

#pragma once

#include <Arduino.h>

#define VERSION "V1.0"

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
constexpr bool kBootDebugLogging = false;
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