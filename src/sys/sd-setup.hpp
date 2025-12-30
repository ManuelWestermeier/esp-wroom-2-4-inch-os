#pragma once

#include <Arduino.h>

#include "../fs/index.hpp"

void sdSetup()
{
    SD_FS::init();

    if (!SPIFFS.begin(true))
    {
        Serial.println("⚠️ SPIFFS mount failed");
    }

    SD_FS::lsDirSerial("/");

    if (!SD_FS::exists("/settings"))
    {
        SD_FS::createDir("/settings");
    }

    if (!SD_FS::exists("/public"))
    {
        SD_FS::createDir("/public");
    }
}