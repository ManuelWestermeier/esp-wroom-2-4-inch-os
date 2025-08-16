#pragma once
#include <Arduino.h>
#include <vector>
#include <algorithm>
#include <TFT_eSPI.h> // wichtig für MC_DATUM

String readString(const String &file, const String &key);