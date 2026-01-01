#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../fs/index.hpp"
#include "../audio/index.hpp"
#include "../screen/index.hpp"
#include "../wifi/index.hpp"
#include "../anim/entry.hpp"

#include "startup.hpp"
#include "sd-setup.hpp"

void initializeSetup()
{
    pinMode(TFT_BL, OUTPUT);
    analogWrite(TFT_BL, 0);
    // disable Arduino loop watchdog
    disableCore0WDT();
    // disableCore1WDT();
    esp_task_wdt_delete(NULL); // unregister this task

    Serial.begin(115200);
    pinMode(0, INPUT_PULLUP); // Button is active LOW

    // Audio::init(60);
    sdSetup();
    Screen::init();
    UserWiFi::start();

    startupCheck();

#ifdef USE_STARTUP_ANIMATION
    startAnimationMWOS();
#endif
}