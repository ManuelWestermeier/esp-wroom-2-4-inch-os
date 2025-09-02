#pragma once
#include <Arduino.h>
#include <vector>
#include <algorithm>
#include <TFT_eSPI.h> // wichtig für MC_DATUM

#include "../styles/global.hpp"

#include "icons/index.hpp"

String readString(const String &file, const String &key);