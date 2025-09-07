#pragma once

#include <Arduino.h>

#include "../screen/svg.hpp"

extern NSVGimage *createSVG(String);

namespace SVG
{
    extern NSVGimage *settings;
    extern NSVGimage *wifi;
    extern NSVGimage *design;
    extern NSVGimage *folder;
    extern NSVGimage *account;
    extern NSVGimage *login;
    extern NSVGimage *signin;
}
