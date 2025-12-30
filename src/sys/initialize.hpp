#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "startup.hpp"

void initializeSetup()
{
    // disable Arduino loop watchdog
    disableCore0WDT();
    disableCore1WDT();
    // esp_task_wdt_delete(NULL); // unregister this task

    Serial.begin(115200);
    pinMode(0, INPUT_PULLUP); // Button is active LOW

    // Audio::init(60);
    Screen::init(120);
    SD_FS::init();
    UserWiFi::start();

    startupCheck();
}