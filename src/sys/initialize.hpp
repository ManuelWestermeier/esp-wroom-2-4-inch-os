#pragma once

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../fs/index.hpp"
#include "../audio/index.hpp"
#include "../screen/index.hpp"
#include "../wifi/index.hpp"
#include "../anim/entry.hpp"
#include "../led/index.hpp"
#include "../config.hpp"
#include "../sys-apps/app-menager.hpp"

#include "startup.hpp"
#include "sd-setup.hpp"

void testInstallApps()
{
    while (!Windows::canAccess)
        ;
    Windows::canAccess = false; // Reset access flag for next operations
    // AppManager::install("mwsearchapp");
    // AppManager::install("https://mwsearchapp.onrender.com/2");
    // AppManager::install("https://mwsearchapp.onrender.com/3");
    // AppManager::install("https://mwsearchapp.onrender.com/4");
    AppManager::install("https://mwsearchapp.onrender.com/5");
    AppManager::install("https://mwsearchapp.onrender.com/6");
    AppManager::install("https://mwsearchapp.onrender.com/7");
    
    Windows::canAccess = true; // Allow access again after installations
}

void initializeSetup()
{
    pinMode(TFT_BL, OUTPUT);
    pinMode(0, INPUT_PULLUP); // Button is active LOW

    // disable Arduino loop watchdog
    disableCore0WDT();
    // disableCore1WDT();
    esp_task_wdt_delete(NULL); // unregister this task

    Serial.begin(115200);
    Serial.println("MW-MINI-OS");

    // Audio::init(60);
    sdSetup();
    Screen::init();
    UserWiFi::start();

    LED::init();
    LED::rgb(0, 0, 0);

    startupCheck();

#ifdef USE_STARTUP_ANIMATION
    startAnimationMWOS();
#endif
}