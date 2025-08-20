#pragma once

#include <Arduino.h>
#include <map>
#include <vector>

#include "windows.hpp"

using std::map;
using std::vector;

struct AppData
{
    vector<Window> windows;
};

map<String, AppData> APP_DATA;